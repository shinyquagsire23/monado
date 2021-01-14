// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Small helper struct that for debugging views.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_tracking
 */

#pragma once

#ifndef __cplusplus
#error "This header is C++-only."
#endif

#include <opencv2/opencv.hpp>
#include "util/u_frame.h"


struct HelperDebugSink
{
public:
	enum Kind
	{
		AllAvailable,
		AlwaysSingle,
	};

public:
	Kind kind = AllAvailable;
	struct xrt_frame_sink *sink = {};
	struct xrt_frame *frame = {};

	cv::Mat rgb[2] = {};


public:
	HelperDebugSink(Kind kind)
	{
		this->kind = kind;
	}

	HelperDebugSink() = delete;

	~HelperDebugSink()
	{
		xrt_frame_reference(&frame, NULL);
	}

	void
	refresh(struct xrt_frame *xf)
	{
		if (sink == NULL) {
			return;
		}

		// But what about second breakfast?
		bool second_view = false;
		int rows, cols, width, height;

		cols = xf->width;
		rows = xf->height;
		width = xf->width;
		height = xf->height;
		enum xrt_stereo_format stereo_format = xf->stereo_format;

		switch (xf->stereo_format) {
		case XRT_STEREO_FORMAT_SBS:
			cols /= 2;
			if (kind == AllAvailable) {
				second_view = true;
			} else {
				stereo_format = XRT_STEREO_FORMAT_NONE;
				width /= 2;
				second_view = false;
			}
			break;
		case XRT_STEREO_FORMAT_NONE:
			// Noop
			break;
		default: return;
		}

		// Create a new frame and also dereferences the old frame.
		u_frame_create_one_off(XRT_FORMAT_R8G8B8, width, height, &frame);

		// Copy needed info.
		frame->source_sequence = xf->source_sequence;
		frame->stereo_format = stereo_format;

		// Doesn't claim ownership of the frame data,
		// points directly at the frame data.
		rgb[0] = cv::Mat(   //
		    rows,           // rows
		    cols,           // cols
		    CV_8UC3,        // channels
		    frame->data,    // data
		    frame->stride); // stride

		if (second_view) {
			// Doesn't claim ownership of the frame data,
			// points directly at the frame data.
			rgb[1] = cv::Mat(           //
			    rows,                   // rows
			    cols,                   // cols
			    CV_8UC3,                // channels
			    frame->data + 3 * cols, // data
			    frame->stride);         // stride
		}
	}

	void
	submit()
	{
		if (frame != NULL) {
			// Make sure that the cv::Mats doesn't use the data.
			rgb[0] = cv::Mat();
			rgb[1] = cv::Mat();
			sink->push_frame(sink, frame);
		}

		// We unreference the frame here, downstream is either
		// done with it or have referenced it themselves.
		xrt_frame_reference(&frame, NULL);
	}
};
