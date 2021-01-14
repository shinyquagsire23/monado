// Copyright 2018-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds event related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include "os/os_threading.h"

#include "util/u_misc.h"

#include "oxr_objects.h"
#include "oxr_logger.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>


/*
 *
 * Struct and defines.
 *
 */

struct oxr_event
{
	struct oxr_event *next;
	size_t length;
	XrResult result;
};


/*
 *
 * Internal helpers.
 *
 */

void
lock(struct oxr_instance *inst)
{
	os_mutex_lock(&inst->event.mutex);
}

void
unlock(struct oxr_instance *inst)
{
	os_mutex_unlock(&inst->event.mutex);
}

void *
oxr_event_extra(struct oxr_event *event)
{
	return &event[1];
}

struct oxr_event *
pop(struct oxr_instance *inst)
{
	struct oxr_event *ret = inst->event.next;
	if (ret == NULL) {
		return NULL;
	}

	inst->event.next = ret->next;
	ret->next = NULL;

	if (ret == inst->event.last) {
		inst->event.last = NULL;
	}

	return ret;
}

void
push(struct oxr_instance *inst, struct oxr_event *event)
{
	struct oxr_event *last = inst->event.last;
	if (last != NULL) {
		last->next = event;
	}
	inst->event.last = event;

	if (inst->event.next == NULL) {
		inst->event.next = event;
	}
}

#define ALLOC(log, inst, event, extra)                                                                                 \
	do {                                                                                                           \
		XrResult ret = oxr_event_alloc(log, inst, sizeof(**extra), event);                                     \
		if (ret != XR_SUCCESS) {                                                                               \
			return ret;                                                                                    \
		}                                                                                                      \
		*((void **)extra) = oxr_event_extra(*event);                                                           \
	} while (false)

static XrResult
oxr_event_alloc(struct oxr_logger *log, struct oxr_instance *inst, size_t size, struct oxr_event **out_event)
{
	struct oxr_event *event = U_CALLOC_WITH_CAST(struct oxr_event, sizeof(struct oxr_event) + size);

	if (event == NULL) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Out of memory");
	}

	event->next = NULL;
	event->length = size;
	event->result = XR_SUCCESS;

	*out_event = event;

	return XR_SUCCESS;
}

static bool
is_session_link_to_event(struct oxr_event *event, XrSession session)
{
	XrStructureType *type = oxr_event_extra(event);

	switch (*type) {
	case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
		XrEventDataSessionStateChanged *changed = (XrEventDataSessionStateChanged *)type;
		return changed->session == session;
	}
	case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
		XrEventDataInteractionProfileChanged *changed = (XrEventDataInteractionProfileChanged *)type;
		return changed->session == session;
	}
	default: return false;
	}
}

/*
 *
 * 'Exported' functions.
 *
 */

XrResult
oxr_event_push_XrEventDataSessionStateChanged(struct oxr_logger *log,
                                              struct oxr_session *sess,
                                              XrSessionState state,
                                              XrTime time)
{
	struct oxr_instance *inst = sess->sys->inst;
	XrEventDataSessionStateChanged *changed;
	struct oxr_event *event = NULL;

	ALLOC(log, inst, &event, &changed);

	changed->type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
	changed->session = oxr_session_to_openxr(sess);
	changed->state = state;
	changed->time = time;

	event->result = state == XR_SESSION_STATE_LOSS_PENDING ? XR_SESSION_LOSS_PENDING : XR_SUCCESS;

	lock(inst);
	push(inst, event);
	unlock(inst);

	return XR_SUCCESS;
}


XrResult
oxr_event_push_XrEventDataInteractionProfileChanged(struct oxr_logger *log, struct oxr_session *sess)
{
	struct oxr_instance *inst = sess->sys->inst;
	XrEventDataSessionStateChanged *changed;
	struct oxr_event *event = NULL;

	ALLOC(log, inst, &event, &changed);

	changed->type = XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED;
	changed->session = oxr_session_to_openxr(sess);

	lock(inst);
	push(inst, event);
	unlock(inst);

	return XR_SUCCESS;
}

XrResult
oxr_event_push_XrEventDataMainSessionVisibilityChangedEXTX(struct oxr_logger *log,
                                                           struct oxr_session *sess,
                                                           bool visible)
{
	struct oxr_instance *inst = sess->sys->inst;
	XrEventDataMainSessionVisibilityChangedEXTX *changed;
	struct oxr_event *event = NULL;

	ALLOC(log, inst, &event, &changed);
	changed->type = XR_TYPE_EVENT_DATA_MAIN_SESSION_VISIBILITY_CHANGED_EXTX;
	changed->flags = 0;
	changed->visible = visible;
	event->result = XR_SUCCESS;
	lock(inst);
	push(inst, event);
	unlock(inst);

	return XR_SUCCESS;
}

XrResult
oxr_event_remove_session_events(struct oxr_logger *log, struct oxr_session *sess)
{
	struct oxr_instance *inst = sess->sys->inst;
	XrSession session = oxr_session_to_openxr(sess);

	lock(inst);

	struct oxr_event *e = inst->event.next;
	while (e != NULL) {
		struct oxr_event *cur = e;
		e = e->next;

		if (!is_session_link_to_event(cur, session)) {
			continue;
		}

		if (cur == inst->event.next) {
			inst->event.next = cur->next;
		}

		if (cur == inst->event.last) {
			inst->event.last = NULL;
		}
		free(cur);
	}

	unlock(inst);

	return XR_SUCCESS;
}

XrResult
oxr_poll_event(struct oxr_logger *log, struct oxr_instance *inst, XrEventDataBuffer *eventData)
{
	struct oxr_session *sess = inst->sessions;
	while (sess) {
		oxr_session_poll(log, sess);
		sess = sess->next;
	}

	lock(inst);
	struct oxr_event *event = pop(inst);
	unlock(inst);

	if (event == NULL) {
		return XR_EVENT_UNAVAILABLE;
	}

	XrResult ret = event->result;
	memcpy(eventData, oxr_event_extra(event), event->length);
	free(event);

	return ret;
}
