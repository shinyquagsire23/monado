// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Code to run machine learning models for camera-based hand tracker.
 * @author Moses Turner <moses@collabora.com>
 * @author Marcus Edel <marcus.edel@collabora.com>
 * @author Simon Zeni <simon@bl4ckb0ne.ca>
 * @ingroup drv_ht
 */

#pragma once

#include "ht_driver.hpp"

#include <opencv2/core/mat.hpp>

#include <filesystem>
#include <array>

// forward-declare
struct OrtApi;
struct OrtEnv;
struct OrtMemoryInfo;
struct OrtSession;
struct OrtSessionOptions;
struct OrtValue;
struct ht_device;

class ht_model
{
	struct ht_device *device = nullptr;

	const OrtApi *api = nullptr;
	OrtEnv *env = nullptr;

	OrtMemoryInfo *palm_detection_meminfo = nullptr;
	OrtSession *palm_detection_session = nullptr;
	OrtValue *palm_detection_tensor = nullptr;
	std::array<float, 3 * 128 * 128> palm_detection_data;

	std::mutex hand_landmark_lock;
	OrtMemoryInfo *hand_landmark_meminfo = nullptr;
	OrtSession *hand_landmark_session = nullptr;
	OrtValue *hand_landmark_tensor = nullptr;
	std::array<float, 3 * 224 * 224> hand_landmark_data;

	void
	init_palm_detection(OrtSessionOptions *opts);
	void
	init_hand_landmark(OrtSessionOptions *opts);

public:
	ht_model(struct ht_device *htd);
	~ht_model();

	std::vector<Palm7KP>
	palm_detection(ht_view *htv, const cv::Mat &input);
	Hand2D
	hand_landmark(const cv::Mat input);
};
