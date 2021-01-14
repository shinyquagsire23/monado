// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Utilities for accessing members in an OpenXR structure chain.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

#pragma once

#include <xrt/xrt_openxr_includes.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @ingroup oxr_api
 * @{
 */

/*!
 * Finds an input struct of the given type in a next-chain.
 *
 * Returns NULL if nothing matching is found.
 *
 * Prefer using OXR_GET_INPUT_FROM_CHAIN() instead, since it includes the
 * casting.
 */
static inline XrBaseInStructure const *
oxr_find_input_in_chain(const void *ptr, XrStructureType desired)
{
	while (ptr != NULL) {
		XrBaseInStructure const *base = (XrBaseInStructure const *)ptr;
		if (base->type == desired) {
			return base;
		}
		ptr = base->next;
	}
	return NULL;
}

/*!
 * Finds an input struct of the given type in a next-chain and casts it as
 * desired.
 *
 * Returns NULL if nothing matching is found.
 *
 * Note: There is no protection here to ensure that STRUCTURE_TYPE_ENUM (an
 * XrStructureType value) and TYPE (a type name) actually match!
 */
#define OXR_GET_INPUT_FROM_CHAIN(PTR, STRUCTURE_TYPE_ENUM, TYPE)                                                       \
	((TYPE const *)oxr_find_input_in_chain(PTR, STRUCTURE_TYPE_ENUM))

/*!
 * Finds an output struct of the given type in a next-chain.
 *
 * Returns NULL if nothing matching is found.
 *
 * Prefer using OXR_GET_OUTPUT_FROM_CHAIN() instead, since it includes the
 * casting.
 */
static inline XrBaseOutStructure *
oxr_find_output_in_chain(void *ptr, XrStructureType desired)
{
	while (ptr != NULL) {
		XrBaseOutStructure *base = (XrBaseOutStructure *)ptr;
		if (base->type == desired) {
			return base;
		}
		ptr = base->next;
	}
	return NULL;
}

/*!
 * Finds an output struct of the given type in a next-chain and casts it as
 * desired.
 *
 * Returns NULL if nothing matching is found.
 *
 * Note: There is no protection here to ensure that STRUCTURE_TYPE_ENUM (an
 * XrStructureType value) and TYPE (a type name) actually match!
 */
#define OXR_GET_OUTPUT_FROM_CHAIN(PTR, STRUCTURE_TYPE_ENUM, TYPE)                                                      \
	((TYPE *)oxr_find_output_in_chain(PTR, STRUCTURE_TYPE_ENUM))

/*!
 * @}
 */

#ifdef __cplusplus
}
#endif
