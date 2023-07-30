// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Expose std::deque to C
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "util/u_time.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define U_DEQUE_DECLARATION(TYPE)                                                                                      \
	struct u_deque_##TYPE                                                                                          \
	{                                                                                                              \
		void *ptr;                                                                                             \
	};                                                                                                             \
	struct u_deque_##TYPE u_deque_##TYPE##_create(void);                                                           \
	void u_deque_##TYPE##_push_back(struct u_deque_##TYPE ud, TYPE e);                                             \
	bool u_deque_##TYPE##_pop_front(struct u_deque_##TYPE ud, TYPE *e);                                            \
	TYPE u_deque_##TYPE##_at(struct u_deque_##TYPE ud, size_t i);                                                  \
	size_t u_deque_##TYPE##_size(struct u_deque_##TYPE wrap);                                                      \
	void u_deque_##TYPE##_destroy(struct u_deque_##TYPE *ud);

U_DEQUE_DECLARATION(timepoint_ns)

#ifdef __cplusplus
}
#endif
