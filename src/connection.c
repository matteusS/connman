/*
 *
 *  Connection Manager
 *
 *  Copyright (C) 2007-2009  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/route.h>

#include <gdbus.h>

#include "connman.h"

struct gateway_data {
	int index;
	char *gateway;
};

static GSList *gateway_list = NULL;

static struct gateway_data *find_gateway(int index, const char *gateway)
{
	GSList *list;

	if (gateway == NULL)
		return NULL;

	for (list = gateway_list; list; list = list->next) {
		struct gateway_data *data = list->data;

		if (data->gateway == NULL)
			continue;

		if (data->index == index &&
				g_str_equal(data->gateway, gateway) == TRUE)
			return data;
	}

	return NULL;
}

static void remove_gateway(int index, const char *gateway)
{
	struct gateway_data *data;

	data = find_gateway(index, gateway);
	if (data == NULL)
		return;

	gateway_list = g_slist_remove(gateway_list, data);
}

static int set_route(struct connman_element *element, const char *gateway)
{
	struct ifreq ifr;
	struct rtentry rt;
	struct sockaddr_in *addr;
	int sk, err;

	DBG("element %p", element);

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -1;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = element->index;

	if (ioctl(sk, SIOCGIFNAME, &ifr) < 0) {
		close(sk);
		return -1;
	}

	DBG("ifname %s", ifr.ifr_name);

	memset(&rt, 0, sizeof(rt));
	rt.rt_flags = RTF_UP | RTF_GATEWAY;

	addr = (struct sockaddr_in *) &rt.rt_dst;
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = INADDR_ANY;

	addr = (struct sockaddr_in *) &rt.rt_gateway;
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = inet_addr(gateway);

	addr = (struct sockaddr_in *) &rt.rt_genmask;
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = INADDR_ANY;

	err = ioctl(sk, SIOCADDRT, &rt);
	if (err < 0)
		connman_error("Setting default route failed (%s)",
							strerror(errno));

	close(sk);

	return err;
}

static int del_route(struct connman_element *element, const char *gateway)
{
	struct ifreq ifr;
	struct rtentry rt;
	struct sockaddr_in *addr;
	int sk, err;

	DBG("element %p", element);

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -1;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = element->index;

	if (ioctl(sk, SIOCGIFNAME, &ifr) < 0) {
		close(sk);
		return -1;
	}

	DBG("ifname %s", ifr.ifr_name);

	memset(&rt, 0, sizeof(rt));
	rt.rt_flags = RTF_UP | RTF_GATEWAY;

	addr = (struct sockaddr_in *) &rt.rt_dst;
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = INADDR_ANY;

	addr = (struct sockaddr_in *) &rt.rt_gateway;
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = inet_addr(gateway);

	addr = (struct sockaddr_in *) &rt.rt_genmask;
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = INADDR_ANY;

	err = ioctl(sk, SIOCDELRT, &rt);
	if (err < 0)
		connman_error("Removing default route failed (%s)",
							strerror(errno));

	close(sk);

	return err;
}

static DBusConnection *connection;

static void emit_default_signal(struct connman_element *element)
{
	DBusMessage *signal;
	DBusMessageIter entry, value;
	const char *key = "Default";

	signal = dbus_message_new_signal(element->path,
			CONNMAN_CONNECTION_INTERFACE, "PropertyChanged");
	if (signal == NULL)
		return;

	dbus_message_iter_init_append(signal, &entry);

	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);

	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
					DBUS_TYPE_BOOLEAN_AS_STRING, &value);
	dbus_message_iter_append_basic(&value, DBUS_TYPE_BOOLEAN,
							&element->enabled);
	dbus_message_iter_close_container(&entry, &value);

	g_dbus_send_message(connection, signal);
}

static void set_default(struct connman_element *element, gpointer user_data)
{
	struct gateway_data *data = user_data;

	DBG("element %p name %s", element, element->name);

	if (element->index != data->index)
		return;

	if (element->enabled == TRUE)
		return;

	connman_element_set_enabled(element, TRUE);
	emit_default_signal(element);
}

static void del_default(struct connman_element *element, gpointer user_data)
{
	struct gateway_data *data = user_data;

	DBG("element %p name %s", element, element->name);

	if (element->index != data->index)
		return;

	if (element->enabled == FALSE)
		return;

	connman_element_set_enabled(element, FALSE);
	emit_default_signal(element);
}

static void new_default(struct connman_element *element, gpointer user_data)
{
	struct connman_service *service;
	const char *gateway;

	DBG("element %p name %s", element, element->name);

	if (g_slist_length(gateway_list) > 0)
		return;

	connman_element_get_value(element,
				CONNMAN_PROPERTY_ID_IPV4_GATEWAY, &gateway);

	DBG("gateway %s", gateway);

	if (gateway == NULL)
		return;

	if (set_route(element, gateway) < 0)
		return;

	service = __connman_element_get_service(element);
	__connman_service_indicate_default(service);

	connman_element_set_enabled(element, TRUE);
	emit_default_signal(element);
}

static void connection_newgateway(int index, const char *gateway)
{
	struct gateway_data *data;

	DBG("index %d gateway %s", index, gateway);

	data = find_gateway(index, gateway);
	if (data != NULL)
		return;

	data = g_try_new0(struct gateway_data, 1);
	if (data == NULL)
		return;

	data->index = index;
	data->gateway = g_strdup(gateway);

	gateway_list = g_slist_append(gateway_list, data);

	__connman_element_foreach(NULL, CONNMAN_ELEMENT_TYPE_CONNECTION,
							set_default, data);
}

static void connection_delgateway(int index, const char *gateway)
{
	struct gateway_data *data;

	DBG("index %d gateway %s", index, gateway);

	data = find_gateway(index, gateway);
	if (data == NULL)
		return;

	gateway_list = g_slist_remove(gateway_list, data);

	__connman_element_foreach(NULL, CONNMAN_ELEMENT_TYPE_CONNECTION,
							del_default, data);

	g_free(data->gateway);
	g_free(data);

	if (g_slist_length(gateway_list) > 0)
		return;

	DBG("selecting new default gateway");

	__connman_element_foreach(NULL, CONNMAN_ELEMENT_TYPE_CONNECTION,
							new_default, NULL);
}

static struct connman_rtnl connection_rtnl = {
	.name		= "connection",
	.newgateway	= connection_newgateway,
	.delgateway	= connection_delgateway,
};

static DBusMessage *get_properties(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct connman_element *element = data;
	DBusMessage *reply;
	DBusMessageIter array, dict;
	connman_uint8_t strength;
	const char *device, *network;
	const char *type;

	DBG("conn %p", conn);

	if (__connman_security_check_privilege(msg,
					CONNMAN_SECURITY_PRIVILEGE_PUBLIC) < 0)
		return __connman_error_permission_denied(msg);

	reply = dbus_message_new_method_return(msg);
	if (reply == NULL)
		return NULL;

	dbus_message_iter_init_append(reply, &array);

	dbus_message_iter_open_container(&array, DBUS_TYPE_ARRAY,
			DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
			DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING
			DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict);

	type = connman_element_get_string(element, "Type");
	if (type != NULL)
		connman_dbus_dict_append_variant(&dict, "Type",
						DBUS_TYPE_STRING, &type);

	strength = connman_element_get_uint8(element, "Strength");
	if (strength > 0)
		connman_dbus_dict_append_variant(&dict, "Strength",
						DBUS_TYPE_BYTE, &strength);

	if (element->devname != NULL)
		connman_dbus_dict_append_variant(&dict, "Interface",
					DBUS_TYPE_STRING, &element->devname);

	connman_dbus_dict_append_variant(&dict, "Default",
					DBUS_TYPE_BOOLEAN, &element->enabled);

	device = __connman_element_get_device_path(element);
	if (device != NULL)
		connman_dbus_dict_append_variant(&dict, "Device",
					DBUS_TYPE_OBJECT_PATH, &device);

	network = __connman_element_get_network_path(element);
	if (network != NULL)
		connman_dbus_dict_append_variant(&dict, "Network",
					DBUS_TYPE_OBJECT_PATH, &network);

	__connman_element_append_ipv4(element, &dict);

	dbus_message_iter_close_container(&array, &dict);

	return reply;
}

static DBusMessage *set_property(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	DBusMessageIter iter, value;
	const char *name;
	int type;

	DBG("conn %p", conn);

	if (dbus_message_iter_init(msg, &iter) == FALSE)
		return __connman_error_invalid_arguments(msg);

	dbus_message_iter_get_basic(&iter, &name);
	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &value);

	if (__connman_security_check_privilege(msg,
					CONNMAN_SECURITY_PRIVILEGE_MODIFY) < 0)
		return __connman_error_permission_denied(msg);

	type = dbus_message_iter_get_arg_type(&value);

	return g_dbus_create_reply(msg, DBUS_TYPE_INVALID);
}

static GDBusMethodTable connection_methods[] = {
	{ "GetProperties", "",   "a{sv}", get_properties },
	{ "SetProperty",   "sv", "",      set_property   },
	{ },
};

static GDBusSignalTable connection_signals[] = {
	{ "PropertyChanged", "sv" },
	{ },
};

static void append_connections(DBusMessageIter *entry)
{
	DBusMessageIter value, iter;
	const char *key = "Connections";

	dbus_message_iter_append_basic(entry, DBUS_TYPE_STRING, &key);

	dbus_message_iter_open_container(entry, DBUS_TYPE_VARIANT,
		DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_OBJECT_PATH_AS_STRING,
								&value);

	dbus_message_iter_open_container(&value, DBUS_TYPE_ARRAY,
				DBUS_TYPE_OBJECT_PATH_AS_STRING, &iter);
	__connman_element_list(NULL, CONNMAN_ELEMENT_TYPE_CONNECTION, &iter);
	dbus_message_iter_close_container(&value, &iter);

	dbus_message_iter_close_container(entry, &value);
}

static void emit_connections_signal(void)
{
	DBusMessage *signal;
	DBusMessageIter entry;

	signal = dbus_message_new_signal(CONNMAN_MANAGER_PATH,
				CONNMAN_MANAGER_INTERFACE, "PropertyChanged");
	if (signal == NULL)
		return;

	dbus_message_iter_init_append(signal, &entry);

	append_connections(&entry);

	g_dbus_send_message(connection, signal);
}

static int register_interface(struct connman_element *element)
{
	DBG("element %p name %s", element, element->name);

	if (g_dbus_register_interface(connection, element->path,
					CONNMAN_CONNECTION_INTERFACE,
					connection_methods, connection_signals,
					NULL, element, NULL) == FALSE) {
		connman_error("Failed to register %s connection", element->path);
		return -EIO;
	}

	emit_connections_signal();

	return 0;
}

static void unregister_interface(struct connman_element *element)
{
	DBG("element %p name %s", element, element->name);

	emit_connections_signal();

	g_dbus_unregister_interface(connection, element->path,
						CONNMAN_CONNECTION_INTERFACE);
}

static int connection_probe(struct connman_element *element)
{
	struct connman_service *service;
	const char *gateway = NULL;

	DBG("element %p name %s", element, element->name);

	if (element->parent == NULL)
		return -ENODEV;

	if (element->parent->type != CONNMAN_ELEMENT_TYPE_IPV4)
		return -ENODEV;

	connman_element_get_value(element,
				CONNMAN_PROPERTY_ID_IPV4_GATEWAY, &gateway);

	DBG("gateway %s", gateway);

	if (register_interface(element) < 0)
		return -ENODEV;

	service = __connman_element_get_service(element);
	__connman_service_indicate_state(service,
					CONNMAN_SERVICE_STATE_READY);

	if (gateway == NULL)
		return 0;

	if (find_gateway(element->index, gateway) != NULL) {
		DBG("previous gateway still present");
		goto done;
	}

	if (g_slist_length(gateway_list) > 0) {
		DBG("default gateway already present");
		return 0;
	}

	if (set_route(element, gateway) < 0)
		return 0;

done:
	service = __connman_element_get_service(element);
	__connman_service_indicate_default(service);

	connman_element_set_enabled(element, TRUE);
	emit_default_signal(element);

	return 0;
}

static void connection_remove(struct connman_element *element)
{
	struct connman_service *service;
	const char *gateway = NULL;

	DBG("element %p name %s", element, element->name);

	service = __connman_element_get_service(element);
	__connman_service_indicate_state(service,
					CONNMAN_SERVICE_STATE_DISCONNECT);

	unregister_interface(element);

	connman_element_get_value(element,
				CONNMAN_PROPERTY_ID_IPV4_GATEWAY, &gateway);

	DBG("gateway %s", gateway);

	if (gateway == NULL)
		return;

	remove_gateway(element->index, gateway);

	connman_element_set_enabled(element, FALSE);
	emit_default_signal(element);

	del_route(element, gateway);
}

static struct connman_driver connection_driver = {
	.name		= "connection",
	.type		= CONNMAN_ELEMENT_TYPE_CONNECTION,
	.priority	= CONNMAN_DRIVER_PRIORITY_LOW,
	.probe		= connection_probe,
	.remove		= connection_remove,
};

int __connman_connection_init(void)
{
	DBG("");

	connection = connman_dbus_get_connection();

	if (connman_rtnl_register(&connection_rtnl) < 0)
		connman_error("Failed to setup RTNL gateway driver");

	connman_rtnl_send_getroute();

	return connman_driver_register(&connection_driver);
}

void __connman_connection_cleanup(void)
{
	GSList *list;

	DBG("");

	connman_driver_unregister(&connection_driver);

	connman_rtnl_unregister(&connection_rtnl);

	for (list = gateway_list; list; list = list->next) {
		struct gateway_data *data = list->data;

		DBG("index %d gateway %s", data->index, data->gateway);

		g_free(data->gateway);
		g_free(data);
		list->data = NULL;
	}

	g_slist_free(gateway_list);
	gateway_list = NULL;

	dbus_connection_unref(connection);
}
