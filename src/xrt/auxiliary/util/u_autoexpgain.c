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
#include "util/u_format.h"
#include "util/u_misc.h"
#include "util/u_var.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(X, A, B) (MIN(MAX((X), (A)), (B)))

#define LEVELS 256 //!< Possible pixel intensity values, only 8-bit supported
#define INITIAL_BRIGHTNESS 0.5
#define INITIAL_MAX_BRIGHTNES_STEP 0.05 //!< 0.1 is faster but introduces oscilations more often
#define INITIAL_THRESHOLD 0.1
#define GRID_COLS 40 //!< Amount of columns for the histogram sample grid

//! Auto exposure and gain (AEG) adjustment algorithm state.
struct u_autoexpgain
{
	bool enable; //!< Whether to enable auto exposure and gain adjustment

	//! Algorithm strategy that affects how score and brightness are computed
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

	//! Scores further than `threshold` from zero will trigger a `brightness` update.
	float threshold;

	uint32_t frame_counter; //!< Number of frames received

	//! Every how many frames should we update `brightness`. Some cameras take a
	//! couple of frames until the new exposure/gain sets in and a new score can
	//! be recomputed properly.
	uint8_t update_every;

	float exposure; //!< Currently computed exposure value to use
	float gain;     //!< Currently computed gain value to use
};

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
		assert(false && "unexpected branch taken");
	}

	// Other simpler tables that might work for WMR are:
	// {{0, 120, 16}, {0.2, 6000, 16}, {1.0, 6000, 255}};
	// {{0, 120, 16}, {0.2, 6000, 16}, {0.9, 6000, 255}, {1.0, 9000, 255}};

	// Assertions
	assert(steps_count >= 2 && "Exoected at least two steps");
	assert(steps[0].b == 0 && "First step should be at b=0");
	assert(steps[steps_count - 1].b == 1 && "Last step should be at b=1");
	assert(brightness >= 0 && brightness <= 1);

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

	float exposure = -1;
	float gain = -1;
	brightness_to_expgain(aeg, brightness, &exposure, &gain);
	aeg->exposure = (uint16_t)exposure;
	aeg->gain = (uint8_t)gain;
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

	// Score that tries to make the mean reach TARGET_MEAN.

	float target_mean = -1;
	if (aeg->strategy == U_AEG_STRATEGY_TRACKING) {
		// We are not that much interested in using the full dynamic range for tracking
		// so we prefer a darkish image because that reduces exposure and gain.
		target_mean = LEVELS / 4;
	} else if (aeg->strategy == U_AEG_STRATEGY_DYNAMIC_RANGE) {
		target_mean = LEVELS / 2;
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

	aeg->frame_counter++;
	if (aeg->frame_counter % aeg->update_every != 0) {
		return;
	}

	bool score_is_high = fabsf(score) > aeg->threshold;
	if (!score_is_high) {
		return;
	}

	float max_step = aeg->max_brightness_step;
	float step = CLAMP(max_step * score, -max_step, max_step);
	aeg->brightness.val -= step;
	aeg->brightness.val = CLAMP(aeg->brightness.val, 0, 1);
}

/*
 *
 * Exported functions
 *
 */

struct u_autoexpgain *
u_autoexpgain_create(enum u_aeg_strategy strategy, bool enabled_from_start, uint8_t update_every)
{
	struct u_autoexpgain *aeg = U_TYPED_CALLOC(struct u_autoexpgain);

	aeg->enable = enabled_from_start;
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
	aeg->max_brightness_step = INITIAL_MAX_BRIGHTNES_STEP;

	aeg->threshold = INITIAL_THRESHOLD;
	aeg->frame_counter = 0;
	aeg->update_every = update_every;

	brightness_to_expgain(aeg, INITIAL_BRIGHTNESS, &aeg->exposure, &aeg->gain);

	return aeg;
}

void
u_autoexpgain_add_vars(struct u_autoexpgain *aeg, void *root)
{
	u_var_add_bool(root, &aeg->enable, "Enable AEG");
	u_var_add_u8(root, &aeg->update_every, "Update every X frames");
	u_var_add_combo(root, &aeg->strategy_combo, "Strategy");
	u_var_add_draggable_f32(root, &aeg->brightness, "Brightness");
	u_var_add_f32(root, &aeg->threshold, "Score threshold");
	u_var_add_f32(root, &aeg->max_brightness_step, "Max brightness step");
	u_var_add_ro_f32(root, &aeg->current_score, "Image score");
	u_var_add_histogram_f32(root, &aeg->histogram_ui, "Intensity histogram");
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
