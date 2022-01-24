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

#include "u_string_list.h"
#include "u_string_list.hpp"

using xrt::auxiliary::util::StringList;


struct u_string_list
{
	u_string_list() = default;
	u_string_list(StringList &&sl) : list(std::move(sl)) {}

	StringList list;
};

struct u_string_list *
u_string_list_create()
{
	try {
		auto ret = std::make_unique<u_string_list>();
		return ret.release();
	} catch (std::exception const &) {
		return nullptr;
	}
}


struct u_string_list *
u_string_list_create_with_capacity(uint32_t capacity)
{

	try {
		auto ret = std::make_unique<u_string_list>(xrt::auxiliary::util::StringList{capacity});
		return ret.release();
	} catch (std::exception const &) {
		return nullptr;
	}
}

struct u_string_list *
u_string_list_create_from_list(struct u_string_list *usl)
{
	try {
		auto ret = std::make_unique<u_string_list>(xrt::auxiliary::util::StringList{usl->list});
		return ret.release();
	} catch (std::exception const &) {
		return nullptr;
	}
}

struct u_string_list *
u_string_list_create_from_array(const char *const *arr, uint32_t size)
{
	if (arr == nullptr || size == 0) {
		return u_string_list_create();
	}
	try {
		auto ret = std::make_unique<u_string_list>(xrt::auxiliary::util::StringList{size});
		for (uint32_t i = 0; i < size; ++i) {
			ret->list.push_back(arr[i]);
		}
		return ret.release();
	} catch (std::exception const &) {
		return nullptr;
	}
}

uint32_t
u_string_list_get_size(const struct u_string_list *usl)
{

	if (usl == nullptr) {
		return 0;
	}
	return usl->list.size();
}


const char *const *
u_string_list_get_data(const struct u_string_list *usl)
{

	if (usl == nullptr) {
		return nullptr;
	}
	return usl->list.data();
}


int
u_string_list_append(struct u_string_list *usl, const char *str)
{
	if (usl == nullptr) {
		return -1;
	}
	try {
		usl->list.push_back(str);
		return 1;
	} catch (std::exception const &) {
		return -1;
	}
}

int
u_string_list_append_array(struct u_string_list *usl, const char *const *arr, uint32_t size)
{

	if (usl == nullptr) {
		return -1;
	}
	try {
		for (uint32_t i = 0; i < size; ++i) {
			usl->list.push_back(arr[i]);
		}
		return 1;
	} catch (std::exception const &) {
		return -1;
	}
}

int
u_string_list_append_unique(struct u_string_list *usl, const char *str)
{
	if (usl == nullptr) {
		return -1;
	}
	try {
		auto added = usl->list.push_back_unique(str);
		return added ? 1 : 0;
	} catch (std::exception const &) {
		return -1;
	}
}

bool
u_string_list_contains(struct u_string_list *usl, const char *str)
{
	return usl->list.contains(str);
}

void
u_string_list_destroy(struct u_string_list **list_ptr)
{
	if (list_ptr == nullptr) {
		return;
	}
	u_string_list *list = *list_ptr;
	if (list == nullptr) {
		return;
	}
	delete list;
	*list_ptr = nullptr;
}
