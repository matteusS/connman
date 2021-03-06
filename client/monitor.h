/*
 *
 *  Connection Manager
 *
 *  Copyright (C) 2012  Intel Corporation. All rights reserved.
 *
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

#ifndef __CLIENT_MONITOR_H
#define __CLIENT_MONITOR_H

#include <dbus/dbus.h>


int monitor_connman(DBusConnection *connection, char *interface,
				char *signal_name);

DBusHandlerResult service_property_changed(DBusConnection *connection,
				DBusMessage *message, void *user_data);

DBusHandlerResult tech_property_changed(DBusConnection *connection,
				DBusMessage *message, void *user_data);

DBusHandlerResult tech_added_removed(DBusConnection *connection,
				DBusMessage *message, void *user_data);

DBusHandlerResult manager_property_changed(DBusConnection *connection,
				DBusMessage *message, void *user_data);

DBusHandlerResult manager_services_changed(DBusConnection *connection,
				DBusMessage *message, void *user_data);
#endif
