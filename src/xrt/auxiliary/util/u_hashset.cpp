// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Hashset struct header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_hashset.h"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>


/*
 *
 * Private structs and defines.
 *
 */

struct u_hashset
{
	std::unordered_map<std::string, struct u_hashset_item *> map = {};
};


/*
 *
 * "Exported" functions.
 *
 */

extern "C" int
u_hashset_create(struct u_hashset **out_hashset)
{
	auto hs = new u_hashset;
	*out_hashset = hs;
	return 0;
}

extern "C" int
u_hashset_destroy(struct u_hashset **hs)
{
	delete *hs;
	*hs = NULL;
	return 0;
}

extern "C" int
u_hashset_find_str(struct u_hashset *hs,
                   const char *str,
                   size_t length,
                   struct u_hashset_item **out_item)
{
	std::string key = std::string(str, length);
	auto search = hs->map.find(key);

	if (search != hs->map.end()) {
		*out_item = search->second;
		return 0;
	}
	return -1;
}

extern "C" int
u_hashset_find_c_str(struct u_hashset *hs,
                     const char *c_str,
                     struct u_hashset_item **out_item)
{
	size_t length = strlen(c_str);
	return u_hashset_find_str(hs, c_str, length, out_item);
}

extern "C" int
u_hashset_insert_item(struct u_hashset *hs, struct u_hashset_item *item)
{
	std::string key = std::string(item->c_str, item->length);
	hs->map[key] = item;
	return 0;
}

extern "C" int
u_hashset_erase_item(struct u_hashset *hs, struct u_hashset_item *item)
{
	std::string key = std::string(item->c_str, item->length);
	hs->map.erase(key);
	return 0;
}

extern "C" int
u_hashset_erase_str(struct u_hashset *hs, const char *str, size_t length)
{
	std::string key = std::string(str, length);
	hs->map.erase(key);
	return 0;
}

extern "C" int
u_hashset_erase_c_str(struct u_hashset *hs, const char *c_str)
{
	size_t length = strlen(c_str);
	return u_hashset_erase_str(hs, c_str, length);
}

extern "C" void
u_hashset_clear_and_call_for_each(struct u_hashset *hs,
                                  u_hashset_callback cb,
                                  void *priv)
{
	std::vector<struct u_hashset_item *> tmp;
	tmp.reserve(hs->map.size());

	for (auto &n : hs->map) {
		tmp.push_back(n.second);
	}

	hs->map.clear();

	for (auto n : tmp) {
		cb(n, priv);
	}
}
