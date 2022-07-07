// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Expose std::vector to C
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define U_VECTOR_DECLARATION(TYPE)                                                                                     \
	struct u_vector_##TYPE                                                                                         \
	{                                                                                                              \
		void *ptr;                                                                                             \
	};                                                                                                             \
	struct u_vector_##TYPE u_vector_##TYPE##_create();                                                             \
	void u_vector_##TYPE##_push_back(struct u_vector_##TYPE uv, TYPE e);                                           \
	TYPE u_vector_##TYPE##_at(struct u_vector_##TYPE uv, size_t i);                                                \
	void u_vector_##TYPE##_destroy(struct u_vector_##TYPE *uv);

U_VECTOR_DECLARATION(int)
U_VECTOR_DECLARATION(float)

#ifdef __cplusplus
}
#endif
