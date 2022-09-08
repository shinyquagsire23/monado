// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGloves Alpha Encoding Decoding implementation.
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup drv_opengloves
 */

#include <string>
#include <stdexcept>

#include <map>
#include "util/u_logging.h"

#include "alpha_encoding.h"
#include "encoding.h"

enum opengloves_alpha_encoding_key
{
	OPENGLOVES_ALPHA_ENCODING_FinThumb,
	OPENGLOVES_ALPHA_ENCODING_FinSplayThumb,

	OPENGLOVES_ALPHA_ENCODING_FinIndex,
	OPENGLOVES_ALPHA_ENCODING_FinSplayIndex,

	OPENGLOVES_ALPHA_ENCODING_FinMiddle,
	OPENGLOVES_ALPHA_ENCODING_FinSplayMiddle,

	OPENGLOVES_ALPHA_ENCODING_FinRing,
	OPENGLOVES_ALPHA_ENCODING_FinSplayRing,

	OPENGLOVES_ALPHA_ENCODING_FinPinky,
	OPENGLOVES_ALPHA_ENCODING_FinSplayPinky,

	OPENGLOVES_ALPHA_ENCODING_FinJointThumb0,
	OPENGLOVES_ALPHA_ENCODING_FinJointThumb1,
	OPENGLOVES_ALPHA_ENCODING_FinJointThumb2,
	OPENGLOVES_ALPHA_ENCODING_FinJointThumb3, // unused in input but used for parity to other fingers in the array


	OPENGLOVES_ALPHA_ENCODING_FinJointIndex0,
	OPENGLOVES_ALPHA_ENCODING_FinJointIndex1,
	OPENGLOVES_ALPHA_ENCODING_FinJointIndex2,
	OPENGLOVES_ALPHA_ENCODING_FinJointIndex3,


	OPENGLOVES_ALPHA_ENCODING_FinJointMiddle0,
	OPENGLOVES_ALPHA_ENCODING_FinJointMiddle1,
	OPENGLOVES_ALPHA_ENCODING_FinJointMiddle2,
	OPENGLOVES_ALPHA_ENCODING_FinJointMiddle3,


	OPENGLOVES_ALPHA_ENCODING_FinJointRing0,
	OPENGLOVES_ALPHA_ENCODING_FinJointRing1,
	OPENGLOVES_ALPHA_ENCODING_FinJointRing2,
	OPENGLOVES_ALPHA_ENCODING_FinJointRing3,


	OPENGLOVES_ALPHA_ENCODING_FinJointPinky0,
	OPENGLOVES_ALPHA_ENCODING_FinJointPinky1,
	OPENGLOVES_ALPHA_ENCODING_FinJointPinky2,
	OPENGLOVES_ALPHA_ENCODING_FinJointPinky3,

	OPENGLOVES_ALPHA_ENCODING_JoyX,
	OPENGLOVES_ALPHA_ENCODING_JoyY,
	OPENGLOVES_ALPHA_ENCODING_JoyBtn,

	OPENGLOVES_ALPHA_ENCODING_TrgValue,
	OPENGLOVES_ALPHA_ENCODING_BtnTrg,
	OPENGLOVES_ALPHA_ENCODING_BtnA,
	OPENGLOVES_ALPHA_ENCODING_BtnB,

	OPENGLOVES_ALPHA_ENCODING_GesGrab,
	OPENGLOVES_ALPHA_ENCODING_GesPinch,

	OPENGLOVES_ALPHA_ENCODING_BtnMenu,
	OPENGLOVES_ALPHA_ENCODING_BtnCalib,

	OPENGLOVES_ALPHA_ENCODING_MAX
};

#define OPENGLOVES_ALPHA_ENCODING_VAL_IN_MAP_E_0

static const std::string opengloves_alpha_encoding_key_characters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ()";

static bool
opengloves_alpha_encoding_is_key_character(const char character)
{
	return opengloves_alpha_encoding_key_characters.find(character) != std::string::npos;
}

