// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simple @ref xrt_frame wrapper around a cv::Mat.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_tracking
 */

#include "util/u_format.h"

#include "tracking/t_frame_cv_mat_wrapper.hpp"


namespace xrt::auxiliary::tracking {


/*
 *
 * C functions.
 *
 */

extern "C" void
frame_mat_destroy(struct xrt_frame *xf)
{
	FrameMat *fm = (FrameMat *)xf;
	delete fm;
}


/*
 *
 * Member functions
 *
 */

void
FrameMat::fillInFields(cv::Mat mat, xrt_format format, const Params &params)
{
	uint32_t width = (uint32_t)mat.cols;
	uint32_t height = (uint32_t)mat.rows;
	size_t stride = mat.step[0];
	size_t size = stride * height;

	this->matrix = mat;

	// Main wrapping of cv::Mat by frame.
	xrt_frame &frame = this->frame;
	frame.reference.count = 1;
	frame.destroy = frame_mat_destroy;
	frame.data = mat.ptr<uint8_t>();
	frame.format = format;
	frame.width = width;
	frame.height = height;
	frame.stride = stride;
	frame.size = size;

	// Params
	frame.timestamp = params.timestamp_ns;
	frame.stereo_format = params.stereo_format;
}

FrameMat::~FrameMat()
{
	// Noop
}

FrameMat::FrameMat()
{
	// Noop
}


/*
 *
 * Static functions.
 *
 */

void
FrameMat::wrapR8G8B8(const cv::Mat &mat, xrt_frame **fm_out, const Params /*&&?*/ params)
{
	assert(mat.channels() == 3);
	assert(mat.type() == CV_8UC3);


	FrameMat *fm = new FrameMat();
	fm->fillInFields(mat, XRT_FORMAT_R8G8B8, params);

	// Unreference any old frames.
	xrt_frame_reference(fm_out, NULL);

	// Already has a ref count of one.
	*fm_out = &fm->frame;
}

void
FrameMat::wrapL8(const cv::Mat &mat, xrt_frame **fm_out, const Params /*&&?*/ params)
{
	assert(mat.channels() == 1);
	assert(mat.type() == CV_8UC1);


	FrameMat *fm = new FrameMat();
	fm->fillInFields(mat, XRT_FORMAT_L8, params);

	// Unreference any old frames.
	xrt_frame_reference(fm_out, NULL);

	// Already has a ref count of one.
	*fm_out = &fm->frame;
}


} // namespace xrt::auxiliary::tracking
