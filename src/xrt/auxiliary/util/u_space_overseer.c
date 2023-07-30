// Copyright 2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief A implementation of the @ref xrt_space_overseer interface.
 *
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "xrt/xrt_space.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_tracking.h"

#include "math/m_space.h"

#include "util/u_misc.h"
#include "util/u_hashmap.h"
#include "util/u_logging.h"
#include "util/u_space_overseer.h"

#include <assert.h>
#include <pthread.h>


/*
 *
 * Structs and defines.
 *
 */

/*!
 * Keeps track of what kind of space it is.
 */
enum u_space_type
{
	U_SPACE_TYPE_NULL,
	U_SPACE_TYPE_POSE,
	U_SPACE_TYPE_OFFSET,
	U_SPACE_TYPE_ROOT,
};

/*!
 * Representing a single space, can be several ones. There should only be one
 * root space per overseer.
 */
struct u_space
{
	struct xrt_space base;

	/*!
	 * The space this space is in.
	 */
	struct u_space *next;

	/*!
	 * The type of the space.
	 */
	enum u_space_type type;

	union {
		struct
		{
			struct xrt_device *xdev;
			enum xrt_input_name xname;
		} pose;

		struct
		{
			struct xrt_pose pose;
		} offset;
	};
};

/*!
 * Default implementation of the xrt_space_overseer object.
 */
struct u_space_overseer
{
	struct xrt_space_overseer base;

	//! Main graph lock.
	pthread_rwlock_t lock;

	//! Map from xdev to space, each entry holds a reference.
	struct u_hashmap_int *xdev_map;
};


/*
 *
 * Helper functions.
 *
 */

static inline struct u_space *
u_space(struct xrt_space *xs)
{
	return (struct u_space *)xs;
}

static inline struct u_space_overseer *
u_space_overseer(struct xrt_space_overseer *xso)
{
	return (struct u_space_overseer *)xso;
}

/*!
 * A lot of code here uses u_space directly and need to change reference count
 * so this helper is here to make that easier.
 */
static inline void
u_space_reference(struct u_space **dst, struct u_space *src)
{
	struct u_space *old_dst = *dst;

	if (old_dst == src) {
		return;
	}

	if (src) {
		xrt_reference_inc(&src->base.reference);
	}

	*dst = src;

	if (old_dst) {
		if (xrt_reference_dec(&old_dst->base.reference)) {
			old_dst->base.destroy(&old_dst->base);
		}
	}
}

/*!
 * Helper function when clearing a hashmap to also unreference a space.
 */
static void
hashmap_unreference_space_items(void *item, void *priv)
{
	struct u_space *us = (struct u_space *)item;
	u_space_reference(&us, NULL);
}

static struct u_space *
find_xdev_space_read_locked(struct u_space_overseer *uso, struct xrt_device *xdev)
{
	void *ptr = NULL;
	uint64_t key = (uint64_t)(intptr_t)xdev;
	u_hashmap_int_find(uso->xdev_map, key, &ptr);

	if (ptr == NULL) {
		U_LOG_E("Looking for space belonging to unknown xrt_device! '%s'", xdev->str);
	}
	assert(ptr != NULL);

	return (struct u_space *)ptr;
}


/*
 *
 * Graph traversing functions.
 *
 */

/*!
 * For each space, push the relation of that space and then traverse by calling
 * @p push_then_traverse again with the parent space. That means traverse goes
 * from a leaf space to a the root space, relations are pushed in the same
 * order.
 */
static void
push_then_traverse(struct xrt_relation_chain *xrc, struct u_space *space, uint64_t at_timestamp_ns)
{
	switch (space->type) {
	case U_SPACE_TYPE_NULL: break; // No-op
	case U_SPACE_TYPE_POSE: {
		assert(space->pose.xdev != NULL);
		assert(space->pose.xname != 0);

		struct xrt_space_relation xsr;
		xrt_device_get_tracked_pose(space->pose.xdev, space->pose.xname, at_timestamp_ns, &xsr);
		m_relation_chain_push_relation(xrc, &xsr);
	} break;
	case U_SPACE_TYPE_OFFSET: m_relation_chain_push_pose_if_not_identity(xrc, &space->offset.pose); break;
	case U_SPACE_TYPE_ROOT: return; // Stops the traversing.
	}

	// Please tail-call optimise this miss compiler.
	assert(space->next != NULL);
	push_then_traverse(xrc, space->next, at_timestamp_ns);
}

