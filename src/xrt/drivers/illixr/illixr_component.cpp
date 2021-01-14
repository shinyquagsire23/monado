// Copyright 2020-2021, The Board of Trustees of the University of Illinois.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  ILLIXR plugin
 * @author RSIM Group <illixr@cs.illinois.edu>
 * @ingroup drv_illixr
 */


#include "xrt/xrt_device.h"

#include <iostream>
#include "common/plugin.hpp"
#include "common/phonebook.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/pose_prediction.hpp"

using namespace ILLIXR;

/// Dummy plugin class for an instance during phonebook registration
class illixr_plugin : public plugin
{
public:
	illixr_plugin(std::string name_, phonebook *pb_)
	    : plugin{name_, pb_}, sb{pb->lookup_impl<switchboard>()}, sb_pose{pb->lookup_impl<pose_prediction>()},
	      sb_eyebuffer{sb->publish<rendered_frame>("eyebuffer")}, sb_vsync_estimate{sb->subscribe_latest<time_type>(
	                                                                  "vsync_estimate")}
	{}

	const std::shared_ptr<switchboard> sb;
	const std::shared_ptr<pose_prediction> sb_pose;
	const std::unique_ptr<writer<rendered_frame>> sb_eyebuffer;
	const std::unique_ptr<reader_latest<time_type>> sb_vsync_estimate;
	fast_pose_type prev_pose;                                       /* stores a copy of pose each time
	                                                                   illixr_read_pose() is called */
	std::chrono::time_point<std::chrono::system_clock> sample_time; /* when prev_pose was stored */
};

static illixr_plugin *illixr_plugin_obj = nullptr;

extern "C" plugin *
illixr_monado_create_plugin(phonebook *pb)
{
	illixr_plugin_obj = new illixr_plugin{"illixr_plugin", pb};
	illixr_plugin_obj->start();
	return illixr_plugin_obj;
}

extern "C" struct xrt_pose
illixr_read_pose()
{
	assert(illixr_plugin_obj && "illixr_plugin_obj must be initialized first.");

	if (!illixr_plugin_obj->sb_pose->fast_pose_reliable()) {
		std::cerr << "Pose not reliable yet; returning best guess" << std::endl;
	}
	struct xrt_pose ret;
	const fast_pose_type fast_pose = illixr_plugin_obj->sb_pose->get_fast_pose();
	const pose_type pose = fast_pose.pose;

	// record when the pose was read for use in write_frame
	illixr_plugin_obj->sample_time = std::chrono::system_clock::now();

	ret.orientation.x = pose.orientation.x();
	ret.orientation.y = pose.orientation.y();
	ret.orientation.z = pose.orientation.z();
	ret.orientation.w = pose.orientation.w();
	ret.position.x = pose.position.x();
	ret.position.y = pose.position.y();
	ret.position.z = pose.position.z();

	// store pose in static variable for use in write_frame
	illixr_plugin_obj->prev_pose = fast_pose; // copy member variables

	return ret;
}