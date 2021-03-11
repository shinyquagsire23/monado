// Copyright 2021, Mateo de Mayo.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Internal header for qwerty_device and its friends.
 * @author Mateo de Mayo <mateodemayo@gmail.com>
 * @ingroup drv_qwerty
 */
#pragma once

#include "xrt/xrt_device.h"

#define QWERTY_HMD_STR "Qwerty HMD"
#define QWERTY_HMD_TRACKER_STR QWERTY_HMD_STR " Tracker"
#define QWERTY_LEFT_STR "Qwerty Left Controller"
#define QWERTY_LEFT_TRACKER_STR QWERTY_LEFT_STR " Tracker"
#define QWERTY_RIGHT_STR "Qwerty Right Controller"
#define QWERTY_RIGHT_TRACKER_STR QWERTY_RIGHT_STR " Tracker"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @addtogroup drv_qwerty
 * @{
 */

//! Fake device that modifies its tracked pose through its methods.
//! @implements xrt_device
struct qwerty_device
{
	struct xrt_device base;
	struct xrt_pose pose; //!< Internal pose state

	float movement_speed; //!< In meters per frame
	bool left_pressed;
	bool right_pressed;
	bool forward_pressed;
	bool backward_pressed;
	bool up_pressed;
	bool down_pressed;

	float look_speed; //!< In radians per frame
	bool look_left_pressed;
	bool look_right_pressed;
	bool look_up_pressed;
	bool look_down_pressed;

	bool sprint_pressed; //!< Movement speed boost
	float yaw_delta;   //!< How much extra yaw to add for the next pose. Then reset to 0.
	float pitch_delta; //!< Similar to `yaw_delta`
};

//! @implements qwerty_device
struct qwerty_hmd
{
	struct qwerty_device base;
};

//! @implements qwerty_device
struct qwerty_controller
{
	struct qwerty_device base;
};

/*!
 * @name Qwerty Device
 * @memberof qwerty_device
 * qwerty_device public methods
 * @{
 */
//! @public @memberof qwerty_device <!-- Trick for doxygen -->

//! Cast to qwerty_device. Ensures returning a valid device or crashing.
struct qwerty_device *
qwerty_device(struct xrt_device *xd);

// clang-format off
void qwerty_press_left(struct qwerty_device *qd);
void qwerty_release_left(struct qwerty_device *qd);
void qwerty_press_right(struct qwerty_device *qd);
void qwerty_release_right(struct qwerty_device *qd);
void qwerty_press_forward(struct qwerty_device *qd);
void qwerty_release_forward(struct qwerty_device *qd);
void qwerty_press_backward(struct qwerty_device *qd);
void qwerty_release_backward(struct qwerty_device *qd);
void qwerty_press_up(struct qwerty_device *qd);
void qwerty_release_up(struct qwerty_device *qd);
void qwerty_press_down(struct qwerty_device *qd);
void qwerty_release_down(struct qwerty_device *qd);

void qwerty_press_look_left(struct qwerty_device *qd);
void qwerty_release_look_left(struct qwerty_device *qd);
void qwerty_press_look_right(struct qwerty_device *qd);
void qwerty_release_look_right(struct qwerty_device *qd);
void qwerty_press_look_up(struct qwerty_device *qd);
void qwerty_release_look_up(struct qwerty_device *qd);
void qwerty_press_look_down(struct qwerty_device *qd);
void qwerty_release_look_down(struct qwerty_device *qd);
// clang-format on

//! Momentarily increase `movement_speed` until `qwerty_release_sprint()`
void
qwerty_press_sprint(struct qwerty_device *qd);

void
qwerty_release_sprint(struct qwerty_device *qd);

//! Add yaw and pitch movement for the next frame
void
qwerty_add_look_delta(struct qwerty_device *qd, float yaw, float pitch);

//! Change movement speed in exponential steps (usually integers, but any float allowed)
void
qwerty_change_movement_speed(struct qwerty_device *qd, float steps);

/*!
 * @}
 */

/*!
 * @name Qwerty HMD
 * @memberof qwerty_hmd
 * qwerty_hmd public methods
 * @{
 */
//! @public @memberof qwerty_hmd <!-- Trick for doxygen -->

//! Create qwerty_hmd. Crash on failure.
struct qwerty_hmd *
qwerty_hmd_create(void);

/*!
 * @}
 */

/*!
 * @name Qwerty Controller
 * @memberof qwerty_controller
 * qwerty_controller public methods
 * @{
 */
//! @public @memberof qwerty_controller <!-- Trick for doxygen -->

//! Create qwerty_controller. Crash on failure.
struct qwerty_controller *
qwerty_controller_create(bool is_left, struct qwerty_hmd *qhmd);

/*!
 * @}
 */

/*!
 * @}
 */

#ifdef __cplusplus
}
#endif
