// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Code to generate permutation of indices.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_math
 */


#include "util/u_misc.h"
#include "util/u_logging.h"

#include "m_permutation.h"


/*
 *
 * Helper functions
 *
 */

static void
swap(uint32_t *array, uint32_t a, uint32_t b)
{
	int tmp = array[a];
	array[a] = array[b];
	array[b] = tmp;
}

static void
copy(struct m_permutator *mp, uint32_t *out_elements)
{
	for (uint32_t i = 0; i < mp->n; i++) {
		out_elements[i] = mp->elements[i];
	}
}

static void
setup(struct m_permutator *mp, uint32_t num_elements)
{
	mp->i = 0;
	mp->n = num_elements;
	mp->indices = U_TYPED_ARRAY_CALLOC(uint32_t, num_elements);
	mp->elements = U_TYPED_ARRAY_CALLOC(uint32_t, num_elements);
	for (uint32_t i = 0; i < mp->n; i++) {
		mp->indices[i] = 0;
		mp->elements[i] = i;
	}
}

static bool
step(struct m_permutator *mp)
{
	while (mp->i < mp->n) {
		if (mp->indices[mp->i] < mp->i) {
			uint32_t a = mp->i % 2 == 0 ? 0 : mp->indices[mp->i];
			uint32_t b = mp->i;
			swap(mp->elements, a, b);
			mp->indices[mp->i]++;
			mp->i = 0;
			return true;
		} else {
			mp->indices[mp->i] = 0;
			mp->i++;
		}
	}

	return false;
}


/*
 *
 * 'Exported' functions.
 *
 */

bool
m_permutator_step(struct m_permutator *mp, uint32_t *out_elements, uint32_t num_elements)
{
	if (mp->indices == NULL || mp->n != num_elements) {
		setup(mp, num_elements);
		copy(mp, out_elements);
		return true;
	} else if (step(mp)) {

		copy(mp, out_elements);
		return true;
	} else {
		return false;
	}
}

void
m_permutator_reset(struct m_permutator *mp)
{
	if (mp->indices != NULL) {
		free(mp->indices);
		mp->indices = NULL;
	}
	if (mp->elements != NULL) {
		free(mp->elements);
		mp->elements = NULL;
	}

	U_ZERO(mp);
}


/*
 *
 * Debug functions.
 *
 */

#if 1

#include <stdio.h>

static void
printArray(const uint32_t *array, uint32_t num)
{
	static int count = 0;
	fprintf(stderr, "GLARG #%i: ", count++);
	for (uint32_t i = 0; i < num; i++) {
		fprintf(stderr, "%i, ", array[i]);
	}
	fprintf(stderr, "\n");
}

void
m_do_the_thing()
{
	struct m_permutator mp = {0};
	uint32_t elements[7];
	uint32_t size = ARRAY_SIZE(elements);
	while (m_permutator_step(&mp, elements, size)) {
		printArray(elements, size);
	}

	m_permutator_reset(&mp);

	U_LOG_D("BLARG!");
}

#endif
