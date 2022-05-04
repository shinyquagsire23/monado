// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Documentation-only header.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_tracking
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup aux_tracking Tracking
 * @ingroup aux
 * @brief Trackers, filters and associated helper code.
 *
 *
 * ### Coordinate system
 *
 * Right now there is no specific convention on where a tracking systems
 * coordinate system is centered, and is something we probably need to figure
 * out. Right now the stereo based tracking system used by the PSVR and PSMV
 * tracking system is centered on the camera that OpenCV decided is origin.
 *
 * To go a bit further on the PSVR/PSMV case. Think about a idealized start up
 * case, the user is wearing the HMD headset and holding two PSMV controllers.
 * The HMD's coordinate system axis are perfectly parallel with the user
 * coordinate with the user's coordinate system. Where -Z is forward. The user
 * holds the controllers with the ball pointing up and the buttons on the back
 * pointing forward. Which if you read the documentation of @ref psmv_device
 * will that the axis of the PSMV are also perfectly aligned with the users
 * coordinate system. So everything "attached" to the user has its coordinate
 * system parallel to the user's.
 *
 * The camera on the other hand is looking directly at the user, its Z-axis and
 * X-axis is flipped in relation to the user's. So to compare what is sees to
 * what the user sees, everything is rotated 180° around the Y-axis.
 */

/*!
 * @dir auxiliary/tracking
 * @ingroup aux
 *
 * @brief Trackers, filters and associated helper code.
 */


#ifdef __cplusplus

namespace xrt::auxiliary {
	/*!
	 * @brief Namespace used by C++ interfaces in the auxiliary tracking library code.
	 */
	namespace tracking {
		// Empty
	} // namespace tracking
} // namespace xrt::auxiliary
#endif


#ifdef __cplusplus
}
#endif
