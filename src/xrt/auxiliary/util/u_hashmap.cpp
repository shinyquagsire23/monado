// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Hashmap for integer values header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_hashmap.h"

#include <unordered_map>
#include <vector>


/*
 *
 * Private structs and defines.
 *
 */

struct u_hashmap_int
{
	std::unordered_map<uint64_t, void *> map = {};
};


/*
 *
 * "Exported" functions.
 *
 */

extern "C" int
u_hashmap_int_create(struct u_hashmap_int **out_hashmap_int)
{
	auto hs = new u_hashmap_int;
	*out_hashmap_int = hs;
	return 0;
}

extern "C" int
u_hashmap_int_destroy(struct u_hashmap_int **hmi)
{
	delete *hmi;
	*hmi = NULL;
	return 0;
}

int
u_hashmap_int_find(struct u_hashmap_int *hmi, uint64_t key, void **out_item)
{
	auto search = hmi->map.find(key);

	if (search != hmi->map.end()) {
		*out_item = search->second;
		return 0;
	}
	return -1;
}

extern "C" int
u_hashmap_int_insert(struct u_hashmap_int *hmi, uint64_t key, void *value)
{
	hmi->map[key] = value;
	return 0;
}

extern "C" int
u_hashmap_int_erase(struct u_hashmap_int *hmi, uint64_t key)
{
	hmi->map.erase(key);
	return 0;
}

bool
u_hashmap_int_empty(const struct u_hashmap_int *hmi)
{
	return hmi->map.empty();
}

extern "C" void
u_hashmap_int_clear_and_call_for_each(struct u_hashmap_int *hmi, u_hashmap_int_callback cb, void *priv)
{
	std::vector<void *> tmp;
	tmp.reserve(hmi->map.size());

	for (auto &n : hmi->map) {
		tmp.push_back(n.second);
	}

	hmi->map.clear();

	for (auto n : tmp) {
		cb(n, priv);
	}
}
