// Copyright 2021, Collabora Ltd.
// Author: Jakob Bornecrantz <jakob@collabora.com>
// SPDX-License-Identifier: BSL-1.0


float from_linear_to_srgb_channel(float value)
{
	if (value < 0.0031308) {
		return 12.92 * value;
	} else {
		return 1.055 * pow(value, 1.0 / 2.4) - 0.055;
	}
}

vec3 from_linear_to_srgb(vec3 linear_rgb)
{
	return vec3(
		from_linear_to_srgb_channel(linear_rgb.r),
		from_linear_to_srgb_channel(linear_rgb.g),
		from_linear_to_srgb_channel(linear_rgb.b)
	);
}