static const std::map<std::string, int> opengloves_alpha_encoding_input_key_string{
    {"A", OPENGLOVES_ALPHA_ENCODING_FinThumb},         // whole thumb curl (default curl value for thumb joints)
    {"(AB)", OPENGLOVES_ALPHA_ENCODING_FinSplayThumb}, // whole thumb splay thumb joint 3 (doesn't exist, but keeps
                                                       // consistency with the other fingers
    {"B", OPENGLOVES_ALPHA_ENCODING_FinIndex},         // whole index curl (default curl value for index joints)
    {"(BB)", OPENGLOVES_ALPHA_ENCODING_FinSplayIndex}, // whole index splay

    {"C", OPENGLOVES_ALPHA_ENCODING_FinMiddle},         // whole middle curl (default curl value for middle joints)
    {"(CB)", OPENGLOVES_ALPHA_ENCODING_FinSplayMiddle}, // whole middle splay

    {"D", OPENGLOVES_ALPHA_ENCODING_FinRing},            // whole ring curl (default curl value for
    {"(DB)", OPENGLOVES_ALPHA_ENCODING_FinSplayRing},    // whole ring splay
                                                         // ring joints)
    {"E", OPENGLOVES_ALPHA_ENCODING_FinPinky},           // whole pinky curl (default curl value
    {"(EB)", OPENGLOVES_ALPHA_ENCODING_FinSplayPinky},   // whole pinky splay
                                                         // for pinky joints
    {"(AAA)", OPENGLOVES_ALPHA_ENCODING_FinJointThumb0}, // thumb joint 0
    {"(AAB)", OPENGLOVES_ALPHA_ENCODING_FinJointThumb1}, // thumb joint 1
    {"(AAC)", OPENGLOVES_ALPHA_ENCODING_FinJointThumb2}, // thumb joint 2
    {"(AAD)", OPENGLOVES_ALPHA_ENCODING_FinJointThumb3},
    {"(BAA)", OPENGLOVES_ALPHA_ENCODING_FinJointIndex0},  // index joint 0
    {"(BAB)", OPENGLOVES_ALPHA_ENCODING_FinJointIndex1},  // index joint 1
    {"(BAC)", OPENGLOVES_ALPHA_ENCODING_FinJointIndex2},  // index joint 2
    {"(BAD)", OPENGLOVES_ALPHA_ENCODING_FinJointIndex3},  // index joint 3
    {"(CAA)", OPENGLOVES_ALPHA_ENCODING_FinJointMiddle0}, // middle joint 0
    {"(CAB)", OPENGLOVES_ALPHA_ENCODING_FinJointMiddle1}, // middle joint 1
    {"(CAC)", OPENGLOVES_ALPHA_ENCODING_FinJointMiddle2}, // middle joint 2
    {"(CAD)", OPENGLOVES_ALPHA_ENCODING_FinJointMiddle3}, // middle joint 3
    {"(DAA)", OPENGLOVES_ALPHA_ENCODING_FinJointRing0},   // ring joint 0
    {"(DAB)", OPENGLOVES_ALPHA_ENCODING_FinJointRing1},   // ring joint 1
    {"(DAC)", OPENGLOVES_ALPHA_ENCODING_FinJointRing2},   // ring joint 2
    {"(DAD)", OPENGLOVES_ALPHA_ENCODING_FinJointRing3},   // ring joint 3
    {"(EAA)", OPENGLOVES_ALPHA_ENCODING_FinJointPinky0},  // pinky joint 0
    {"(EAB)", OPENGLOVES_ALPHA_ENCODING_FinJointPinky1},  // pinky joint 1
    {"(EAC)", OPENGLOVES_ALPHA_ENCODING_FinJointPinky2},  // pinky joint 2
    {"(EAD)", OPENGLOVES_ALPHA_ENCODING_FinJointPinky3},  // pinky joint 3
    {"F", OPENGLOVES_ALPHA_ENCODING_JoyX},                // joystick x component
    {"G", OPENGLOVES_ALPHA_ENCODING_JoyY},                // joystick y component
    {"H", OPENGLOVES_ALPHA_ENCODING_JoyBtn},              // joystick button
    {"I", OPENGLOVES_ALPHA_ENCODING_BtnTrg},              // trigger button
    {"J", OPENGLOVES_ALPHA_ENCODING_BtnA},                // A button
    {"K", OPENGLOVES_ALPHA_ENCODING_BtnB},                // B button
    {"L", OPENGLOVES_ALPHA_ENCODING_GesGrab},             // grab gesture (boolean)
    {"M", OPENGLOVES_ALPHA_ENCODING_GesPinch},            // pinch gesture (boolean)
    {"N", OPENGLOVES_ALPHA_ENCODING_BtnMenu},             // system button pressed (opens SteamVR menu)
    {"O", OPENGLOVES_ALPHA_ENCODING_BtnCalib},            // calibration button
    {"P", OPENGLOVES_ALPHA_ENCODING_TrgValue},            // analog trigger value
    {"", OPENGLOVES_ALPHA_ENCODING_MAX}                   // Junk key
};

