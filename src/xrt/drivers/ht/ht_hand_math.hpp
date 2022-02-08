// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Helper math to do things with 3D hands for the camera-based hand tracker
 * @author Moses Turner <moses@collabora.com>
 * @author Nick Klingensmith <programmerpichu@gmail.com>
 * @ingroup drv_ht
 */

#pragma once

struct Hand2D;
struct Hand3D;
struct HandHistory3D;
struct ht_device;
struct xrt_hand_joint_set;

float
sumOfHandJointDistances(const Hand3D &one, const Hand3D &two);

float
errHandHistory(const HandHistory3D &history_hand, const Hand3D &present_hand);
float
errHandDisparity(const Hand2D &left_rays, const Hand2D &right_rays);

void
applyJointWidths(struct xrt_hand_joint_set *set);
void
applyThumbIndexDrag(Hand3D *hand);
void
applyJointOrientations(struct xrt_hand_joint_set *set, bool is_right);

float
handednessJointSet(Hand3D *set);
void
handednessHandHistory3D(HandHistory3D *history);

void
handEuroFiltersInit(HandHistory3D *history, double fc_min, double fc_min_d, double beta);
void
handEuroFiltersRun(struct ht_device *htd, HandHistory3D *f, Hand3D *out_hand);

bool
rejectTooFar(struct ht_device *htd, Hand3D *hand);
bool
rejectTooClose(struct ht_device *htd, Hand3D *hand);
bool
rejectTinyPalm(struct ht_device *htd, Hand3D *hand);
