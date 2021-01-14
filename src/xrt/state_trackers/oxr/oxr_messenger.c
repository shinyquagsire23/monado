// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds debug utils/messenger related functions.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup oxr_main
 */

#include <stdlib.h>

#include "util/u_debug.h"
#include "util/u_misc.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_handle.h"

static XrResult
oxr_messenger_destroy(struct oxr_logger *log, struct oxr_handle_base *hb)
{
	struct oxr_debug_messenger *mssngr = (struct oxr_debug_messenger *)hb;
	struct oxr_instance *inst = mssngr->inst;

	/*
	 * Instances keep typed pointers to messengers around too.
	 * Remove ourselves.
	 */
	for (size_t i = 0; i < XRT_MAX_HANDLE_CHILDREN; ++i) {
		if (inst->messengers[i] == mssngr) {
			inst->messengers[i] = NULL;
			free(mssngr);
			return XR_SUCCESS;
		}
	}
	return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, " debug messenger not found in parent instance");
}

//! @todo call into inst to create this instead?
XrResult
oxr_create_messenger(struct oxr_logger *log,
                     struct oxr_instance *inst,
                     const XrDebugUtilsMessengerCreateInfoEXT *createInfo,
                     struct oxr_debug_messenger **out_mssngr)
{

	struct oxr_debug_messenger **parent_slot = NULL;
	for (size_t i = 0; i < XRT_MAX_HANDLE_CHILDREN; ++i) {
		if (inst->messengers[i] == NULL) {
			parent_slot = &(inst->messengers[i]);
			break;
		}
	}
	if (parent_slot == NULL) {
		return oxr_error(log, XR_ERROR_LIMIT_REACHED, " Instance cannot hold any more debug messengers");
	}

	struct oxr_debug_messenger *mssngr = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, mssngr, OXR_XR_DEBUG_MESSENGER, oxr_messenger_destroy, &inst->handle);

	mssngr->inst = inst;
	mssngr->message_severities = createInfo->messageSeverities;
	mssngr->message_types = createInfo->messageTypes;
	mssngr->user_callback = createInfo->userCallback;
	mssngr->user_data = createInfo->userData;

	*parent_slot = mssngr;

	*out_mssngr = mssngr;
	return XR_SUCCESS;
}
