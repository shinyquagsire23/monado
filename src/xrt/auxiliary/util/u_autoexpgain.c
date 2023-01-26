// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Automatically compute exposure and gain values from an image stream
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup aux_util
 */

#include "math/m_api.h"
#include "util/u_autoexpgain.h"
#include "util/u_debug.h"
#include "util/u_format.h"
#include "util/u_logging.h"
#include "util/u_misc.h"
#include "util/u_var.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

DEBUG_GET_ONCE_LOG_OPTION(aeg_log, "AEG_LOG", U_LOGGING_WARN)

#define AEG_TRACE(...) U_LOG_IFL_T(aeg->log_level, __VA_ARGS__)
#define AEG_DEBUG(...) U_LOG_IFL_D(aeg->log_level, __VA_ARGS__)
#define AEG_INFO(...) U_LOG_IFL_I(aeg->log_level, __VA_ARGS__)
#define AEG_WARN(...) U_LOG_IFL_W(aeg->log_level, __VA_ARGS__)
#define AEG_ERROR(...) U_LOG_IFL_E(aeg->log_level, __VA_ARGS__)
#define AEG_ASSERT(predicate, ...)                                                                                     \
	do {                                                                                                           \
		bool p = predicate;                                                                                    \
		if (!p) {                                                                                              \
			U_LOG(U_LOGGING_ERROR, __VA_ARGS__);                                                           \
			assert(false && "AEG_ASSERT failed: " #predicate);                                             \
			exit(EXIT_FAILURE);                                                                            \
		}                                                                                                      \
	} while (false);
#define AEG_ASSERT_(predicate) AEG_ASSERT(predicate, "Assertion failed " #predicate)

#define LEVELS 256 //!< Possible pixel intensity values, only 8-bit supported
#define INITIAL_BRIGHTNESS 0.5
#define INITIAL_MAX_BRIGHTNESS_STEP 0.1
#define INITIAL_THRESHOLD 0.1
#define GRID_COLS 32 //!< Amount of columns for the histogram sample grid

//! AEG State machine states
enum u_aeg_state
{
	IDLE = 0,
	BRIGHTEN,
	STOP_BRIGHTEN, //!< Avoid oscillations by
	DARKEN,
	STOP_DARKEN, //!< Similar to STOP_BRIGHTEN
};

//! This actions are triggered when the image is too dark, bright or good enough
enum u_aeg_action
{
	GOOD = 0,
	DARK,
	BRIGHT,
};

//! Auto exposure and gain (AEG) adjustment algorithm state.
struct u_autoexpgain
{
	bool enable; //!< Whether to enable auto exposure and gain adjustment

	//! AEG is a finite state machine. @see set_state.
	enum u_aeg_state state;

	enum u_logging_level log_level;

	//! Counts how many times we've overshooted in the last brightness change.
	//! It's then used for exponential backoff of the brightness step.
	int overshoots;

	//! There are buffer states that wait `frame_delay` frames to ensure we are
	//! not overshooting. This field counts the remaining frames to wait.
	//! @see set_state
	int wait;

	//! The selected strategy affects various targets of the algorithm.
	enum u_aeg_strategy strategy;
	struct u_var_combo strategy_combo; //!< UI combo box for selecting `strategy`

	float histogram[LEVELS];                 //!< Pixel intensity histogram
	struct u_var_histogram_f32 histogram_ui; //!< UI for `histogram`

	//! This is a made up scalar that lives in the [0, 1] range. 0 maps to minimum
	//! exp/gain values while 1 to their maximums. An autoexposure strategy limits
	//! itself to modify this value. The mapping between the scalar and the
	//! respective exp/gain values is provided by `brightness_to_expgain`.
	struct u_var_draggable_f32 brightness;
	float last_brightness;     //!< Triggers a exp/gain update when it differs
	float max_brightness_step; //!< Max `brightness` step for each update

	//! The AEG score lives in the [-1, +1] range and represents how dark or
	//! bright this image is. Values close to zero (by `threshold`) represent
	//! images with a good enough `brightness` value.
	float current_score;

	//! Scores further than `threshold` from the target score will trigger a
	//! `brightness` update.
	float threshold;

	//! A camera might take a couple of frames until the new exposure/gain sets in
	//! the image. Knowing how many (this variable) helps in avoiding overshooting
	//! brightness changes.
	int frame_delay;

	float exposure; //!< Currently computed exposure value to use
	float gain;     //!< Currently computed gain value to use
};

static const char *
state_to_string(enum u_aeg_state state)
{
	if (state == IDLE) {
		return "IDLE";
	} else if (state == BRIGHTEN) {
		return "BRIGHTEN";
	} else if (state == STOP_BRIGHTEN) {
		return "STOP_BRIGHTEN";
	} else if (state == DARKEN) {
		return "DARKEN";
	} else if (state == STOP_DARKEN) {
		return "STOP_DARKEN";
	} else {
		AEG_ASSERT_(false);
	}
	return NULL;
}

static const char *
action_to_string(enum u_aeg_action action)
{
	if (action == DARK) {
		return "DARK";
	} else if (action == BRIGHT) {
		return "BRIGHT";
	} else if (action == GOOD) {
		return "GOOD";
	} else {
		AEG_ASSERT_(false);
	}
	return NULL;
}

/*!
 * Defines the AEG state machine transitions.
 * The main idea is that if brightness needs to change then we go from `IDLE` to
 * `BRIGHTEN`/`DARKEN`. To avoid oscillations we detect overshootings
 * and exponentially backoff our brightness step. We only reset our `overshoots`
 * counter after the image have been good for `frame_delay` frames, this delay
 * is counted during `STOP_DARKEN`/`STOP_BRIGHTEN` states.
 *
 * A diagram of the state machine is below:
 * ![AEG state machine](images/autoexpgain.drawio.svg)
 */
static void
set_state(struct u_autoexpgain *aeg, enum u_aeg_action action)
{
	enum u_aeg_state new_state = -1;
	if (aeg->state == IDLE) {
		if (action == DARK) {
			new_state = BRIGHTEN;
		} else if (action == BRIGHT) {
			new_state = DARKEN;
		} else if (action == GOOD) {
			new_state = IDLE;
		} else {
			AEG_ASSERT_(false);
		}
	} else if (aeg->state == BRIGHTEN) {
		if (action == DARK) {
			new_state = BRIGHTEN;
		} else if (action == BRIGHT) {
			aeg->overshoots++;
			new_state = DARKEN;
		} else if (action == GOOD) {
			new_state = STOP_BRIGHTEN;
		} else {
			AEG_ASSERT_(false);
		}
	} else if (aeg->state == STOP_BRIGHTEN) {
		if (action == DARK) {
			new_state = BRIGHTEN;
		} else if (action == BRIGHT) {
			aeg->overshoots++;
			new_state = DARKEN;
		} else if (action == GOOD) {
			aeg->wait--;
			new_state = aeg->wait == 0 ? IDLE : STOP_BRIGHTEN;
		} else {
			AEG_ASSERT_(false);
		}

		if (new_state != STOP_BRIGHTEN) {
			aeg->wait = aeg->frame_delay;
		}
	} else if (aeg->state == DARKEN) {
		if (action == DARK) {
			aeg->overshoots++;
			new_state = BRIGHTEN;
		} else if (action == BRIGHT) {
			new_state = DARKEN;
		} else if (action == GOOD) {
			new_state = STOP_DARKEN;
		} else {
			AEG_ASSERT_(false);
		}
	} else if (aeg->state == STOP_DARKEN) {
		if (action == DARK) {
			aeg->overshoots++;
			new_state = BRIGHTEN;
		} else if (action == BRIGHT) {
			new_state = DARKEN;
		} else if (action == GOOD) {
			aeg->wait--;
			new_state = aeg->wait == 0 ? IDLE : STOP_DARKEN;
		} else {
			AEG_ASSERT_(false);
		}

		if (new_state != STOP_DARKEN) {
			aeg->wait = aeg->frame_delay;
		}
	} else {
		AEG_ASSERT_(false);
	}
	if (new_state == IDLE) {
		aeg->overshoots = 0;
	}
	aeg->overshoots = CLAMP(aeg->overshoots, 0, 3);

	AEG_TRACE("[%s] ---%s--> [%s] (overshoots=%d, wait=%d)", state_to_string(aeg->state), action_to_string(action),
	          state_to_string(new_state), aeg->overshoots, aeg->wait);

	aeg->state = new_state;
}

//! Maps a `brightness` in [0, 1] to a pair of exposure and gain values based on
//! a piecewise function.
static void
brightness_to_expgain(struct u_autoexpgain *aeg, float brightness, float *out_exposure, float *out_gain)
{

	//! These are steps for constructing a piecewise linear function that maps
	//! brightness into (exposure, gain) pairs.
	struct step
	{
		float b; //!< Brightness
		float e; //!< Exposure
		float g; //!< Gain
	};

	// These tables were tuned over WMR cameras such that increasing
	// brightness increases the histogram range more or less linearly.
	struct step steps_t[] = {{0, 120, 16},      {0.15, 4500, 16}, {0.5, 4500, 127},
	                         {0.55, 6000, 127}, {0.9, 6000, 255}, {1, 9000, 255}};
	struct step steps_dr[] = {{0, 120, 16}, {0.3, 9000, 16}, {1.0, 9000, 255}};

	// Select the steps table to use based on our strategy/objective
	struct step *steps = NULL;
	int steps_count = 0;
	if (aeg->strategy == U_AEG_STRATEGY_TRACKING) {
		steps = steps_t;
		steps_count = sizeof(steps_t) / sizeof(struct step);
	} else if (aeg->strategy == U_AEG_STRATEGY_DYNAMIC_RANGE) {
		steps = steps_dr;
		steps_count = sizeof(steps_dr) / sizeof(struct step);
	} else {
		AEG_ASSERT(false, "Unexpected strategy=%d", aeg->strategy);
	}

	// Other simpler tables that might work for WMR are:
	// {{0, 120, 16}, {0.2, 6000, 16}, {1.0, 6000, 255}};
	// {{0, 120, 16}, {0.2, 6000, 16}, {0.9, 6000, 255}, {1.0, 9000, 255}};

	// Assertions
	AEG_ASSERT(steps_count >= 2, "Expected at least two steps but %d found", steps_count);
	AEG_ASSERT(steps[0].b == 0, "First step should be at b=0");
	AEG_ASSERT(steps[steps_count - 1].b == 1, "Last step should be at b=1");
	AEG_ASSERT(brightness >= 0 && brightness <= 1, "Invalid brightness=%f", brightness);

	// Compute the piecewise function result from `steps`
	float exposure = 0;
	float gain = 0;
	for (int i = 1; i < steps_count; i++) {
		struct step s0 = steps[i - 1];
		struct step s1 = steps[i];

		float lower_b = s0.b;
		float higher_b = s1.b;

		if (brightness >= lower_b && brightness <= higher_b) {
			exposure = s0.e + ((brightness - lower_b) / (higher_b - lower_b)) * (s1.e - s0.e);
			gain = s0.g + ((brightness - lower_b) / (higher_b - lower_b)) * (s1.g - s0.g);
			break;
		}
	}
	*out_exposure = exposure;
	*out_gain = gain;
}

//! Update `exposure` and `gain` based on current `brightness` value.
static void
update_expgain(struct u_autoexpgain *aeg)
{
	float brightness = aeg->brightness.val;
	if (aeg->last_brightness == brightness) {
		return;
	}
	aeg->last_brightness = brightness;

	brightness_to_expgain(aeg, brightness, &aeg->exposure, &aeg->gain);
}

//! Returns a value in the range [-1, 1] describing how dark-bright the image
//! is, 0 means it's alright.
static float
get_score(struct u_autoexpgain *aeg, struct xrt_frame *xf)
{
	uint32_t w = xf->width;
	uint32_t h = xf->height;
	uint32_t s = w / GRID_COLS; // Grid cell size

	// Compute histogram (PDF)
	int histogram[LEVELS] = {0};
	int samples_count = 0;
	size_t pixel_size = u_format_block_size(xf->format);
	for (uint32_t y = 0; y < h; y += s) {
		for (uint32_t x = 0; x < w; x += s) {
			// Note that for multichannel images only the first channel is in use.
			uint8_t intensity = xf->data[y * xf->stride + x * pixel_size];
			histogram[intensity]++;
			samples_count++;
		}
	}

	// Draw histogram
	for (int i = 0; i < LEVELS; i++) {
		aeg->histogram[i] = histogram[i];
	}

	// Compute mean
	float mean = 0;
	for (int i = 0; i < LEVELS; i++) {
		mean += (float)i * histogram[i];
	}
	mean /= samples_count;

	float score = 0;

	// Score that tries to make the mean reach a `target_mean`.

	float target_mean = -1;
	if (aeg->strategy == U_AEG_STRATEGY_TRACKING) {
		// We are not that much interested in using the full dynamic range for tracking
		// so we prefer a darkish image because that reduces exposure and gain.
		target_mean = LEVELS / 4;
	} else if (aeg->strategy == U_AEG_STRATEGY_DYNAMIC_RANGE) {
		target_mean = LEVELS / 2;
	} else {
		AEG_ASSERT(false, "Unexpected strategy=%d", aeg->strategy);
	}

	const float range_size = mean < target_mean ? target_mean : (LEVELS - target_mean);
	score = (mean - target_mean) / range_size;
	score = CLAMP(score, -1, 1);

	return score;
}

static void
update_brightness(struct u_autoexpgain *aeg, struct xrt_frame *xf)
{
	float score = get_score(aeg, xf);
	aeg->current_score = score;

	if (!aeg->enable) {
		return;
	}

	float target_score = 0;
	if (aeg->strategy == U_AEG_STRATEGY_TRACKING) {
		target_score = -aeg->threshold; // Makes 0 the right bound of our "good enugh" range
	} else if (aeg->strategy == U_AEG_STRATEGY_DYNAMIC_RANGE) {
		target_score = 0;
	} else {
		AEG_ASSERT(false, "Unexpected strategy=%d", aeg->strategy);
	}

	enum u_aeg_action action; // State machine input action
	if (score > target_score + aeg->threshold) {
		action = BRIGHT;
	} else if (score < target_score - aeg->threshold) {
		action = DARK;
	} else {
		action = GOOD;
	}

	set_state(aeg, action);

	if (aeg->state != BRIGHTEN && aeg->state != DARKEN) {
		return;
	}

	float max_step = aeg->max_brightness_step;
	float step = max_step * score / powf(2.0f, aeg->overshoots);
	aeg->brightness.val -= CLAMP(step, -max_step, max_step);
	aeg->brightness.val = CLAMP(aeg->brightness.val, 0, 1);
}

/*
 *
 * Exported functions
 *
 */

struct u_autoexpgain *
u_autoexpgain_create(enum u_aeg_strategy strategy, bool enabled_from_start, int frame_delay)
{
	struct u_autoexpgain *aeg = U_TYPED_CALLOC(struct u_autoexpgain);

	aeg->enable = enabled_from_start;
	aeg->log_level = debug_get_log_option_aeg_log();
	aeg->state = IDLE;
	aeg->wait = frame_delay;
	aeg->overshoots = 0;
	aeg->strategy = strategy;
	aeg->strategy_combo.count = U_AEG_STRATEGY_COUNT;
	aeg->strategy_combo.options = "Tracking\0Dynamic Range\0\0";
	aeg->strategy_combo.value = (int *)&aeg->strategy;

	aeg->histogram_ui.values = aeg->histogram;
	aeg->histogram_ui.count = LEVELS;

	aeg->brightness.max = 1;
	aeg->brightness.min = 0;
	aeg->brightness.step = 0.002;
	aeg->brightness.val = INITIAL_BRIGHTNESS;
	aeg->last_brightness = INITIAL_BRIGHTNESS;
	aeg->max_brightness_step = INITIAL_MAX_BRIGHTNESS_STEP;

	aeg->threshold = INITIAL_THRESHOLD;
	aeg->frame_delay = frame_delay;

	brightness_to_expgain(aeg, INITIAL_BRIGHTNESS, &aeg->exposure, &aeg->gain);

	return aeg;
}

void
u_autoexpgain_add_vars(struct u_autoexpgain *aeg, void *root, char *prefix)
{
	char tmp[256];

	(void)snprintf(tmp, sizeof(tmp), "%sAuto exposure and gain control", prefix);
	u_var_add_gui_header_begin(root, NULL, tmp);

	(void)snprintf(tmp, sizeof(tmp), "%sUpdate brightness automatically", prefix);
	u_var_add_bool(root, &aeg->enable, tmp);

	(void)snprintf(tmp, sizeof(tmp), "%sFrame update delay", prefix);
	u_var_add_i32(root, &aeg->frame_delay, tmp);

	(void)snprintf(tmp, sizeof(tmp), "%sStrategy", prefix);
	u_var_add_combo(root, &aeg->strategy_combo, tmp);

	(void)snprintf(tmp, sizeof(tmp), "%sBrightness", prefix);
	u_var_add_draggable_f32(root, &aeg->brightness, tmp);

	(void)snprintf(tmp, sizeof(tmp), "%sScore threshold", prefix);
	u_var_add_f32(root, &aeg->threshold, tmp);

	(void)snprintf(tmp, sizeof(tmp), "%sMax brightness step", prefix);
	u_var_add_f32(root, &aeg->max_brightness_step, tmp);

	(void)snprintf(tmp, sizeof(tmp), "%sImage score", prefix);
	u_var_add_ro_f32(root, &aeg->current_score, tmp);

	(void)snprintf(tmp, sizeof(tmp), "%sIntensity histogram", prefix);
	u_var_add_histogram_f32(root, &aeg->histogram_ui, tmp);

	(void)snprintf(tmp, sizeof(tmp), "%sAEG log level", prefix);
	u_var_add_log_level(root, &aeg->log_level, tmp);

	u_var_add_gui_header_end(root, NULL, tmp);
}

void
u_autoexpgain_update(struct u_autoexpgain *aeg, struct xrt_frame *xf)
{
	update_brightness(aeg, xf);
	update_expgain(aeg);
}

float
u_autoexpgain_get_exposure(struct u_autoexpgain *aeg)
{
	return aeg->exposure;
}

float
u_autoexpgain_get_gain(struct u_autoexpgain *aeg)
{
	return aeg->gain;
}

void
u_autoexpgain_destroy(struct u_autoexpgain **aeg)
{
	free(*aeg);
	*aeg = NULL;
}
