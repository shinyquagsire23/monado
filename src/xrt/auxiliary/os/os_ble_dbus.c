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
 * Send helpers.
 *
 */

static void
add_empty_dict_sv(DBusMessage *msg)
{
	// Create an empty array of string variant dicts.
	const char *container_signature = "{sv}"; // dbus type signature string
	DBusMessageIter iter, options;

	// attach it to our dbus message
	dbus_message_iter_init_append(msg, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
	                                 container_signature, &options);
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
		fprintf(stderr, "Message Null after construction\n");
		return -1;
	}

	// send message and get a handle for a reply
	if (!dbus_connection_send_with_reply(conn, msg, &pending, -1)) {
		// -1 is default timeout
		fprintf(stderr, "Out Of Memory!\n");
		return -1;
	}
	if (pending == NULL) {
		fprintf(stderr, "Pending Call Null\n");
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
		fprintf(stderr, "Reply Null\n");
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

	for (int i = 0; i < level; i++) {
		fprintf(stderr, " ");
	}

	switch (type) {
	case DBUS_TYPE_INVALID: {
		fprintf(stderr, "<>\n");
		return -1;
	}
	case DBUS_TYPE_BOOLEAN: {
		int val;
		dbus_message_iter_get_basic(element, &val);
		fprintf(stderr, "BOOLEAN: %s\n", val == 0 ? "false" : "true");
		return 0;
	}
	case DBUS_TYPE_BYTE: {
		int8_t val;
		dbus_message_iter_get_basic(element, &val);
		fprintf(stderr, "BYTE: %02x\n", val);
		return 0;
	}
	case DBUS_TYPE_INT32: {
		int32_t val;
		dbus_message_iter_get_basic(element, &val);
		fprintf(stderr, "INT32: %" PRIi32 "\n", val);
		return 0;
	}
	case DBUS_TYPE_UINT32: {
		uint32_t val;
		dbus_message_iter_get_basic(element, &val);
		fprintf(stderr, "UINT32: %" PRIu32 "\n", val);
		return 0;
	}
	case DBUS_TYPE_INT64: {
		int64_t val;
		dbus_message_iter_get_basic(element, &val);
		fprintf(stderr, "INT64: %" PRIi64 "\n", val);
		return 0;
	}
	case DBUS_TYPE_UINT64: {
		uint64_t val;
		dbus_message_iter_get_basic(element, &val);
		fprintf(stderr, "UINT32: %" PRIu64 "\n", val);
		return 0;
	}
	case DBUS_TYPE_STRING: {
		dbus_message_iter_get_basic(element, &str);
		fprintf(stderr, "STRING: %s\n", str);
		return 0;
	}
	case DBUS_TYPE_OBJECT_PATH: {
		dbus_message_iter_get_basic(element, &str);
		fprintf(stderr, "OBJECT_PATH: %s\n", str);
		return 0;
	}
	case DBUS_TYPE_ARRAY: {
		int elm_type = dbus_message_iter_get_element_type(element);
		int elm_count = dbus_message_iter_get_element_count(element);
		fprintf(stderr, "ARRAY: %c:%i\n", elm_type, elm_count);
		DBusMessageIter sub;
		dbus_message_iter_recurse(element, &sub);
		dump_recurse(element, &sub, level + 2);
		return 0;
	}
	case DBUS_TYPE_VARIANT: {
		DBusMessageIter var;
		dbus_message_iter_recurse(element, &var);
		int var_type = dbus_message_iter_get_arg_type(&var);
		fprintf(stderr, "VARIANT: %c\n", var_type);
		dump_one_element(&var, level + 2);
		return 0;
	}
	case DBUS_TYPE_DICT_ENTRY: {
		fprintf(stderr, "DICT\n");
		DBusMessageIter sub;
		dbus_message_iter_recurse(element, &sub);
		dump_recurse(element, &sub, level + 2);
		return 0;
	}
	default:
		fprintf(stderr, "Got! %c\n", type); // line break
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
dict_get_string_and_varient_child(DBusMessageIter *dict,
                                  const char **out_str,
                                  DBusMessageIter *out_child)
{
	DBusMessageIter child;
	int type = dbus_message_iter_get_arg_type(dict);
	if (type != DBUS_TYPE_DICT_ENTRY) {
		fprintf(stderr, "Expected dict got '%c'!\n", type);
		return -1;
	}

	dbus_message_iter_recurse(dict, &child);
	type = dbus_message_iter_get_arg_type(&child);
	if (type != DBUS_TYPE_STRING && type != DBUS_TYPE_OBJECT_PATH) {
		fprintf(stderr,
		        "Expected dict first thing to be string or object "
		        "path, got '%c'\n",
		        type);
		return -1;
	}

	dbus_message_iter_get_basic(&child, out_str);
	dbus_message_iter_next(&child);

	type = dbus_message_iter_get_arg_type(&child);
	if (type != DBUS_TYPE_VARIANT) {
		fprintf(stderr, "Expected variant got '%c'\n", type);
		return -1;
	}

	dbus_message_iter_recurse(&child, out_child);

	return 0;
}

static int
dict_get_string_and_array_elm(const DBusMessageIter *in_dict,
                              const char **out_str,
                              DBusMessageIter *out_array_elm)
{
	DBusMessageIter dict = *in_dict;
	DBusMessageIter child;
	int type = dbus_message_iter_get_arg_type(&dict);
	if (type != DBUS_TYPE_DICT_ENTRY) {
		fprintf(stderr, "Expected dict got '%c'!\n", type);
		return -1;
	}

	dbus_message_iter_recurse(&dict, &child);
	type = dbus_message_iter_get_arg_type(&child);
	if (type != DBUS_TYPE_STRING && type != DBUS_TYPE_OBJECT_PATH) {
		fprintf(stderr,
		        "Expected dict first thing to be string or object "
		        "path, got '%c'\n",
		        type);
		return -1;
	}

	dbus_message_iter_get_basic(&child, out_str);
	dbus_message_iter_next(&child);

	type = dbus_message_iter_get_arg_type(&child);
	if (type != DBUS_TYPE_ARRAY) {
		fprintf(stderr, "Expected array got '%c'\n", type);
		return -1;
	}

	dbus_message_iter_recurse(&child, out_array_elm);

	return 0;
}

#define for_each(i, first)                                                     \
	for (DBusMessageIter i = first;                                        \
	     dbus_message_iter_get_arg_type(&i) != DBUS_TYPE_INVALID;          \
	     dbus_message_iter_next(&i))

/*!
 * Ensures that the @p parent is a array and has a element type the given type,
 * outputs the first element of the array on success.
 */
static int
array_get_first_elem_of_type(const DBusMessageIter *in_parent,
                             int of_type,
                             DBusMessageIter *out_elm)
{
	DBusMessageIter parent = *in_parent;
	int type = dbus_message_iter_get_arg_type(&parent);
	if (type != DBUS_TYPE_ARRAY) {
		fprintf(stderr, "Expected array got '%c'!\n", type);
		return -1;
	}

	DBusMessageIter elm;
	dbus_message_iter_recurse(&parent, &elm);

	type = dbus_message_iter_get_arg_type(&elm);
	if (type != of_type) {
		fprintf(stderr, "Expected elem type of '%c' got '%c'!\n",
		        of_type, type);
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
array_find_variant_value(const DBusMessageIter *first_elm,
                         const char *key,
                         DBusMessageIter *out_value)
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
		fprintf(stderr, "Expected array type ('%c')\n", type);
		return -1;
	}

	int elm_type = dbus_message_iter_get_element_type(&array);
	if (elm_type != DBUS_TYPE_STRING) {
		fprintf(stderr, "Expected string element type ('%c')\n", type);
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
 * Bluez helpers.
 *
 */

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
	} else if (ret > 0) {
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
		fprintf(stderr, "Invalid UUID value type ('%c')\n", type);
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
gatt_char_has_uuid_and_notify(const DBusMessageIter *dict,
                              const char *uuid,
                              const char **out_path_str)
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

/*!
 * Returns true if the object implements the `org.bluez.Device1` interface,
 * and one of it's `UUIDs` matches the given @p uuid.
 */
static int
device_has_uuid(const DBusMessageIter *dict,
                const char *uuid,
                const char **out_path_str)
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

static ssize_t
get_path_to_notify_char(DBusConnection *conn,
                        const char *dev_uuid,
                        const char *char_uuid,
                        char *output,
                        size_t output_len)
{
	DBusMessage *msg;
	DBusError err;

	msg = dbus_message_new_method_call(
	    "org.bluez",                          // target for the method call
	    "/",                                  // object to call on
	    "org.freedesktop.DBus.ObjectManager", // interface to call on
	    "GetManagedObjects");                 // method name
	if (send_message(conn, &err, &msg) != 0) {
		return -1;
	}

	DBusMessageIter args, first_elm;
	dbus_message_iter_init(msg, &args);
	int ret = array_get_first_elem_of_type(&args, DBUS_TYPE_DICT_ENTRY,
	                                       &first_elm);
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
			ret = gatt_char_has_uuid_and_notify(&c, char_uuid,
			                                    &char_path_str);
			if (ret <= 0) {
				continue;
			}
			if (!starts_with_and_has_slash(char_path_str,
			                               dev_path_str)) {
				continue;
			}

			ssize_t written =
			    snprintf(output, output_len, "%s", char_path_str);
			// free reply
			dbus_message_unref(msg);
			return written;
		}
	}

	// free reply
	dbus_message_unref(msg);
	return 0;
}


/*
 *
 * BLE notify object implementation.
 *
 */

struct ble_notify
{
	struct os_ble_device base;
	DBusConnection *conn;
	DBusError err;
	int fd;
};

static int
os_ble_notify_read(struct os_ble_device *bdev,
                   uint8_t *data,
                   size_t length,
                   int milliseconds)
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

	if (dev->conn != NULL) {
		dbus_connection_unref(dev->conn);
		dev->conn = NULL;
	}

	free(dev);
}

int
os_ble_notify_open(const char *dev_uuid,
                   const char *char_uuid,
                   struct os_ble_device **out_ble)
{
	DBusMessage *msg;

	struct ble_notify *bledev = U_TYPED_CALLOC(struct ble_notify);
	bledev->base.read = os_ble_notify_read;
	bledev->base.destroy = os_ble_notify_destroy;
	bledev->fd = -1;

	dbus_error_init(&bledev->err);
	bledev->conn = dbus_bus_get(DBUS_BUS_SYSTEM, &bledev->err);
	if (dbus_error_is_set(&bledev->err)) {
		fprintf(stderr, "DBUS Connection Error: %s\n",
		        bledev->err.message);
		dbus_error_free(&bledev->err);
	}
	if (bledev->conn == NULL) {
		os_ble_notify_destroy(&bledev->base);
		return -1;
	}

	char dbus_address[256]; // should be long enough
	XRT_MAYBE_UNUSED ssize_t written =
	    get_path_to_notify_char(bledev->conn, dev_uuid, char_uuid,
	                            dbus_address, sizeof(dbus_address));

	msg = dbus_message_new_method_call(
	    "org.bluez",                     // target for the method call
	    dbus_address,                    // object to call on
	    "org.bluez.GattCharacteristic1", // interface to call on
	    "AcquireNotify");                // method name
	if (msg == NULL) {
		fprintf(stderr, "Message Null after construction\n");
		os_ble_notify_destroy(&bledev->base);
		return -1;
	}

	// AcquireNotify has a argument of Array of Dicts.
	add_empty_dict_sv(msg);

	// Send the message, consumes our message and returns what we received.
	if (send_message(bledev->conn, &bledev->err, &msg) != 0) {
		return -1;
	}

	DBusMessageIter args;
	char *response = NULL;
	dbus_message_iter_init(msg, &args);
	while (true) {
		int type = dbus_message_iter_get_arg_type(&args);
		if (type == DBUS_TYPE_INVALID) {
			break;
		} else if (type == DBUS_TYPE_STRING) {
			dbus_message_iter_get_basic(&args, &response);
			printf("DBus call returned message: %s\n", response);
		} else if (type == DBUS_TYPE_UNIX_FD) {
			dbus_message_iter_get_basic(&args, &bledev->fd);
		}
		dbus_message_iter_next(&args);
	}

	// free reply
	dbus_message_unref(msg);

	// We didn't get a fd.
	if (bledev->fd == -1) {
		os_ble_notify_destroy(&bledev->base);
		return -1;
	}

	*out_ble = &bledev->base;

	return 0;
}
