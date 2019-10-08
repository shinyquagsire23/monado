// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds event related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "util/u_misc.h"

#include "oxr_objects.h"
#include "oxr_logger.h"



struct oxr_event
{
	struct oxr_event *next;
	size_t length;
	XrResult result;
};


void
lock(struct oxr_instance *inst)
{}

void
unlock(struct oxr_instance *inst)
{}

void *
oxr_event_extra(struct oxr_event *event)
{
	return &event[1];
}

struct oxr_event *
pop(struct oxr_instance *inst)
{
	struct oxr_event *ret = inst->next_event;
	if (ret == NULL) {
		return NULL;
	}

	inst->next_event = ret->next;
	ret->next = NULL;

	if (ret == inst->last_event) {
		inst->last_event = NULL;
	}

	return ret;
}

void
push(struct oxr_instance *inst, struct oxr_event *event)
{
	struct oxr_event *last = inst->last_event;
	if (last != NULL) {
		last->next = event;
	}
	inst->last_event = event;

	if (inst->next_event == NULL) {
		inst->next_event = event;
	}
}

#define ALLOC(log, inst, event, extra)                                         \
	do {                                                                   \
		XrResult ret =                                                 \
		    oxr_event_alloc(log, inst, sizeof(**extra), event);        \
		if (ret != XR_SUCCESS) {                                       \
			return ret;                                            \
		}                                                              \
		*((void **)extra) = oxr_event_extra(*event);                   \
	} while (false)

static XrResult
oxr_event_alloc(struct oxr_logger *log,
                struct oxr_instance *inst,
                size_t size,
                struct oxr_event **out_event)
{
	struct oxr_event *event = U_CALLOC_WITH_CAST(
	    struct oxr_event, sizeof(struct oxr_event) + size);

	if (event == NULL) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 " out of memory");
	}

	event->next = NULL;
	event->length = size;
	event->result = XR_SUCCESS;

	*out_event = event;

	return XR_SUCCESS;
}

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

	event->result = state == XR_SESSION_STATE_LOSS_PENDING
	                    ? XR_SESSION_LOSS_PENDING
	                    : XR_SUCCESS;

	lock(inst);
	push(inst, event);
	unlock(inst);

	return XR_SUCCESS;
}

XrResult
oxr_poll_event(struct oxr_logger *log,
               struct oxr_instance *inst,
               XrEventDataBuffer *eventData)
{
	struct oxr_session *sess = inst->sessions;
	while (sess) {
		oxr_session_poll(sess);
		sess = sess->next;
	}

	lock(inst);
	struct oxr_event *event = pop(inst);
	unlock(inst);

	if (event == NULL) {
		return XR_EVENT_UNAVAILABLE;
	}

	memcpy(eventData, oxr_event_extra(event), event->length);
	free(event);

	return event->result;
}
