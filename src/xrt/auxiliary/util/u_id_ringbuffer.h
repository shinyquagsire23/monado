// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Ring buffer for things keyed on an ID but otherwise maintained externally, for C usage.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Container type to let you store IDs in a ring buffer, and maybe your own data in your own parallel array.
 *
 * The IDs are uint64_t. If you don't need any of the order-dependent functionality, you can treat use them for any
 * purpose you like.
 *
 * Some functionality requires that IDs be pushed in increasing order, but it's highlighted in the docs.
 * If you need more than this, either extend this or use the underlying C++ container if that's an OK solution for you.
 *
 */
struct u_id_ringbuffer;

/**
 * Create a ringbuffer for storing IDs.
 *
 * You might keep an array of equivalent capacity locally: methods of this container will tell you which index in that
 * array to interact with.
 *
 * @param capacity
 * @return struct u_id_ringbuffer*
 *
 * @public @memberof u_id_ringbuffer
 */
struct u_id_ringbuffer *
u_id_ringbuffer_create(uint32_t capacity);

/**
 * Push a new element to the back
 *
 * @param uirb self pointer
 * @param id The ID to push back. It is your responsibility to make sure you insert in order if you want to use ordered
 * methods.
 * @return the "inner" index in your array to store any associated data, or negative if error.
 *
 * @public @memberof u_id_ringbuffer
 */
int64_t
u_id_ringbuffer_push_back(struct u_id_ringbuffer *uirb, uint64_t id);

/**
 * Pop an element from the front, if any
 *
 * @param uirb self pointer.
 *
 * @public @memberof u_id_ringbuffer
 */
void
u_id_ringbuffer_pop_front(struct u_id_ringbuffer *uirb);

/**
 * Pop an element from the back, if any
 *
 * @param uirb self pointer.
 *
 * @public @memberof u_id_ringbuffer
 */
void
u_id_ringbuffer_pop_back(struct u_id_ringbuffer *uirb);

/**
 * Get the back (most recent) of the buffer
 *
 * @param uirb self pointer
 * @param[out] out_id Where to store the back ID
 * @return the "inner" index in your array with any associated data, or negative if error (empty, etc).
 *
 * @public @memberof u_id_ringbuffer
 */
int32_t
u_id_ringbuffer_get_back(struct u_id_ringbuffer *uirb, uint64_t *out_id);

/**
 * Get the front (least recent) of the buffer
 *
 * @param uirb self pointer
 * @param[out] out_id Where to store the front ID
 * @return the "inner" index in your array with any associated data, or negative if error (empty, etc).
 *
 * @public @memberof u_id_ringbuffer
 */
int32_t
u_id_ringbuffer_get_front(struct u_id_ringbuffer *uirb, uint64_t *out_id);

/**
 * Get the number of elements in the buffer
 *
 * @param uirb self pointer
 * @return the size
 *
 * @public @memberof u_id_ringbuffer
 */
uint32_t
u_id_ringbuffer_get_size(struct u_id_ringbuffer const *uirb);

/**
 * Get whether the buffer is empty
 *
 * @param uirb self pointer
 * @return true if empty
 *
 * @public @memberof u_id_ringbuffer
 */
bool
u_id_ringbuffer_is_empty(struct u_id_ringbuffer const *uirb);

/**
 * Get an element a certain distance ("age") from the back of the buffer
 *
 * See u_id_ringbuffer_get_at_clamped_age() if you want to clamp the age
 *
 * @param uirb self pointer
 * @param age the distance from the back (0 is the same as back, 1 is previous, etc)
 * @param[out] out_id Where to store the ID, if successful (optional)
 * @return the "inner" index in your array with any associated data, or negative if error (empty, out of range, etc).
 *
 * @public @memberof u_id_ringbuffer
 */
int32_t
u_id_ringbuffer_get_at_age(struct u_id_ringbuffer *uirb, uint32_t age, uint64_t *out_id);

/**
 * Get an element a certain distance ("age") from the back of the buffer, clamping age to stay in bounds as long as the
 * buffer is not empty.
 *
 * See u_id_ringbuffer_get_at_age() if you don't want clamping.
 *
 * @param uirb self pointer
 * @param age the distance from the back (0 is the same as back, 1 is previous, etc)
 * @param[out] out_id Where to store the ID, if successful (optional)
 * @return the "inner" index in your array with any associated data, or negative if error (empty, etc).
 *
 * @public @memberof u_id_ringbuffer
 */
int32_t
u_id_ringbuffer_get_at_clamped_age(struct u_id_ringbuffer *uirb, uint32_t age, uint64_t *out_id);

/**
 * Get an element a certain index from the front of the (logical) buffer
 *
 * @param uirb self pointer
 * @param index the distance from the front (0 is the same as front, 1 is newer, etc)
 * @param[out] out_id Where to store the ID, if successful (optional)
 * @return the "inner" index in your array with any associated data, or negative if error (empty, out of range, etc).
 *
 * @public @memberof u_id_ringbuffer
 */
int32_t
u_id_ringbuffer_get_at_index(struct u_id_ringbuffer *uirb, uint32_t index, uint64_t *out_id);

/**
 * Find the latest element not less than the supplied ID @p search_id .
 *
 * Assumes/depends on your maintenance of entries in ascending order. If you aren't ensuring this, use
 * u_id_ringbuffer_find_id_unordered() instead.
 *
 * (Wraps `std::lower_bound`)
 *
 * @param uirb self pointer
 * @param search_id the ID to search for.
 * @param[out] out_id Where to store the ID found, if successful (optional)
 * @param[out] out_index Where to store the ring buffer index (not the inner index) found, if successful (optional)
 * @return the "inner" index in your array with any associated data, or negative if error (empty, etc).
 *
 * @public @memberof u_id_ringbuffer
 */
int32_t
u_id_ringbuffer_lower_bound_id(struct u_id_ringbuffer *uirb, uint64_t search_id, uint64_t *out_id, uint32_t *out_index);

/**
 * Find the element with the supplied ID @p search_id in an unordered buffer.
 *
 * This does *not* depend on order so does a linear search. If you are keeping your IDs in ascending order, use
 * u_id_ringbuffer_lower_bound_id() instead.
 *
 * @param uirb self pointer
 * @param search_id the ID to search for.
 * @param[out] out_id Where to store the ID found, if successful (optional)
 * @param[out] out_index Where to store the ring buffer index (not the inner index) found, if successful (optional)
 * @return the "inner" index in your array with any associated data, or negative if error (empty, etc).
 *
 * @public @memberof u_id_ringbuffer
 */
int32_t
u_id_ringbuffer_find_id_unordered(struct u_id_ringbuffer *uirb,
                                  uint64_t search_id,
                                  uint64_t *out_id,
                                  uint32_t *out_index);

/**
 * Destroy an ID ring buffer.
 *
 * Does null checks.
 *
 * @param ptr_to_uirb Address of your ring buffer pointer. Will be set to zero.
 *
 * @public @memberof u_id_ringbuffer
 */
void
u_id_ringbuffer_destroy(struct u_id_ringbuffer **ptr_to_uirb);

#ifdef __cplusplus
} // extern "C"
#endif
