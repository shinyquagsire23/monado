// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  BLE implementation based on Linux Bluez/dbus.
 * @author Pete Black <pete.black@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_os
 */

#include "os_ble.h"

#include "util/u_misc.h"
#include "util/u_logging.h"

#include <poll.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <dbus/dbus.h>


/*
 *
 * Defines.
 *
 */

#define I(bch, ...) U_LOG_I(__VA_ARGS__)
#define E(bch, ...) U_LOG_E(__VA_ARGS__)


/*
 *
 * Structs.
 *
 */

/*!
 * Small helper that keeps track of a connection and a error.
 */
struct ble_conn_helper
{
	DBusConnection *conn;
	DBusError err;
};

/*!
 * An implementation of @ref os_ble_device using a DBus connection to BlueZ.
 * @implements os_ble_device
 */
struct ble_notify
{
	struct os_ble_device base;
	struct ble_conn_helper bch;
	int fd;
};


/*
 *
 * Send helpers.
 *
 */

static void
add_single_byte_array(DBusMessage *msg, uint8_t value)
{
	// Create an array of bytes.
	const char *container_signature = "y"; // dbus type signature string
	DBusMessageIter iter, array;

	// attach it to our dbus message
	dbus_message_iter_init_append(msg, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, container_signature, &array);
	dbus_message_iter_append_basic(&array, DBUS_TYPE_BYTE, &value);
	dbus_message_iter_close_container(&iter, &array);
}

static void
add_empty_dict_sv(DBusMessage *msg)
{
	// Create an empty array of string variant dicts.
	const char *container_signature = "{sv}"; // dbus type signature string
	DBusMessageIter iter, options;

	// attach it to our dbus message
	dbus_message_iter_init_append(msg, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, container_signature, &options);
	dbus_message_iter_close_container(&iter, &options);
}

static int
send_message(DBusConnection *conn, DBusError *err, DBusMessage **msg_ptr)
{
	DBusPendingCall *pending;

	// Take the message and null it.
	DBusMessage *msg = *msg_ptr;
	*msg_ptr = NULL;

	if (msg == NULL) {
		U_LOG_E("Message Null after construction\n");
		return -1;
	}

	// send message and get a handle for a reply
	if (!dbus_connection_send_with_reply(conn, msg, &pending, -1)) {
		// -1 is default timeout
		U_LOG_E("Out Of Memory!\n");
		return -1;
	}
	if (pending == NULL) {
		U_LOG_E("Pending Call Null\n");
		return -1;
	}
	dbus_connection_flush(conn);

	// Unref the message.
	dbus_message_unref(msg);
	msg = NULL;

	// block until we receive a reply
	dbus_pending_call_block(pending);

	// get the reply message
	msg = dbus_pending_call_steal_reply(pending);

	// free the pending message handle
	dbus_pending_call_unref(pending);
	pending = NULL;

	if (msg == NULL) {
		U_LOG_E("Reply Null\n");
		return -1;
	}

	*msg_ptr = msg;
	return 0;
}


/*
 *
 * Dump functions
 *
 */

static void
dump_recurse(DBusMessageIter *parent, DBusMessageIter *sub, int level);

