// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Defines for OpenGloves internal inputs
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup drv_opengloves
 */

#pragma once
#include <stdbool.h>

#define OPENGLOVES_ENCODING_MAX_ANALOG_VALUE 1023.0f
#define OPENGLOVES_ENCODING_MAX_PACKET_SIZE 150

#ifdef __cplusplus
extern "C" {
#endif


struct opengloves_input_button
{
	float value;
	bool pressed;
};

struct opengloves_input_joystick
{
	float x;
	float y;
	bool pressed;
};

struct opengloves_input_gesture
{
	bool activated;
};

struct opengloves_input_buttons
{
	struct opengloves_input_button A;
	struct opengloves_input_button B;
	struct opengloves_input_button trigger;
	struct opengloves_input_button menu;
};

struct opengloves_input_joysticks
{
	struct opengloves_input_joystick main;
};

struct opengloves_input_gestures
{
	struct opengloves_input_gesture grab;
	struct opengloves_input_gesture pinch;
};


struct opengloves_input
{
	float flexion[5][5];
	float splay[5];

	struct opengloves_input_joysticks joysticks;
	struct opengloves_input_buttons buttons;
	struct opengloves_input_gestures gestures;
};

struct opengloves_output_force_feedback
{
	float thumb;
	float index;
	float middle;
	float ring;
	float little;
};

struct opengloves_output
{
	struct opengloves_output_force_feedback force_feedback;
};

#ifdef __cplusplus
}
#endif
