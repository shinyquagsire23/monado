// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds input related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "util/u_debug.h"
#include "util/u_time.h"
#include "util/u_misc.h"

#include "xrt/xrt_compiler.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_handle.h"


/*
 *
 * Action set functions
 *
 */

static XrResult
oxr_action_set_destroy_cb(struct oxr_logger* log, struct oxr_handle_base* hb)
{
	//! @todo Move to oxr_action.h
	struct oxr_action_set* act_set = (struct oxr_action_set*)hb;

	free(act_set);

	return XR_SUCCESS;
}

XrResult
oxr_action_set_create(struct oxr_logger* log,
                      struct oxr_session* sess,
                      const XrActionSetCreateInfo* createInfo,
                      struct oxr_action_set** out_act_set)
{
	//! @todo Implement more fully.
	struct oxr_action_set* act_set = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, act_set, OXR_XR_DEBUG_ACTIONSET,
	                              oxr_action_set_destroy_cb, &sess->handle);
	act_set->sess = sess;

	*out_act_set = act_set;

	return XR_SUCCESS;
}


/*
 *
 * Action functions
 *
 */

static XrResult
oxr_action_destroy_cb(struct oxr_logger* log, struct oxr_handle_base* hb)
{
	//! @todo Move to oxr_action.h
	struct oxr_action* act = (struct oxr_action*)hb;

	free(act);

	return XR_SUCCESS;
}

XrResult
oxr_action_create(struct oxr_logger* log,
                  struct oxr_action_set* act_set,
                  const XrActionCreateInfo* createInfo,
                  struct oxr_action** out_act)
{
	//! @todo Implement more fully.
	struct oxr_action* act = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, act, OXR_XR_DEBUG_ACTION,
	                              oxr_action_destroy_cb, &act_set->handle);
	act->act_set = act_set;

	*out_act = act;

	return XR_SUCCESS;
}


/*
 *
 * Session functions.
 *
 */

XrResult
oxr_action_sync_data(struct oxr_logger* log,
                     struct oxr_session* sess,
                     uint32_t countActionSets,
                     const XrActiveActionSet* actionSets)
{
	//! @todo Implement
	return XR_SUCCESS;
}

XrResult
oxr_action_set_interaction_profile_suggested_bindings(
    struct oxr_logger* log,
    struct oxr_session* sess,
    const XrInteractionProfileSuggestedBinding* suggestedBindings)
{
	//! @todo Implement
	return XR_SUCCESS;
}

XrResult
oxr_action_get_current_interaction_profile(
    struct oxr_logger* log,
    struct oxr_session* sess,
    XrPath topLevelUserPath,
    XrInteractionProfileInfo* interactionProfile)
{
	//! @todo Implement
	return oxr_error(log, XR_ERROR_HANDLE_INVALID, " not implemented");
}

XrResult
oxr_action_get_input_source_localized_name(
    struct oxr_logger* log,
    struct oxr_session* sess,
    XrPath source,
    XrInputSourceLocalizedNameFlags whichComponents,
    uint32_t bufferCapacityInput,
    uint32_t* bufferCountOutput,
    char* buffer)
{
	//! @todo Implement
	return oxr_error(log, XR_ERROR_HANDLE_INVALID, " not implemented");
}


/*
 *
 * Action get functions.
 *
 */

XrResult
oxr_action_get_boolean(struct oxr_logger* log,
                       struct oxr_action* act,
                       uint32_t countSubactionPaths,
                       const XrPath* subactionPaths,
                       XrActionStateBoolean* data)
{
	//! @todo Implement
	data->currentState = XR_FALSE;
	data->changedSinceLastSync = XR_FALSE;
	data->lastChangeTime = 0;
	data->isActive = XR_FALSE;

	return XR_SUCCESS;
}

XrResult
oxr_action_get_vector1f(struct oxr_logger* log,
                        struct oxr_action* act,
                        uint32_t countSubactionPaths,
                        const XrPath* subactionPaths,
                        XrActionStateVector1f* data)
{
	//! @todo Implement
	data->currentState = 0.0f;
	data->changedSinceLastSync = XR_FALSE;
	data->lastChangeTime = 0;
	data->isActive = XR_FALSE;

	return XR_SUCCESS;
}

XrResult
oxr_action_get_vector2f(struct oxr_logger* log,
                        struct oxr_action* act,
                        uint32_t countSubactionPaths,
                        const XrPath* subactionPaths,
                        XrActionStateVector2f* data)
{
	//! @todo Implement
	data->currentState.x = 0.0f;
	data->currentState.y = 0.0f;
	data->changedSinceLastSync = XR_FALSE;
	data->lastChangeTime = 0;
	data->isActive = XR_FALSE;

	return XR_SUCCESS;
}

XrResult
oxr_action_get_pose(struct oxr_logger* log,
                    struct oxr_action* act,
                    XrPath subactionPath,
                    XrActionStatePose* data)
{
	//! @todo Implement
	data->isActive = XR_FALSE;

	return XR_SUCCESS;
}

XrResult
oxr_action_get_bound_sources(struct oxr_logger* log,
                             struct oxr_action* act,
                             uint32_t sourceCapacityInput,
                             uint32_t* sourceCountOutput,
                             XrPath* sources)
{
	//! @todo Implement
	return oxr_error(log, XR_ERROR_HANDLE_INVALID, " not implemented");
}


/*
 *
 * Haptic feedback functions.
 *
 */

XrResult
oxr_action_apply_haptic_feedback(struct oxr_logger* log,
                                 struct oxr_action* act,
                                 uint32_t countSubactionPaths,
                                 const XrPath* subactionPaths,
                                 const XrHapticBaseHeader* hapticEvent)
{
	//! @todo Implement
	return oxr_error(log, XR_ERROR_HANDLE_INVALID, " not implemented");
}

XrResult
oxr_action_stop_haptic_feedback(struct oxr_logger* log,
                                struct oxr_action* act,
                                uint32_t countSubactionPaths,
                                const XrPath* subactionPaths)
{
	//! @todo Implement
	return oxr_error(log, XR_ERROR_HANDLE_INVALID, " not implemented");
}
