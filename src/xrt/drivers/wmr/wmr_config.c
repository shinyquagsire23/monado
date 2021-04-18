/* Copyright 2021, Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 */
/*!
 * @file
 * @brief	Driver code to read WMR config blocks
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_wmr
 */
#include "wmr_config.h"

bool
wmr_config_parse(struct wmr_hmd_config *c)
{
	int i, j, k;

	const struct xrt_vec2 display_size[2] = {{4320, 2160}, {4320, 2160}};

	// eye_centers from VisibleAreaCenter X/Y
	struct xrt_vec2 eye_centers[2] = {
	    {1171.2011028243148, 1078.7720082720277},
	    {3154.10490135909, 1085.7209119898746},
	};

	const double eye_radius[2] = {1500, 1500};
	const double eye_affine[2][9] = {
	    {1467.741455078125, -0, 1171.2010498046875, -0, 1467.642333984375, 1078.77197265625, 0, 0, 1},
	    {1469.2613525390625, -0, 3154.10498046875, -0, 1468.5185546875, 1085.720947265625, 0, 0, 1}};

	// These need to be acquired from the WMR config:
	/* From DistortionRed/Green/Blue ModelParameters[0] and [1] */
	const struct xrt_vec2 eye_centers_rgb[2][3][2] = {{{{1173.7816892048809, 1079.9493112867228}},
	                                                   {{1171.8855282399534, 1078.1630786236972}},
	                                                   {{1169.6008128643944, 1074.7670304358412}}},
	                                                  {{{3150.2395019256492, 1086.1749083261475}},
	                                                   {{3149.7810962925541, 1084.9113795043713}},
	                                                   {{3150.3983095783842, 1082.3335369357619}}}};
	/* From DistortionRed/Green/Blue ModelParameters [2,3,4] */
	const double distortion_params[2][3][3] = {
	    {
	        {1.6392082863852426E-7, 4.0096839564631026E-14, 6.6538855737065E-20},   /* Red */
	        {2.1590946493033866E-7, -4.78028658789357E-14, 1.1929574027716904E-19}, /* Green */
	        {3.1991456111909366E-7, -2.300137347653273E-13, 2.3405778580046485E-19} /* Blue */
	    },
	    {
	        {1.5982850735663643E-7, 4.990973924637425E-14, 6.0056239395619067E-20},   /* Red */
	        {2.1206804797012724E-7, -3.5561864117498794E-14, 1.0992145779675043E-19}, /* Green */
	        {3.1395508877599257E-7, -2.0999418299177255E-13, 2.1828476911150306E-19}  /* Blue */
	    }};

	for (i = 0; i < 2; i++) {
		struct wmr_distortion_eye_config *eye = c->eye_params + i;

		eye->display_size = display_size[i];
		eye->visible_center = eye_centers[i];
		eye->visible_radius = eye_radius[i];
		for (j = 0; j < 9; j++) {
			eye->affine_xform.v[j] = eye_affine[i][j];
		}

		for (j = 0; j < 3; j++) {
			/* RGB distortion params */
			eye->distortion3K[j].eye_center = *eye_centers_rgb[i][j];
			for (k = 0; k < 3; k++) {
				eye->distortion3K[j].k[k] = distortion_params[i][j][k];
			}
		}
	}
	return true;
}