/*!
 * For each space, traverse by calling @p traverse_then_push_inverse again with
 * the parent space then push the inverse of the relation of that. That means
 * traverse goes from a leaf space to a the root space, relations are pushed in
 * the reversed order.
 */
static void
traverse_then_push_inverse(struct xrt_relation_chain *xrc, struct u_space *space, uint64_t at_timestamp_ns)
{
	// Done traversing.
	switch (space->type) {
	case U_SPACE_TYPE_NULL: break;
	case U_SPACE_TYPE_POSE: break;
	case U_SPACE_TYPE_OFFSET: break;
	case U_SPACE_TYPE_ROOT: return; // Stops the traversing.
	}

	// Can't tail-call optimise this one :(
	assert(space->next != NULL);
	traverse_then_push_inverse(xrc, space->next, at_timestamp_ns);

	switch (space->type) {
	case U_SPACE_TYPE_NULL: break; // No-op
	case U_SPACE_TYPE_POSE: {
		assert(space->pose.xdev != NULL);
		assert(space->pose.xname != 0);

		struct xrt_space_relation xsr;
		xrt_device_get_tracked_pose(space->pose.xdev, space->pose.xname, at_timestamp_ns, &xsr);
		m_relation_chain_push_inverted_relation(xrc, &xsr);
	} break;
	case U_SPACE_TYPE_OFFSET: m_relation_chain_push_inverted_pose_if_not_identity(xrc, &space->offset.pose); break;
	case U_SPACE_TYPE_ROOT: assert(false); // Should not get here.
	}
}

static void
build_relation_chain_read_locked(struct u_space_overseer *uso,
                                 struct xrt_relation_chain *xrc,
                                 struct u_space *base,
                                 struct u_space *target,
                                 uint64_t at_timestamp_ns)
{
	assert(xrc != NULL);
	assert(base != NULL);
	assert(target != NULL);

	push_then_traverse(xrc, target, at_timestamp_ns);
	traverse_then_push_inverse(xrc, base, at_timestamp_ns);
}

static void
build_relation_chain(struct u_space_overseer *uso,
                     struct xrt_relation_chain *xrc,
                     struct u_space *base,
                     struct u_space *target,
                     uint64_t at_timestamp_ns)
{
	pthread_rwlock_rdlock(&uso->lock);
	build_relation_chain_read_locked(uso, xrc, base, target, at_timestamp_ns);
	pthread_rwlock_unlock(&uso->lock);
}

static inline void
special_resolve(struct xrt_relation_chain *xrc, struct xrt_space_relation *out_relation)
{
	// A space chain with zero step is always valid.
	if (xrc->step_count == 0) {
		out_relation->pose = (struct xrt_pose)XRT_POSE_IDENTITY;
		out_relation->relation_flags =                   //
		    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |   //
		    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | //
		    XRT_SPACE_RELATION_POSITION_VALID_BIT |      //
		    XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
	} else {
		m_relation_chain_resolve(xrc, out_relation);
	}
}


/*
 *
 * Direct space functions.
 *
 */

static void
space_destroy(struct xrt_space *xs)
{
	struct u_space *us = u_space(xs);

	assert(us->next != NULL || us->type == U_SPACE_TYPE_ROOT);

	u_space_reference(&us->next, NULL);

	free(us);
}

/*!
 * Creates a space, returns with a reference of one.
 */
static struct u_space *
create_space(enum u_space_type type, struct u_space *parent)
{
	assert(parent != NULL || type == U_SPACE_TYPE_ROOT);

	struct u_space *us = U_TYPED_CALLOC(struct u_space);
	us->base.reference.count = 1;
	us->base.destroy = space_destroy;
	us->type = type;

	u_space_reference(&us->next, parent);

	return us;
}

static void
create_and_set_root_space(struct u_space_overseer *uso)
{
	assert(uso->base.semantic.root == NULL);

	struct u_space *us = create_space(U_SPACE_TYPE_ROOT, NULL);

	// Created with one reference.
	uso->base.semantic.root = &us->base;
}


/*
 *
 * Member functions.
 *
 */

