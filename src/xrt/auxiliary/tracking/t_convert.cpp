// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Code to build conversion tables and convert images.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_tracking
 */

#include "tracking/t_tracking.h"

#include <opencv2/opencv.hpp>


/*
 *
 * 'Exported' functions.
 *
 */

extern "C" void
t_convert_fill_table(struct t_convert_table *t)
{
	for (int y = 0; y < 256; y++) {
		for (int u = 0; u < 256; u++) {
			uint8_t *dst = &t->v[y][u][0][0];

			for (int v = 0; v < 256; v++) {
				dst[0] = y;
				dst[1] = u;
				dst[2] = v;
				dst += 3;
			}
		}
	}
}

extern "C" void
t_convert_make_y8u8v8_to_r8g8b8(struct t_convert_table *t)
{
	size_t size = 256 * 256 * 256;

	t_convert_fill_table(t);
	t_convert_in_place_y8u8v8_to_r8g8b8(size, 1, 0, t);
}

extern "C" void
t_convert_make_y8u8v8_to_h8s8v8(struct t_convert_table *t)
{
	size_t size = 256 * 256 * 256;

	t_convert_fill_table(t);
	t_convert_in_place_y8u8v8_to_h8s8v8(size, 1, 0, &t->v);
}

extern "C" void
t_convert_make_h8s8v8_to_r8g8b8(struct t_convert_table *t)
{
	size_t size = 256 * 256 * 256;

	t_convert_fill_table(t);
	t_convert_in_place_h8s8v8_to_r8g8b8(size, 1, 0, &t->v);
}

extern "C" void
t_convert_in_place_y8u8v8_to_r8g8b8(uint32_t width, uint32_t height, size_t stride, void *data_ptr)
{
	cv::Mat data(height, width, CV_8UC3, data_ptr, stride);
	cv::cvtColor(data, data, cv::COLOR_YUV2RGB);
}

extern "C" void
t_convert_in_place_y8u8v8_to_h8s8v8(uint32_t width, uint32_t height, size_t stride, void *data_ptr)
{
	cv::Mat data(height, width, CV_8UC3, data_ptr, stride);
	cv::Mat temp(height, width, CV_32FC3);
	cv::cvtColor(data, temp, cv::COLOR_YUV2RGB);
	cv::cvtColor(temp, data, cv::COLOR_RGB2HSV);
}

extern "C" void
t_convert_in_place_h8s8v8_to_r8g8b8(uint32_t width, uint32_t height, size_t stride, void *data_ptr)
{
	cv::Mat data(height, width, CV_8UC3, data_ptr, stride);
	cv::cvtColor(data, data, cv::COLOR_YUV2RGB);
}
