// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Expose std::vector to C
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup aux_util
 */

#include "u_vector.h"
#include <vector>

using std::vector;

#define U_VECTOR_IMPLEMENTATION(TYPE)                                                                                  \
	u_vector_##TYPE u_vector_##TYPE##_create()                                                                     \
	{                                                                                                              \
		u_vector_##TYPE uv{new vector<TYPE>};                                                                  \
		return uv;                                                                                             \
	}                                                                                                              \
                                                                                                                       \
	void u_vector_##TYPE##_push_back(u_vector_##TYPE uv, TYPE e)                                                   \
	{                                                                                                              \
		vector<TYPE> *v = static_cast<vector<TYPE> *>(uv.ptr);                                                 \
		v->push_back(e);                                                                                       \
	}                                                                                                              \
                                                                                                                       \
	TYPE u_vector_##TYPE##_at(u_vector_##TYPE uv, size_t i)                                                        \
	{                                                                                                              \
		vector<TYPE> *v = static_cast<vector<TYPE> *>(uv.ptr);                                                 \
		return v->at(i);                                                                                       \
	}                                                                                                              \
                                                                                                                       \
	void u_vector_##TYPE##_destroy(u_vector_##TYPE *uv)                                                            \
	{                                                                                                              \
		vector<TYPE> *v = static_cast<vector<TYPE> *>(uv->ptr);                                                \
		delete v;                                                                                              \
		uv->ptr = nullptr;                                                                                     \
	}

extern "C" {
U_VECTOR_IMPLEMENTATION(int)
U_VECTOR_IMPLEMENTATION(float)
}