static int
dump_one_element(DBusMessageIter *element, int level)
{
	int type = dbus_message_iter_get_arg_type(element);
	char *str;

	switch (type) {
	case DBUS_TYPE_INVALID: {
		U_LOG_E("%*s<>", level, "");
		return -1;
	}
	case DBUS_TYPE_BOOLEAN: {
		int val;
		dbus_message_iter_get_basic(element, &val);
		U_LOG_D("%*sBOOLEAN: %s", level, "", val == 0 ? "false" : "true");
		return 0;
	}
	case DBUS_TYPE_BYTE: {
		int8_t val;
		dbus_message_iter_get_basic(element, &val);
		U_LOG_D("%*sBYTE: %02x", level, "", val);
		return 0;
	}
	case DBUS_TYPE_INT32: {
		int32_t val;
		dbus_message_iter_get_basic(element, &val);
		U_LOG_D("%*sINT32: %" PRIi32, level, "", val);
		return 0;
	}
	case DBUS_TYPE_UINT32: {
		uint32_t val;
		dbus_message_iter_get_basic(element, &val);
		U_LOG_D("%*sUINT32: %" PRIu32, level, "", val);
		return 0;
	}
	case DBUS_TYPE_INT64: {
		int64_t val;
		dbus_message_iter_get_basic(element, &val);
		U_LOG_D("%*sINT64: %" PRIi64, level, "", val);
		return 0;
	}
	case DBUS_TYPE_UINT64: {
		uint64_t val;
		dbus_message_iter_get_basic(element, &val);
		U_LOG_D("%*sUINT32: %" PRIu64, level, "", val);
		return 0;
	}
	case DBUS_TYPE_STRING: {
		dbus_message_iter_get_basic(element, &str);
		U_LOG_D("%*sSTRING: %s", level, "", str);
		return 0;
	}
	case DBUS_TYPE_OBJECT_PATH: {
		dbus_message_iter_get_basic(element, &str);
		U_LOG_D("%*sOBJECT_PATH: %s", level, "", str);
		return 0;
	}
	case DBUS_TYPE_ARRAY: {
		int elm_type = dbus_message_iter_get_element_type(element);
		int elm_count = dbus_message_iter_get_element_count(element);
		U_LOG_D("%*sARRAY: %c:%i", level, "", elm_type, elm_count);
		DBusMessageIter sub;
		dbus_message_iter_recurse(element, &sub);
		dump_recurse(element, &sub, level + 2);
		return 0;
	}
	case DBUS_TYPE_VARIANT: {
		DBusMessageIter var;
		dbus_message_iter_recurse(element, &var);
		int var_type = dbus_message_iter_get_arg_type(&var);
		U_LOG_D("%*sVARIANT: %c", level, "", var_type);
		dump_one_element(&var, level + 2);
		return 0;
	}
	case DBUS_TYPE_DICT_ENTRY: {
		U_LOG_D("%*sDICT", level, "");
		DBusMessageIter sub;
		dbus_message_iter_recurse(element, &sub);
		dump_recurse(element, &sub, level + 2);
		return 0;
	}
	default:
		U_LOG_D("%*sGot! %c", level, "", type); // line break
		return 0;
	}
}

static void
dump_recurse(DBusMessageIter *parent, DBusMessageIter *sub, int level)
{
	while (true) {
		if (dump_one_element(sub, level) < 0) {
			return;
		}
		dbus_message_iter_next(sub);
	}
}


/*
 *
 * DBus iterator helper functions.
 *
 */

/*!
 * Checks if a string starts with, has extra slash and room for more.
 */
static bool
starts_with_and_has_slash(const char *str, const char *beginning)
{
	size_t str_len = strlen(str);
	size_t beginning_len = strlen(beginning);

	if (str_len <= beginning_len + 1) {
		return false;
	}

	size_t i = 0;
	for (; i < beginning_len; i++) {
		if (str[i] != beginning[i]) {
			return false;
		}
	}

	if (str[i] != '/') {
		return false;
	}

	return true;
}

static int
dict_get_string_and_varient_child(DBusMessageIter *dict, const char **out_str, DBusMessageIter *out_child)
{
	DBusMessageIter child;
	int type = dbus_message_iter_get_arg_type(dict);
	if (type != DBUS_TYPE_DICT_ENTRY) {
		U_LOG_E("Expected dict got '%c'!\n", type);
		return -1;
	}

	dbus_message_iter_recurse(dict, &child);
	type = dbus_message_iter_get_arg_type(&child);
	if (type != DBUS_TYPE_STRING && type != DBUS_TYPE_OBJECT_PATH) {
		U_LOG_E(
		    "Expected dict first thing to be string or object path, "
		    "got '%c'\n",
		    type);
		return -1;
	}

	dbus_message_iter_get_basic(&child, out_str);
	dbus_message_iter_next(&child);

	type = dbus_message_iter_get_arg_type(&child);
	if (type != DBUS_TYPE_VARIANT) {
		U_LOG_E("Expected variant got '%c'\n", type);
		return -1;
	}

	dbus_message_iter_recurse(&child, out_child);

	return 0;
}

