// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief A collection of strings, like a list of extensions to enable
 *
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 *
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @brief A collection of string literals (const char *), such as used for extension name lists.
 *
 * @see xrt::auxiliary::util::StringList
 */
struct u_string_list;

/*!
 * @brief Create a string list with room for at least the given number of strings.
 *
 * @public @memberof u_string_list
 */
struct u_string_list *
u_string_list_create(void);

/*!
 * @brief Create a string list with room for at least the given number of strings.
 *
 * @public @memberof u_string_list
 */
struct u_string_list *
u_string_list_create_with_capacity(uint32_t capacity);

/*!
 * @brief Create a new string list from an existing string list.
 *
 * @public @memberof u_string_list
 */
struct u_string_list *
u_string_list_create_from_list(struct u_string_list *usl);

/*!
 * @brief Create a new string list from an array of suitable strings.
 *
 * @param arr an array of zero or more non-null, null-terminated string that must live at least as long as the list,
 * preferably string literals.
 * @param size the number of elements in the array.
 *
 * @public @memberof u_string_list
 */
struct u_string_list *
u_string_list_create_from_array(const char *const *arr, uint32_t size);

/*!
 * @brief Retrieve the number of elements in the list
 *
 * @public @memberof u_string_list
 */
uint32_t
u_string_list_get_size(const struct u_string_list *usl);

/*!
 * @brief Retrieve the data pointer of the list
 *
 * @public @memberof u_string_list
 */
const char *const *
u_string_list_get_data(const struct u_string_list *usl);

/*!
 * @brief Append a new string literal to the list.
 *
 * @param usl self pointer
 * @param str a non-null, null-terminated string that must live at least as long as the list, preferably a string
 * literal.
 * @return 1 if successfully added, negative for errors.
 *
 * @public @memberof u_string_list
 */
int
u_string_list_append(struct u_string_list *usl, const char *str);


/*!
 * @brief Append an array of new string literals to the list.
 *
 * @param usl self pointer
 * @param arr an array of zero or more non-null, null-terminated string that must live at least as long as the list,
 * preferably string literals.
 * @param size the number of elements in the array.
 * @return 1 if successfully added, negative for errors.
 *
 * @public @memberof u_string_list
 */
int
u_string_list_append_array(struct u_string_list *usl, const char *const *arr, uint32_t size);

/*!
 * @brief Append a new string literal to the list, if it's not the same as a string already in the list.
 *
 * (Comparing string contents, not pointers)
 *
 * @param usl self pointer
 * @param str a non-null, null-terminated string that must live at least as long as the list, preferably a string
 * literal.
 * @return 1 if successfully added, 0 if already existing so not added, negative for errors.
 *
 * @public @memberof u_string_list
 */
int
u_string_list_append_unique(struct u_string_list *usl, const char *str);

/*!
 * @brief Check if the string is in the list.
 *
 * (Comparing string contents, not pointers)
 *
 * @param usl self pointer
 * @param str a non-null, null-terminated string.
 *
 * @return true if the string is in the list.
 */
bool
u_string_list_contains(struct u_string_list *usl, const char *str);

/*!
 * @brief Destroy a string list.
 *
 * Performs null checks and sets your pointer to zero.
 *
 * @public @memberof u_string_list
 */
void
u_string_list_destroy(struct u_string_list **list_ptr);

#ifdef __cplusplus
} // extern "C"
#endif
