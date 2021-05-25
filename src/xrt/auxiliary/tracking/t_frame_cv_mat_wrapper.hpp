// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simple @ref xrt_frame wrapper around a @ref cv::Mat.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_tracking
 */

#include "xrt/xrt_frame.h"

#include <opencv2/opencv.hpp>


namespace xrt::auxiliary::tracking {


class FrameMat
{
public:
	/*!
	 * Additional optional parameters for frame creation.
	 */
	class Params
	{
	public:
		enum xrt_stereo_format stereo_format;
		uint64_t timestamp_ns;
	};


public:
	// Exposed to the C api.
	struct xrt_frame frame = {};

	// The @ref cv::Mat that holds the data.
	cv::Mat matrix = cv::Mat();


public:
	/*!
	 * Only public due to C needed to destroy it.
	 */
	~FrameMat();

	/*!
	 * Wraps the given @ref cv::Mat assuming it's a 24bit RGB format matrix, the pointer pointed to by @ref xf_ptr
	 * will have it's reference updated.
	 */
	static void
	wrapR8G8B8(cv::Mat mat, xrt_frame **xf_ptr, const Params /*&&?*/ params = {});

	/*!
	 * Wraps the given @ref cv::Mat assuming it's a 8bit format matrix, the pointer pointed to by @ref xf_ptr will
	 * have it's reference updated.
	 */
	static void
	wrapL8(cv::Mat mat, xrt_frame **xf_ptr, const Params /*&&?*/ params = {});


private:
	FrameMat();

	void
	fillInFields(cv::Mat mat, xrt_format format, const Params &params);
};



} // namespace xrt::auxiliary::tracking
