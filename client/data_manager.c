/*
 *
 *  Connection Manager
 *
 *  Copyright (C) 2012  Intel Corporation. All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <glib.h>

#include "src/connman.h"
#include "client/services.h"
#include "client/technology.h"
#include "client/data_manager.h"
#include "gdbus/gdbus.h"
#include "include/dbus.h"

static void extract_manager_properties(DBusMessage *message)
{
	DBusMessageIter iter, array;

	dbus_message_iter_init(message, &iter);
	dbus_message_iter_recurse(&iter, &array);
	extract_properties(&array);
}

int store_proxy_input(DBusConnection *connection, DBusMessage *message,
				char *name, int num_args, char *argv[])
{
	int i, j, k;
	int error = 0;
	gchar **servers = NULL;
	gchar **excludes = NULL;

	for (i = 0; strcmp(argv[i], "excludes") != 0; i++) {
		servers = g_try_realloc(servers, (i + 1) * sizeof(char *));
		if (servers == NULL) {
			fprintf(stderr, "Could not allocate memory for list\n");
			return -ENOMEM;
		}
		servers[i] = g_strdup(argv[i]);
		/* In case the user doesn't enter "excludes" */
		if (i + 1 == num_args) {
			i++;
			j = 0;
			goto free_servers;
		}
	}
	for (j = 0; j + (i + 1) != num_args; j++) {
		excludes = g_try_realloc(excludes, (j + 1) * sizeof(char *));
		if (excludes == NULL) {
			fprintf(stderr, "Could not allocate memory for list\n");
			return -ENOMEM;
		}
		excludes[j] = g_strdup(argv[j + (i + 1)]);
	}

free_servers:
	error = set_proxy_manual(connection, message, name, servers, excludes,
									i, j);

	for (k = 0; k < j - 1; k++)
		g_free(excludes[k]);
	g_free(excludes);

	for (k = 0; k < i - 1; k++)
		g_free(servers[k]);
	g_free(servers);

	return error;
}

int connect_service(DBusConnection *connection, char *name)
{
	DBusMessage *message, *message_connect = NULL;
	struct service_data service;
	char *path = NULL;
	const char *path_name;
	DBusError err;
	int err_ret = 0;

	message = get_message(connection, "GetServices");
	if (message == NULL)
		return -ENOMEM;

	path_name = find_service(connection, message, name, &service);
	if (path_name == NULL) {
		err_ret = -ENXIO;
		goto error;
	}

	path = g_strdup_printf("/net/connman/service/%s", path_name);
	message_connect = dbus_message_new_method_call("net.connman", path,
						"net.connman.Service",
						"Connect");
	if (message_connect == NULL) {
		err_ret = -ENOMEM;
		goto error;
	}

	dbus_error_init(&err);
	dbus_connection_send_with_reply_and_block(connection, message_connect,
								-1, &err);

	if (dbus_error_is_set(&err)) {
		printf("Connection failed; error: '%s'\n", err.message);
		err_ret = -EINVAL;
		goto error;
	}

	dbus_connection_send(connection, message_connect, NULL);
	dbus_connection_flush(connection);

error:
	if (message != NULL)
		dbus_message_unref(message);
	if (message_connect != NULL)
		dbus_message_unref(message_connect);
	g_free(path);

	return err_ret;
}

int disconnect_service(DBusConnection *connection, char *name)
{
	DBusMessage *message, *message_disconnect = NULL;
	struct service_data service;
	char *path = NULL;
	const char *path_name;
	DBusError err;
	int err_ret = 0;

	message = get_message(connection, "GetServices");
	if (message == NULL)
		return -ENOMEM;

	path_name = find_service(connection, message, name, &service);
	if (path_name == NULL) {
		err_ret = -ENXIO;
		goto error;
	}

	path = g_strdup_printf("/net/connman/service/%s", path_name);
	printf("%s\n", path);
	message_disconnect = dbus_message_new_method_call("net.connman", path,
							  "net.connman.Service",
							  "Disconnect");
	if (message_disconnect == NULL) {
		err_ret = -ENOMEM;
		goto error;
	}

	dbus_error_init(&err);
	dbus_connection_send_with_reply_and_block(connection,
						  message_disconnect,
						  -1, &err);

	if (dbus_error_is_set(&err)) {
		printf("Connection failed; error: '%s'\n", err.message);
		err_ret = -EINVAL;
		goto error;
	}

	dbus_connection_send(connection, message_disconnect, NULL);
	dbus_connection_flush(connection);

error:
	if (message != NULL)
		dbus_message_unref(message);
	if (message_disconnect != NULL)
		dbus_message_unref(message_disconnect);
	g_free(path);

	return err_ret;
}

int set_manager(DBusConnection *connection, char *key, dbus_bool_t value)
{
	DBusMessage *message;
	DBusMessageIter iter;

	message = dbus_message_new_method_call("net.connman", "/",
						"net.connman.Manager",
						"SetProperty");
	if (message == NULL)
		return -ENOMEM;

	dbus_message_iter_init_append(message, &iter);
	connman_dbus_property_append_basic(&iter, (const char *) key,
						DBUS_TYPE_BOOLEAN, &value);
	dbus_connection_send(connection, message, NULL);
	dbus_connection_flush(connection);
	dbus_message_unref(message);

	return 0;
}

/* Call with any given function we want connman to respond to */
DBusMessage *get_message(DBusConnection *connection, char *function)
{
	DBusMessage *message, *reply;
	DBusError error;

	message = dbus_message_new_method_call(CONNMAN_SERVICE,
						CONNMAN_MANAGER_PATH,
						CONNMAN_MANAGER_INTERFACE,
						function);
	if (message == NULL)
		return NULL;

	dbus_error_init(&error);

	reply = dbus_connection_send_with_reply_and_block(connection,
							   message, -1, &error);
	if (dbus_error_is_set(&error) == TRUE) {
		fprintf(stderr, "%s\n", error.message);
		dbus_error_free(&error);
	} else if (reply == NULL)
		fprintf(stderr, "Failed to receive message\n");

	dbus_message_unref(message);

	return reply;
}

int list_properties(DBusConnection *connection, char *function,
			char *service_name)
{
	DBusMessage *message;

	message = get_message(connection, function);
	if (message == NULL)
		return -ENOMEM;

	if (strcmp(function, "GetProperties") == 0)
		extract_manager_properties(message);

	else if (strcmp(function, "GetServices") == 0 && service_name != NULL)
		extract_services(message, service_name);

	else if (strcmp(function, "GetServices") == 0 && service_name == NULL)
		get_services(message);

	else if (strcmp(function, "GetTechnologies") == 0)
		extract_tech(message);

	dbus_message_unref(message);

	return 0;
}