static int
dict_get_string_and_array_elm(const DBusMessageIter *in_dict, const char **out_str, DBusMessageIter *out_array_elm)
{
	DBusMessageIter dict = *in_dict;
	DBusMessageIter child;
	int type = dbus_message_iter_get_arg_type(&dict);
	if (type != DBUS_TYPE_DICT_ENTRY) {
		U_LOG_E("Expected dict got '%c'!\n", type);
		return -1;
	}

	dbus_message_iter_recurse(&dict, &child);
	type = dbus_message_iter_get_arg_type(&child);
	if (type != DBUS_TYPE_STRING && type != DBUS_TYPE_OBJECT_PATH) {
		U_LOG_E(
		    "Expected dict first thing to be string or object path, "
		    "got '%c'\n",
		    type);
		return -1;
	}

	dbus_message_iter_get_basic(&child, out_str);
	dbus_message_iter_next(&child);

	type = dbus_message_iter_get_arg_type(&child);
	if (type != DBUS_TYPE_ARRAY) {
		U_LOG_E("Expected array got '%c'\n", type);
		return -1;
	}

	dbus_message_iter_recurse(&child, out_array_elm);

	return 0;
}

#define for_each(i, first)                                                                                             \
	for (DBusMessageIter i = first; dbus_message_iter_get_arg_type(&i) != DBUS_TYPE_INVALID;                       \
	     dbus_message_iter_next(&i))

/*!
 * Ensures that the @p parent is a array and has a element type the given type,
 * outputs the first element of the array on success.
 */
static int
array_get_first_elem_of_type(const DBusMessageIter *in_parent, int of_type, DBusMessageIter *out_elm)
{
	DBusMessageIter parent = *in_parent;
	int type = dbus_message_iter_get_arg_type(&parent);
	if (type != DBUS_TYPE_ARRAY) {
		U_LOG_E("Expected array got '%c'!\n", type);
		return -1;
	}

	DBusMessageIter elm;
	dbus_message_iter_recurse(&parent, &elm);

	type = dbus_message_iter_get_arg_type(&elm);
	if (type != of_type) {
		U_LOG_E("Expected elem type of '%c' got '%c'!\n", of_type, type);
		return -1;
	}

	*out_elm = elm;

	return 1;
}

/*!
 * Given a the first element in a array of dict, loop over them and check if
 * the key matches it's string value. Returns positive if a match is found,
 * zero if not found and negative on failure. The argument @p out_value holds
 * the value of the dict pair.
 */
static int
array_find_variant_value(const DBusMessageIter *first_elm, const char *key, DBusMessageIter *out_value)
{
	const char *str;

	for_each(elm, *first_elm)
	{
		dict_get_string_and_varient_child(&elm, &str, out_value);

		if (strcmp(key, str) == 0) {
			return 1;
		}
	}

	return 0;
}

/*!
 * Given a array which elements are of type string, loop over them and check if
 * any of them matches the given @p key. Returns positive if a match is found,
 * zero if not found and negative on failure.
 */
static int
array_match_string_element(const DBusMessageIter *in_array, const char *key)
{
	DBusMessageIter array = *in_array;
	int type = dbus_message_iter_get_arg_type(&array);
	if (type != DBUS_TYPE_ARRAY) {
		U_LOG_E("Expected array type ('%c')\n", type);
		return -1;
	}

	int elm_type = dbus_message_iter_get_element_type(&array);
	if (elm_type != DBUS_TYPE_STRING) {
		U_LOG_E("Expected string element type ('%c')\n", type);
		return -1;
	}

	DBusMessageIter first_elm;
	dbus_message_iter_recurse(&array, &first_elm);

	for_each(elm, first_elm)
	{
		const char *str = NULL;
		dbus_message_iter_get_basic(&elm, &str);

		if (strcmp(key, str) == 0) {
			return 1;
		}
	}

	return 0;
}


/*
 *
 * D-BUS helpers.
 *
 */

static int
dbus_has_name(DBusConnection *conn, const char *name)
{
	DBusMessage *msg;
	DBusError err;

	msg = dbus_message_new_method_call("org.freedesktop.DBus",  // target for the method call
	                                   "/org/freedesktop/DBus", // object to call on
	                                   "org.freedesktop.DBus",  // interface to call on
	                                   "ListNames");            // method name
	if (send_message(conn, &err, &msg) != 0) {
		return -1;
	}

	DBusMessageIter args;
	dbus_message_iter_init(msg, &args);

	// Check if this is a error message.
	int type = dbus_message_iter_get_arg_type(&args);
	if (type == DBUS_TYPE_STRING) {
		char *response = NULL;
		dbus_message_iter_get_basic(&args, &response);
		U_LOG_E("Error getting calling ListNames:\n%s\n", response);
		response = NULL;

		// free reply
		dbus_message_unref(msg);
		return -1;
	}

	DBusMessageIter first_elm;
	int ret = array_get_first_elem_of_type(&args, DBUS_TYPE_STRING, &first_elm);
	if (ret < 0) {
		// free reply
		dbus_message_unref(msg);
		return -1;
	}

	for_each(elm, first_elm)
	{
		char *response = NULL;
		dbus_message_iter_get_basic(&elm, &response);
		if (strcmp(response, name) == 0) {
			// free reply
			dbus_message_unref(msg);
			return 0;
		}
	}

	// free reply
	dbus_message_unref(msg);
	return -1;
}