static const std::map<int, std::string> opengloves_alpha_encoding_output_key_string{
    {OPENGLOVES_ALPHA_ENCODING_FinThumb, "A"},  // thumb force feedback
    {OPENGLOVES_ALPHA_ENCODING_FinIndex, "B"},  // index force feedback
    {OPENGLOVES_ALPHA_ENCODING_FinMiddle, "C"}, // middle force feedback
    {OPENGLOVES_ALPHA_ENCODING_FinRing, "D"},   // ring force feedback
    {OPENGLOVES_ALPHA_ENCODING_FinPinky, "E"},  // pinky force feedback
};

static std::map<int, std::string>
opengloves_alpha_encoding_parse_to_map(const std::string &str)
{
	std::map<int, std::string> result;

	size_t i = 0;
	while (i < str.length()) {
		// Advance until we get an alphabetic character (no point in looking at values that don't have a key
		// associated with them)

		if (str[i] >= 0 && opengloves_alpha_encoding_is_key_character(str[i])) {
			std::string key = {str[i]};
			i++;

			// we're going to be parsing a "long key", i.e. (AB) for thumb finger splay. Long keys must
			// always be enclosed in brackets
			if (key[0] == '(') {
				while (str[i] >= 0 && opengloves_alpha_encoding_is_key_character(str[i]) &&
				       i < str.length()) {
					key += str[i];
					i++;
				}
			}

			std::string value;
			while (str[i] >= 0 && isdigit(str[i]) && i < str.length()) {
				value += str[i];
				i++;
			}

			// Even if the value is empty we still want to use the key, it means that we have a button that
			// is pressed (it only appears in the packet if it is)
			if (opengloves_alpha_encoding_input_key_string.find(key) !=
			    opengloves_alpha_encoding_input_key_string.end())
				result.insert_or_assign(opengloves_alpha_encoding_input_key_string.at(key), value);
			else
				U_LOG_W("Unable to insert key: %s into input map as it was not found", key.c_str());
		} else
			i++;
	}

	return result;
}


