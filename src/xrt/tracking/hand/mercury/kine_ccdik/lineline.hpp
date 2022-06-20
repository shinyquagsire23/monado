// Copyright 1998, Paul Bourke.
// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Find the closest approach between two lines
 * @author Paul Bourke <paul.bourke@gmail.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */
#include "float.h"
#include <limits.h>
#include <math.h>
#include "util/u_logging.h"
#include "xrt/xrt_defines.h"
#include "stdbool.h"

// Taken and relicensed with written permission from http://paulbourke.net/geometry/pointlineplane/lineline.c +
// http://paulbourke.net/geometry/pointlineplane/

/*
   Calculate the line segment PaPb that is the shortest route between
   two lines P1P2 and P3P4. Calculate also the values of mua and mub where
      Pa = P1 + mua (P2 - P1)
      Pb = P3 + mub (P4 - P3)
   Return false if no solution exists.
*/
bool
LineLineIntersect(struct xrt_vec3 p1,
                  struct xrt_vec3 p2,
                  struct xrt_vec3 p3,
                  struct xrt_vec3 p4,
                  struct xrt_vec3 *pa,
                  struct xrt_vec3 *pb,
                  float *mua,
                  float *mub)
{
	struct xrt_vec3 p13, p43, p21;           // NOLINT
	float d1343, d4321, d1321, d4343, d2121; // NOLINT
	float number, denom;                     // NOLINT

	p13.x = p1.x - p3.x;
	p13.y = p1.y - p3.y;
	p13.z = p1.z - p3.z;

	p43.x = p4.x - p3.x;
	p43.y = p4.y - p3.y;
	p43.z = p4.z - p3.z;
	// Disabled - just checks that P3P4 isn't length 0, which it won't be.
#if 0
   if (ABS(p43.x) < FLT_EPSILON && ABS(p43.y) < FLT_EPSILON && ABS(p43.z) < FLT_EPSILON)
      return false;
#endif
	p21.x = p2.x - p1.x;
	p21.y = p2.y - p1.y;
	p21.z = p2.z - p1.z;

	// Ditto, checks that P2P1 isn't length 0.
#if 0
   if (ABS(p21.x) < EPS && ABS(p21.y) < EPS && ABS(p21.z) < EPS)
      return false;
#endif

	d1343 = p13.x * p43.x + p13.y * p43.y + p13.z * p43.z;
	d4321 = p43.x * p21.x + p43.y * p21.y + p43.z * p21.z;
	d1321 = p13.x * p21.x + p13.y * p21.y + p13.z * p21.z;
	d4343 = p43.x * p43.x + p43.y * p43.y + p43.z * p43.z;
	d2121 = p21.x * p21.x + p21.y * p21.y + p21.z * p21.z;

	denom = d2121 * d4343 - d4321 * d4321;

	// Division-by-zero check
	if (fabsf(denom) < FLT_EPSILON) {
		return false;
	}
	number = d1343 * d4321 - d1321 * d4343;

	*mua = number / denom;
	*mub = (d1343 + d4321 * (*mua)) / d4343;

	pa->x = p1.x + *mua * p21.x;
	pa->y = p1.y + *mua * p21.y;
	pa->z = p1.z + *mua * p21.z;
	pb->x = p3.x + *mub * p43.x;
	pb->y = p3.y + *mub * p43.y;
	pb->z = p3.z + *mub * p43.z;

	return (true);
}