/*
 *
 * Bluez helpers iterator helpers.
 *
 */

/*!
 * Returns true if the object implements the `org.bluez.Device1` interface,
 * and one of it's `UUIDs` matches the given @p uuid.
 */
static int
device_has_uuid(const DBusMessageIter *dict, const char *uuid, const char **out_path_str)
{
	DBusMessageIter iface_elm, first_elm;
	const char *iface_str;
	const char *path_str;

	int ret = dict_get_string_and_array_elm(dict, &path_str, &first_elm);
	if (ret < 0) {
		return ret;
	}

	for_each(elm, first_elm)
	{
		dict_get_string_and_array_elm(&elm, &iface_str, &iface_elm);

		if (strcmp(iface_str, "org.bluez.Device1") != 0) {
			continue;
		}

		DBusMessageIter value;
		int ret = array_find_variant_value(&iface_elm, "UUIDs", &value);
		if (ret <= 0) {
			continue;
		}

		ret = array_match_string_element(&value, uuid);
		if (ret <= 0) {
			continue;
		}

		*out_path_str = path_str;
		return 1;
	}

	return 0;
}


/*!
 * On a gatt interface object get it's Flags property and check if notify is
 * set, returns positive if it found that Flags property, zero on not finding it
 * and negative on error.
 */
static int
gatt_iface_get_flag_notifiable(const DBusMessageIter *iface_elm, bool *out_bool)
{
	DBusMessageIter value;
	int ret = array_find_variant_value(iface_elm, "Flags", &value);
	if (ret <= 0) {
		return ret;
	}

	ret = array_match_string_element(&value, "notify");
	if (ret < 0) {
		// Error
		return ret;
	}
	if (ret > 0) {
		// Found the notify field!
		*out_bool = true;
	}

	// We found the Flags field.
	return 1;
}

/*!
 * On a gatt interface object get it's UUID string property, returns positive
 * if found, zero on not finding it and negative on error.
 */
static int
gatt_iface_get_uuid(const DBusMessageIter *iface_elm, const char **out_str)
{
	DBusMessageIter value;
	int ret = array_find_variant_value(iface_elm, "UUID", &value);
	if (ret <= 0) {
		return ret;
	}

	int type = dbus_message_iter_get_arg_type(&value);
	if (type != DBUS_TYPE_STRING) {
		U_LOG_E("Invalid UUID value type ('%c')\n", type);
		return -1;
	}

	dbus_message_iter_get_basic(&value, out_str);
	return 1;
}

/*!
 * Returns positive value if the object implements the
 * `org.bluez.GattCharacteristic1` interface, it's `UUID` matches the given @p
 * uuid and has the notify flag set.
 */
static int
gatt_char_has_uuid_and_notify(const DBusMessageIter *dict, const char *uuid, const char **out_path_str)
{
	DBusMessageIter first_elm, iface_elm;
	const char *iface_str;
	const char *path_str;
	const char *uuid_str;

	int ret = dict_get_string_and_array_elm(dict, &path_str, &first_elm);
	if (ret < 0) {
		return ret;
	}

	for_each(elm, first_elm)
	{
		dict_get_string_and_array_elm(&elm, &iface_str, &iface_elm);

		if (strcmp(iface_str, "org.bluez.GattCharacteristic1") != 0) {
			continue;
		}

		if (gatt_iface_get_uuid(&iface_elm, &uuid_str) <= 0) {
			continue;
		}

		if (strcmp(uuid_str, uuid) != 0) {
			continue;
		}

		bool notifable = false;
		ret = gatt_iface_get_flag_notifiable(&iface_elm, &notifable);
		if (ret <= 0 || !notifable) {
			continue;
		}

		*out_path_str = path_str;
		return 1;
	}

	return 0;
}


