// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Code to generate permutation of indices.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_math
 */


#include "xrt/xrt_compiler.h"

#pragma once


#ifdef __cplusplus
extern "C" {
#endif



struct m_permutator
{
	uint32_t *indices;
	uint32_t *elements;
	uint32_t i;
	uint32_t n;
};

/*!
 * Returns false if there are no new permutation available, the only thing you
 * need to do before calling this function is to make sure that the struct has
 * been zero initialised.
 */
bool
m_permutator_step(struct m_permutator *mp, uint32_t *out_elements, uint32_t num_elements);

void
m_permutator_reset(struct m_permutator *mp);


void
m_do_the_thing();


#ifdef __cplusplus
}
#endif
