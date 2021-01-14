// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Provides a utility macro for dealing with subaction paths
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup oxr_main
 */

#pragma once

/*!
 * Expansion macro (x-macro) that calls the macro you pass with the shorthand
 * name of each valid subaction path.
 *
 * Use to generate code that checks each subaction path in sequence, etc.
 *
 * If you also want the bogus subaction path of just plain `/user`, then see
 * OXR_FOR_EACH_SUBACTION_PATH()
 *
 * @note Keep this synchronized with OXR_ACTION_GET_FILLER!
 */
#define OXR_FOR_EACH_VALID_SUBACTION_PATH(_)                                                                           \
	_(left)                                                                                                        \
	_(right)                                                                                                       \
	_(head)                                                                                                        \
	_(gamepad)


/*!
 * Expansion macro (x-macro) that calls the macro you pass with the shorthand
 * name of each subaction path, including just bare `user`.
 *
 * Use to generate code that checks each subaction path in sequence, etc.
 *
 * @note Keep this synchronized with OXR_ACTION_GET_FILLER!
 */
#define OXR_FOR_EACH_SUBACTION_PATH(_)                                                                                 \
	OXR_FOR_EACH_VALID_SUBACTION_PATH(_)                                                                           \
	_(user)

/*!
 * Expansion macro (x-macro) that calls the macro you pass for each valid
 * subaction path, with the shorthand name of each subaction path, the same
 * name capitalized, and the corresponding path string.
 *
 * If you also want the bogus subaction path of just plain `/user`, then see
 * OXR_FOR_EACH_SUBACTION_PATH_DETAILED()
 *
 * Use to generate code that checks each subaction path in sequence, etc.
 *
 * Most of the time you can just use OXR_FOR_EACH_VALID_SUBACTION_PATH() or
 * OXR_FOR_EACH_SUBACTION_PATH()
 */
#define OXR_FOR_EACH_VALID_SUBACTION_PATH_DETAILED(_)                                                                  \
	_(left, LEFT, "/user/hand/left")                                                                               \
	_(right, RIGHT, "/user/hand/right")                                                                            \
	_(head, HEAD, "/user/head")                                                                                    \
	_(gamepad, GAMEPAD, "/user/gamepad")

/*!
 * Expansion macro (x-macro) that calls the macro you pass for each subaction
 * path (including the bare `/user`), with the shorthand name of each subaction
 * path, the same name capitalized, and the corresponding path string.
 *
 * Use to generate code that checks each subaction path in sequence, etc.
 *
 * Most of the time you can just use OXR_FOR_EACH_VALID_SUBACTION_PATH() or
 * OXR_FOR_EACH_SUBACTION_PATH()
 */
#define OXR_FOR_EACH_SUBACTION_PATH_DETAILED(_)                                                                        \
	OXR_FOR_EACH_VALID_SUBACTION_PATH_DETAILED(_)                                                                  \
	_(user, USER, "/user")