/*
 *
 * Bluez helpers..
 *
 */

static void
ble_close(struct ble_conn_helper *bch)
{
	if (bch->conn == NULL) {
		return;
	}

	dbus_error_free(&bch->err);
	dbus_connection_unref(bch->conn);
	bch->conn = NULL;
}

static int
ble_init(struct ble_conn_helper *bch)
{
	dbus_error_init(&bch->err);
	bch->conn = dbus_bus_get(DBUS_BUS_SYSTEM, &bch->err);
	if (dbus_error_is_set(&bch->err)) {
		E(bch, "DBUS Connection Error: %s\n", bch->err.message);
		dbus_error_free(&bch->err);
	}
	if (bch->conn == NULL) {
		return -1;
	}

	// Check if org.bluez is running.
	int ret = dbus_has_name(bch->conn, "org.bluez");
	if (ret != 0) {
		ble_close(bch);
		return -1;
	}

	return 0;
}

static int
ble_dbus_send(struct ble_conn_helper *bch, DBusMessage **out_msg)
{
	return send_message(bch->conn, &bch->err, out_msg);
}

static int
ble_get_managed_objects(struct ble_conn_helper *bch, DBusMessage **out_msg)
{
	DBusMessageIter args;
	DBusMessage *msg;
	int type;

	msg = dbus_message_new_method_call("org.bluez",                          // target for the method call
	                                   "/",                                  // object to call on
	                                   "org.freedesktop.DBus.ObjectManager", // interface to call on
	                                   "GetManagedObjects");                 // method name
	if (msg == NULL) {
		E(bch, "Could not create new message!");
		return -1;
	}

	int ret = ble_dbus_send(bch, &msg);
	if (ret != 0) {
		E(bch, "Could send message '%i'!", ret);
		dbus_message_unref(msg);
		return ret;
	}

	dbus_message_iter_init(msg, &args);

	// Check if this is a error message.
	type = dbus_message_iter_get_arg_type(&args);
	if (type == DBUS_TYPE_STRING) {
		char *response = NULL;
		dbus_message_iter_get_basic(&args, &response);
		E(bch, "Error getting objects:\n%s\n", response);
		response = NULL;

		// free reply
		dbus_message_unref(msg);
		return -1;
	}

	// Message is verified.
	*out_msg = msg;

	return 0;
}

static int
ble_connect(struct ble_conn_helper *bch, const char *dbus_address)
{
	DBusMessage *msg = NULL;
	DBusMessageIter args;
	char *response = NULL;
	int ret, type;

	I(bch, "Connecting '%s'", dbus_address);

	msg = dbus_message_new_method_call("org.bluez",         // target for the method call
	                                   dbus_address,        // object to call on
	                                   "org.bluez.Device1", // interface to call on
	                                   "Connect");          // method name
	if (msg == NULL) {
		E(bch, "Message NULL after construction\n");
		return -1;
	}

	// Send the message, consumes our message and returns what we received.
	ret = ble_dbus_send(bch, &msg);
	if (ret != 0) {
		E(bch, "Failed to send message '%i'\n", ret);
		return -1;
	}

	// Function returns nothing on success, check for error message.
	dbus_message_iter_init(msg, &args);
	type = dbus_message_iter_get_arg_type(&args);
	if (type == DBUS_TYPE_STRING) {
		dbus_message_iter_get_basic(&args, &response);
		E(bch, "DBus call returned message: %s\n", response);

		// Free reply.
		dbus_message_unref(msg);
		return -1;
	}

	// Free reply
	dbus_message_unref(msg);

	return 0;
}

static int
ble_connect_all_devices_with_service_uuid(struct ble_conn_helper *bch, const char *service_uuid)
{
	DBusMessageIter args, first_elm;
	DBusMessage *msg = NULL;

	int ret = ble_get_managed_objects(bch, &msg);
	if (ret != 0) {
		return ret;
	}

	dbus_message_iter_init(msg, &args);
	ret = array_get_first_elem_of_type(&args, DBUS_TYPE_DICT_ENTRY, &first_elm);
	if (ret < 0) {
		// free reply
		dbus_message_unref(msg);
		return -1;
	}

	for_each(elm, first_elm)
	{
		const char *dev_path_str;
		ret = device_has_uuid(&elm, service_uuid, &dev_path_str);
		if (ret <= 0) {
			continue;
		}

		ble_connect(bch, dev_path_str);
	}

	// free reply
	dbus_message_unref(msg);

	return 0;
}

