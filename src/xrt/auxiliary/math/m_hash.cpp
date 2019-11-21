// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Hashing function.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_math
 */

#include "math/m_api.h"
#include <string>


extern "C" size_t
math_hash_string(const char *str_c, size_t length)
{
	std::string str = std::string(str_c, length);
	std::hash<std::string> str_hash;
	return str_hash(str);
}