static xrt_result_t
create_offset_space(struct xrt_space_overseer *xso,
                    struct xrt_space *parent,
                    const struct xrt_pose *offset,
                    struct xrt_space **out_space)
{
	assert(out_space != NULL);
	assert(*out_space == NULL);

	struct u_space *uparent = u_space(parent);
	struct u_space *us = NULL;

	if (m_pose_is_identity(offset)) { // Small optimisation.
		us = create_space(U_SPACE_TYPE_NULL, uparent);
	} else {
		us = create_space(U_SPACE_TYPE_OFFSET, uparent);
		us->offset.pose = *offset;
	}

	// Created with one references.
	*out_space = &us->base;

	return XRT_SUCCESS;
}

static xrt_result_t
create_pose_space(struct xrt_space_overseer *xso,
                  struct xrt_device *xdev,
                  enum xrt_input_name name,
                  struct xrt_space **out_space)
{
	assert(out_space != NULL);
	assert(*out_space == NULL);

	struct u_space_overseer *uso = u_space_overseer(xso);

	// Only need the read lock.
	pthread_rwlock_rdlock(&uso->lock);

	struct u_space *uparent = find_xdev_space_read_locked(uso, xdev);
	struct u_space *us = create_space(U_SPACE_TYPE_POSE, uparent);

	// Safe to unlock now.
	pthread_rwlock_unlock(&uso->lock);

	us->pose.xdev = xdev;
	us->pose.xname = name;

	// Created with one references.
	*out_space = &us->base;

	return XRT_SUCCESS;
}

static xrt_result_t
locate_space(struct xrt_space_overseer *xso,
             struct xrt_space *base_space,
             const struct xrt_pose *base_offset,
             uint64_t at_timestamp_ns,
             struct xrt_space *space,
             const struct xrt_pose *offset,
             struct xrt_space_relation *out_relation)
{
	struct u_space_overseer *uso = u_space_overseer(xso);

	struct u_space *ubase_space = u_space(base_space);
	struct u_space *uspace = u_space(space);

	struct xrt_relation_chain xrc = {0};

	m_relation_chain_push_pose_if_not_identity(&xrc, offset);
	build_relation_chain(uso, &xrc, ubase_space, uspace, at_timestamp_ns);
	m_relation_chain_push_inverted_pose_if_not_identity(&xrc, base_offset);

	// For base_space =~= space (approx equals).
	special_resolve(&xrc, out_relation);

	return XRT_SUCCESS;
}

static xrt_result_t
locate_device(struct xrt_space_overseer *xso,
              struct xrt_space *base_space,
              const struct xrt_pose *base_offset,
              uint64_t at_timestamp_ns,
              struct xrt_device *xdev,
              struct xrt_space_relation *out_relation)
{
	struct u_space_overseer *uso = u_space_overseer(xso);

	struct u_space *ubase_space = u_space(base_space);

	struct xrt_relation_chain xrc = {0};

	// Only need the read lock.
	pthread_rwlock_rdlock(&uso->lock);

	struct u_space *uspace = find_xdev_space_read_locked(uso, xdev);
	build_relation_chain_read_locked(uso, &xrc, ubase_space, uspace, at_timestamp_ns);

	// Safe to unlock now.
	pthread_rwlock_unlock(&uso->lock);

	// Do as much work outside of the lock.
	m_relation_chain_push_inverted_pose_if_not_identity(&xrc, base_offset);
	special_resolve(&xrc, out_relation);

	return XRT_SUCCESS;
}

static void
destroy(struct xrt_space_overseer *xso)
{
	struct u_space_overseer *uso = u_space_overseer(xso);

	xrt_space_reference(&uso->base.semantic.unbounded, NULL);
	xrt_space_reference(&uso->base.semantic.stage, NULL);
	xrt_space_reference(&uso->base.semantic.local, NULL);
	xrt_space_reference(&uso->base.semantic.view, NULL);
	xrt_space_reference(&uso->base.semantic.root, NULL);

	// Each device has a reference to its space, make sure to unreference before creating.
	u_hashmap_int_clear_and_call_for_each(uso->xdev_map, hashmap_unreference_space_items, uso);
	u_hashmap_int_destroy(&uso->xdev_map);

	pthread_rwlock_destroy(&uso->lock);

	free(uso);
}