static int
ble_write_value(struct ble_conn_helper *bch, const char *dbus_address, uint8_t value)
{
	DBusMessage *msg = NULL;
	DBusMessageIter args;
	char *response = NULL;
	int ret, type;

	msg = dbus_message_new_method_call("org.bluez",                     // target for the method call
	                                   dbus_address,                    // object to call on
	                                   "org.bluez.GattCharacteristic1", // interface to call on
	                                   "WriteValue");                   // method name
	if (msg == NULL) {
		E(bch, "Message NULL after construction\n");
		return -1;
	}

	// Write has a argument of Array of Bytes, Array of Dicts.
	add_single_byte_array(msg, value);
	add_empty_dict_sv(msg);

	// Send the message, consumes our message and returns what we received.
	ret = ble_dbus_send(bch, &msg);
	if (ret != 0) {
		E(bch, "Failed to send message '%i'\n", ret);
		return -1;
	}

	// Function returns nothing on success, check for error message.
	dbus_message_iter_init(msg, &args);
	type = dbus_message_iter_get_arg_type(&args);
	if (type == DBUS_TYPE_STRING) {
		dbus_message_iter_get_basic(&args, &response);
		E(bch, "DBus call returned message: %s\n", response);

		// Free reply.
		dbus_message_unref(msg);
		return -1;
	}

	// Free reply
	dbus_message_unref(msg);

	return 0;
}

static ssize_t
get_path_to_notify_char(
    struct ble_conn_helper *bch, const char *dev_uuid, const char *char_uuid, char *output, size_t output_len)
{
	DBusMessageIter args, first_elm;
	DBusMessage *msg;

	int ret = ble_get_managed_objects(bch, &msg);
	if (ret != 0) {
		return ret;
	}

	dbus_message_iter_init(msg, &args);
	ret = array_get_first_elem_of_type(&args, DBUS_TYPE_DICT_ENTRY, &first_elm);
	if (ret < 0) {
		// free reply
		dbus_message_unref(msg);
		return -1;
	}

	for_each(elm, first_elm)
	{
		const char *dev_path_str;
		const char *char_path_str;
		ret = device_has_uuid(&elm, dev_uuid, &dev_path_str);
		if (ret <= 0) {
			continue;
		}

		for_each(c, first_elm)
		{
			ret = gatt_char_has_uuid_and_notify(&c, char_uuid, &char_path_str);
			if (ret <= 0) {
				continue;
			}
			if (!starts_with_and_has_slash(char_path_str, dev_path_str)) {
				continue;
			}

			ssize_t written = snprintf(output, output_len, "%s", char_path_str);

			// free reply
			dbus_message_unref(msg);

			return written;
		}
	}

	// free reply
	dbus_message_unref(msg);

	return 0;
}

static int
init_ble_notify(const char *dev_uuid, const char *char_uuid, struct ble_notify *bledev)
{
	DBusMessage *msg;
	int ret;

	ret = ble_init(&bledev->bch);
	if (ret != 0) {
		return ret;
	}

	char dbus_address[256]; // should be long enough
	XRT_MAYBE_UNUSED ssize_t written =
	    get_path_to_notify_char(&bledev->bch, dev_uuid, char_uuid, dbus_address, sizeof(dbus_address));
	if (written == 0) {
		return -1;
	}
	if (written < 0) {
		return -1;
	}

	msg = dbus_message_new_method_call("org.bluez",                     // target for the method call
	                                   dbus_address,                    // object to call on
	                                   "org.bluez.GattCharacteristic1", // interface to call on
	                                   "AcquireNotify");                // method name
	if (msg == NULL) {
		E(&bledev->bch, "Message Null after construction\n");
		return -1;
	}

	// AcquireNotify has a argument of Array of Dicts.
	add_empty_dict_sv(msg);

	// Send the message, consumes our message and returns what we received.
	if (ble_dbus_send(&bledev->bch, &msg) != 0) {
		return -1;
	}

	DBusMessageIter args;
	char *response = NULL;
	dbus_message_iter_init(msg, &args);
	while (true) {
		int type = dbus_message_iter_get_arg_type(&args);
		if (type == DBUS_TYPE_INVALID) {
			break;
		}
		if (type == DBUS_TYPE_STRING) {
			dbus_message_iter_get_basic(&args, &response);
			E(&bledev->bch, "DBus call returned message: %s\n", response);
		} else if (type == DBUS_TYPE_UNIX_FD) {
			dbus_message_iter_get_basic(&args, &bledev->fd);
		}
		dbus_message_iter_next(&args);
	}

	// free reply
	dbus_message_unref(msg);

	// We didn't get a fd.
	if (bledev->fd == -1) {
		return -1;
	}

	return 0;
}