void
opengloves_alpha_encoding_decode(const char *data, struct opengloves_input *out)
{
	std::map<int, std::string> input_map = opengloves_alpha_encoding_parse_to_map(data);

	try {
		// five fingers, 2 (curl + splay)
		for (int i = 0; i < 5; i++) {
			int enum_position = i * 2;
			// curls
			if (input_map.find(enum_position) != input_map.end()) {
				float fin_curl_value = std::stof(input_map.at(enum_position));
				std::fill(std::begin(out->flexion[i]), std::begin(out->flexion[i]) + 4,
				          fin_curl_value / OPENGLOVES_ENCODING_MAX_ANALOG_VALUE);
			}

			// splay
			if (input_map.find(enum_position + 1) != input_map.end())
				out->splay[i] =
				    (std::stof(input_map.at(enum_position + 1)) / OPENGLOVES_ENCODING_MAX_ANALOG_VALUE -
				     0.5f) *
				    2.0f;
		}

		int current_finger_joint = OPENGLOVES_ALPHA_ENCODING_FinJointThumb0;
		for (int i = 0; i < 5; i++) {
			for (int j = 0; j < 4; j++) {
				// individual joint curls
				out->flexion[i][j] = input_map.find(current_finger_joint) != input_map.end()
				                         ? (std::stof(input_map.at(current_finger_joint)) /
				                            OPENGLOVES_ENCODING_MAX_ANALOG_VALUE)
				                         // use the curl of the previous joint
				                         : out->flexion[i][j > 0 ? j - 1 : 0];
				current_finger_joint++;
			}
		}

		// joysticks
		if (input_map.find(OPENGLOVES_ALPHA_ENCODING_JoyX) != input_map.end())
			out->joysticks.main.x = 2 * std::stof(input_map.at(OPENGLOVES_ALPHA_ENCODING_JoyX)) /
			                            OPENGLOVES_ENCODING_MAX_ANALOG_VALUE -
			                        1;
		if (input_map.find(OPENGLOVES_ALPHA_ENCODING_JoyY) != input_map.end())
			out->joysticks.main.y = 2 * std::stof(input_map.at(OPENGLOVES_ALPHA_ENCODING_JoyY)) /
			                            OPENGLOVES_ENCODING_MAX_ANALOG_VALUE -
			                        1;
		out->joysticks.main.pressed = input_map.find(OPENGLOVES_ALPHA_ENCODING_JoyBtn) != input_map.end();

	} catch (std::invalid_argument &e) {
		U_LOG_E("Error parsing input string: %s", e.what());
	}

	if (input_map.find(OPENGLOVES_ALPHA_ENCODING_TrgValue) != input_map.end())
		out->buttons.trigger.value =
		    std::stof(input_map.at(OPENGLOVES_ALPHA_ENCODING_TrgValue)) / OPENGLOVES_ENCODING_MAX_ANALOG_VALUE;
	out->buttons.trigger.pressed = input_map.find(OPENGLOVES_ALPHA_ENCODING_BtnTrg) != input_map.end();

	out->buttons.A.pressed = input_map.find(OPENGLOVES_ALPHA_ENCODING_BtnA) != input_map.end();
	out->buttons.B.pressed = input_map.find(OPENGLOVES_ALPHA_ENCODING_BtnB) != input_map.end();
	out->gestures.grab.activated = input_map.find(OPENGLOVES_ALPHA_ENCODING_GesGrab) != input_map.end();
	out->gestures.pinch.activated = input_map.find(OPENGLOVES_ALPHA_ENCODING_GesPinch) != input_map.end();
	out->buttons.menu.pressed = input_map.find(OPENGLOVES_ALPHA_ENCODING_BtnMenu) != input_map.end();
}

void
opengloves_alpha_encoding_encode(const struct opengloves_output *output, char *out_buff)
{
	sprintf(out_buff, "%s%d%s%d%s%d%s%d%s%d\n",
	        opengloves_alpha_encoding_output_key_string.at(OPENGLOVES_ALPHA_ENCODING_FinThumb).c_str(),
	        (int)(output->force_feedback.thumb * 1000),
	        opengloves_alpha_encoding_output_key_string.at(OPENGLOVES_ALPHA_ENCODING_FinIndex).c_str(),
	        (int)(output->force_feedback.index * 1000),
	        opengloves_alpha_encoding_output_key_string.at(OPENGLOVES_ALPHA_ENCODING_FinMiddle).c_str(),
	        (int)(output->force_feedback.middle * 1000),
	        opengloves_alpha_encoding_output_key_string.at(OPENGLOVES_ALPHA_ENCODING_FinRing).c_str(),
	        (int)(output->force_feedback.ring * 1000),
	        opengloves_alpha_encoding_output_key_string.at(OPENGLOVES_ALPHA_ENCODING_FinPinky).c_str(),
	        (int)(output->force_feedback.little * 1000));
}