/*
 *
 * 'Exported' functions.
 *
 */

struct u_space_overseer *
u_space_overseer_create(void)
{
	struct u_space_overseer *uso = U_TYPED_CALLOC(struct u_space_overseer);
	uso->base.create_offset_space = create_offset_space;
	uso->base.create_pose_space = create_pose_space;
	uso->base.locate_space = locate_space;
	uso->base.locate_device = locate_device;
	uso->base.destroy = destroy;

	XRT_MAYBE_UNUSED int ret = 0;

	ret = pthread_rwlock_init(&uso->lock, NULL);
	assert(ret == 0);

	ret = u_hashmap_int_create(&uso->xdev_map);
	assert(ret == 0);

	create_and_set_root_space(uso);

	return uso;
}

void
u_space_overseer_legacy_setup(struct u_space_overseer *uso,
                              struct xrt_device **xdevs,
                              uint32_t xdev_count,
                              struct xrt_device *head,
                              const struct xrt_pose *local_offset)
{
	struct xrt_space *root = uso->base.semantic.root; // Convenience

	struct u_hashmap_int *torig_map = NULL;
	u_hashmap_int_create(&torig_map);


	for (uint32_t i = 0; i < xdev_count; i++) {
		struct xrt_device *xdev = xdevs[i];
		struct xrt_tracking_origin *torig = xdev->tracking_origin;
		uint64_t key = (uint64_t)(intptr_t)torig;
		struct xrt_space *xs = NULL;

		void *ptr = NULL;
		u_hashmap_int_find(torig_map, key, &ptr);

		if (ptr != NULL) {
			xs = (struct xrt_space *)ptr;
		} else {
			u_space_overseer_create_offset_space(uso, root, &torig->offset, &xs);
			u_hashmap_int_insert(torig_map, key, xs);
		}

		u_space_overseer_link_space_to_device(uso, xs, xdev);
	}

	// Each item has a exrta reference make sure to clear before destroying.
	u_hashmap_int_clear_and_call_for_each(torig_map, hashmap_unreference_space_items, uso);
	u_hashmap_int_destroy(&torig_map);

	// If these are set something is probably wrong, but just in case unset them.
	assert(uso->base.semantic.view == NULL);
	assert(uso->base.semantic.stage == NULL);
	assert(uso->base.semantic.local == NULL);
	xrt_space_reference(&uso->base.semantic.view, NULL);
	xrt_space_reference(&uso->base.semantic.stage, NULL);
	xrt_space_reference(&uso->base.semantic.local, NULL);

	xrt_space_reference(&uso->base.semantic.stage, uso->base.semantic.root);
	u_space_overseer_create_offset_space(uso, uso->base.semantic.root, local_offset, &uso->base.semantic.local);
	if (head != NULL) {
		u_space_overseer_create_pose_space(uso, head, XRT_INPUT_GENERIC_HEAD_POSE, &uso->base.semantic.view);
	}
}

void
u_space_overseer_create_null_space(struct u_space_overseer *uso, struct xrt_space *parent, struct xrt_space **out_space)
{
	assert(out_space != NULL);
	assert(*out_space == NULL);

	struct u_space *uparent = u_space(parent);
	struct u_space *us = create_space(U_SPACE_TYPE_NULL, uparent);

	// Created with one references.
	*out_space = &us->base;
}

void
u_space_overseer_link_space_to_device(struct u_space_overseer *uso, struct xrt_space *xs, struct xrt_device *xdev)
{
	pthread_rwlock_wrlock(&uso->lock);

	void *ptr = NULL;
	uint64_t key = (uint64_t)(intptr_t)xdev;
	u_hashmap_int_find(uso->xdev_map, key, &ptr);
	if (ptr != NULL) {
		U_LOG_W("Device '%s' already have a space attached!", xdev->str);
	}

	// Each xdev needs to add a reference to the space.
	struct xrt_space *new_space = NULL;
	xrt_space_reference(&new_space, xs);

	u_hashmap_int_insert(uso->xdev_map, (uint64_t)(intptr_t)xdev, new_space);

	pthread_rwlock_unlock(&uso->lock);

	// Dereferrence old space outside of lock.
	struct xrt_space *old_space = (struct xrt_space *)ptr;
	xrt_space_reference(&old_space, NULL);
}