/*
 *
 * BLE notify object implementation.
 *
 */

static int
os_ble_notify_read(struct os_ble_device *bdev, uint8_t *data, size_t length, int milliseconds)
{
	struct ble_notify *dev = (struct ble_notify *)bdev;
	struct pollfd fds;
	int ret;

	if (milliseconds >= 0) {
		fds.fd = dev->fd;
		fds.events = POLLIN;
		fds.revents = 0;
		ret = poll(&fds, 1, milliseconds);

		if (ret == -1 || ret == 0) {
			// Error or timeout.
			return ret;
		}
		if (fds.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			// Device disconnect?
			return -1;
		}
	}

	ret = read(dev->fd, data, length);

	if (ret < 0 && (errno == EAGAIN || errno == EINPROGRESS)) {
		// Process most likely received a signal.
		ret = 0;
	}

	return ret;
}

static void
os_ble_notify_destroy(struct os_ble_device *bdev)
{
	struct ble_notify *dev = (struct ble_notify *)bdev;

	if (dev->fd != -1) {
		close(dev->fd);
		dev->fd = -1;
	}

	ble_close(&dev->bch);

	free(dev);
}


/*
 *
 * 'Exported' functions.
 *
 */

int
os_ble_notify_open(const char *dev_uuid, const char *char_uuid, struct os_ble_device **out_ble)
{
	struct ble_notify *bledev = U_TYPED_CALLOC(struct ble_notify);
	bledev->base.read = os_ble_notify_read;
	bledev->base.destroy = os_ble_notify_destroy;
	bledev->fd = -1;

	int ret = init_ble_notify(dev_uuid, char_uuid, bledev);
	if (ret != 0) {
		os_ble_notify_destroy(&bledev->base);
		return -1;
	}

	*out_ble = &bledev->base;

	return 1;
}

int
os_ble_broadcast_write_value(const char *service_uuid, const char *char_uuid, uint8_t value)
{
	struct ble_conn_helper bch = {0};
	DBusMessageIter args, first_elm;
	DBusMessage *msg = NULL;
	int ret = 0;


	/*
	 * Init dbus
	 */

	ret = ble_init(&bch);
	if (ret != 0) {
		return ret;
	}

	/*
	 * Connect devices
	 */

	// Connect all of the devices so we can write to them.
	ble_connect_all_devices_with_service_uuid(&bch, service_uuid);


	/*
	 * Write to all connected devices.
	 */

	/*
	 * We get the objects again, because their services and characteristics
	 * might not have been created before.
	 */
	ret = ble_get_managed_objects(&bch, &msg);
	if (ret != 0) {
		ble_close(&bch);
		return ret;
	}

	dbus_message_iter_init(msg, &args);
	ret = array_get_first_elem_of_type(&args, DBUS_TYPE_DICT_ENTRY, &first_elm);
	if (ret < 0) {
		// free reply
		dbus_message_unref(msg);
		ble_close(&bch);
		return -1;
	}

	for_each(elm, first_elm)
	{
		const char *dev_path_str;
		const char *char_path_str;
		ret = device_has_uuid(&elm, service_uuid, &dev_path_str);
		if (ret <= 0) {
			continue;
		}

		for_each(c, first_elm)
		{
			ret = gatt_char_has_uuid_and_notify(&c, char_uuid, &char_path_str);
			if (ret <= 0) {
				continue;
			}
			if (!starts_with_and_has_slash(char_path_str, dev_path_str)) {
				continue;
			}

			ble_write_value(&bch, char_path_str, value);
		}
	}

	// free reply
	dbus_message_unref(msg);
	ble_close(&bch);

	return 0;
}
