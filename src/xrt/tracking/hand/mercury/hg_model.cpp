// Copyright 2021-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Mercury ML models!
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */

// Many C api things were stolen from here (MIT license):
// https://github.com/microsoft/onnxruntime-inference-examples/blob/main/c_cxx/fns_candy_style_transfer/fns_candy_style_transfer.c
#include "hg_sync.hpp"
#include "hg_image_math.inl"


#include <filesystem>
#include <array>

namespace xrt::tracking::hand::mercury {

#define ORT(expr)                                                                                                      \
	do {                                                                                                           \
		OrtStatus *status = wrap->api->expr;                                                                   \
		if (status != nullptr) {                                                                               \
			const char *msg = wrap->api->GetErrorMessage(status);                                          \
			HG_ERROR(hgt, "[%s:%d]: %s\n", __FILE__, __LINE__, msg);                                       \
			wrap->api->ReleaseStatus(status);                                                              \
			assert(false);                                                                                 \
		}                                                                                                      \
	} while (0)

static cv::Matx23f
blackbar(const cv::Mat &in, enum t_camera_orientation rot, cv::Mat &out, xrt_size out_size)
{
	// Easy to think about, always right, but pretty slow:
	// Get a matrix from the original to the scaled down / blackbar'd image, then get one that goes back.
	// Then just warpAffine() it.
	// Easy in programmer time - never have to worry about off by one, special cases. We can come back and optimize
	// later.
	bool swapped_wh = false;
	float in_w, in_h;

	switch (rot) {
	case CAMERA_ORIENTATION_90:
	case CAMERA_ORIENTATION_270:
		// Swap width and height
		in_w = in.rows;
		in_h = in.cols;
		swapped_wh = true;
		break;
	default:
		in_w = in.cols;
		in_h = in.rows;
		break;
	}

	// Figure out from the rotation and frame sizes if the black bars need to be on top and bottom, or on left and
	// right?
	float scale_down_w = (float)out_size.w / in_w; // 128/1280 = 0.1
	float scale_down_h = (float)out_size.h / in_h; // 128/800 =  0.16

	float scale_down = fmin(scale_down_w, scale_down_h); // 0.1

	float width_inside, height_inside;

	if (swapped_wh) {
		width_inside = (float)in.rows * scale_down;
		height_inside = (float)in.cols * scale_down;
	} else {
		width_inside = (float)in.cols * scale_down;
		height_inside = (float)in.rows * scale_down;
	}

	float translate_x = (out_size.w - width_inside) / 2;  // should be 0 for 1280x800
	float translate_y = (out_size.h - height_inside) / 2; // should be (1280-800)/2 = 240

	cv::Matx23f go;
	cv::Point2f center(in.rows / 2, in.cols / 2);

	switch (rot) {
	case CAMERA_ORIENTATION_0:
		// clang-format off
			go(0,0) = scale_down;  go(0,1) = 0.0f;          go(0,2) = translate_x;
			go(1,0) = 0.0f;        go(1,1) = scale_down;    go(1,2) = translate_y;
		// clang-format on
		break;
	case CAMERA_ORIENTATION_90:
		// clang-format off
			go(0,0) = 0.0f;         go(0,1) = scale_down;   go(0,2) = translate_x;
			go(1,0) = -scale_down;  go(1,1) = 0.0f;         go(1,2) = translate_y+out_size.h-1;
		// clang-format on
		break;
	case CAMERA_ORIENTATION_180:
		// clang-format off
			go(0,0) = -scale_down;  go(0,1) = 0.0f;         go(0,2) = translate_x+out_size.w-1;
			go(1,0) = 0.0f;         go(1,1) = -scale_down;  go(1,2) = -translate_y+out_size.h-1;
		// clang-format on
		break;
	case CAMERA_ORIENTATION_270:
		// clang-format off
			go(0,0) = 0.0f;        go(0,1) = -scale_down;   go(0,2) = translate_x+out_size.w-1;
			go(1,0) = scale_down;  go(1,1) = 0.0f;          go(1,2) = translate_y;
		// clang-format on
		break;
	}

	cv::warpAffine(in, out, go, cv::Size(out_size.w, out_size.h));

	// Return the inverse affine transform by passing
	// through a 3x3 rotation matrix
	cv::Mat e = cv::Mat::eye(3, 3, CV_32F);
	cv::Mat tmp = e(cv::Rect(0, 0, 3, 2));
	cv::Mat(go).copyTo(tmp);

	e = e.inv();
	cv::Matx23f ret = e(cv::Rect(0, 0, 3, 2));

	return ret;
}

static inline int
argmax(const float *data, int size)
{
	float max_value = data[0];
	int out_idx = 0;

	for (int i = 1; i < size; i++) {
		if (data[i] > max_value) {
			max_value = data[i];
			out_idx = i;
		}
	}
	return out_idx;
}

static void
refine_center_of_distribution(
    const float *data, int coarse_x, int coarse_y, int w, int h, float *out_refined_x, float *out_refined_y)
{
	// Be VERY suspicious of this function, it's probably not centering correctly.
	float sum_of_values = 0;
	float sum_of_values_times_locations_x = 0;
	float sum_of_values_times_locations_y = 0;

	int max_kern_width = 10;


	//! @todo this is not good and has at least one edge case, make it more readable and link to a jupyter notebook
	int kern_width_x = std::max(0, std::min(coarse_x, std::min(max_kern_width, abs(coarse_x - w) - 1)));
	int kern_width_y = std::max(0, std::min(coarse_y, std::min(max_kern_width, abs(coarse_y - h) - 1)));
	int min_x = coarse_x - kern_width_x;
	int max_x = coarse_x + kern_width_x;

	int min_y = coarse_y - kern_width_y;
	int max_y = coarse_y + kern_width_y;


	for (int y = min_y; y <= max_y; y++) {
		for (int x = min_x; x <= max_x; x++) {
			int acc = (y * w) + x;
			float val = data[acc];
			sum_of_values += val;
			sum_of_values_times_locations_y += val * ((float)y + 0.5);
			sum_of_values_times_locations_x += val * ((float)x + 0.5);
		}
	}

	if (sum_of_values == 0) {
		// Edge case, will fix soon
		*out_refined_x = coarse_x;
		*out_refined_y = coarse_y;
		U_LOG_E("Failed! %d %d %d %d %d", coarse_x, coarse_y, w, h, max_kern_width);
		return;
	}

	*out_refined_x = sum_of_values_times_locations_x / sum_of_values;
	*out_refined_y = sum_of_values_times_locations_y / sum_of_values;
	return;
}

static float
average_size(const float *data, const float *data_loc, int coarse_x, int coarse_y, int w, int h)
{
	float sum = 0.0;
	float sum_of_values = 0;
	int max_kern_width = 10;
	int min_x = std::max(0, coarse_x - max_kern_width);
	int max_x = std::min(w, coarse_x + max_kern_width);

	int min_y = std::max(0, coarse_y - max_kern_width);
	int max_y = std::min(h, coarse_y + max_kern_width);


	assert(min_x >= 0);
	assert(max_x <= w);

	assert(min_y >= 0);
	assert(max_y <= h);

	for (int y = min_y; y < max_y; y++) {
		for (int x = min_x; x < max_x; x++) {
			int acc = (y * w) + x;
			float val = data[acc];
			float val_loc = data_loc[acc];
			sum += 1 * val_loc;
			sum_of_values += val * val_loc;
		}
	}

	assert(sum != 0);
	return sum_of_values / sum;
}

static void
normalizeGrayscaleImage(cv::Mat &data_in, cv::Mat &data_out)
{
	data_in.convertTo(data_out, CV_32FC1, 1 / 255.0);

	cv::Mat mean;
	cv::Mat stddev;
	cv::meanStdDev(data_out, mean, stddev);

	if (stddev.at<double>(0, 0) == 0) {
		U_LOG_W("Got image with zero standard deviation!");
		return;
	}

	data_out *= 0.25 / stddev.at<double>(0, 0);

	// Calculate it again; mean has changed. Yes we don't need to but it's easy
	//! @todo optimize
	cv::meanStdDev(data_out, mean, stddev);
	data_out += (0.5 - mean.at<double>(0, 0));
}

void
init_hand_detection(HandTracking *hgt, onnx_wrap *wrap)
{
	std::filesystem::path path = hgt->models_folder;

	path /= "grayscale_detection.onnx";

	wrap->input_name = "input_image_grayscale";
	wrap->input_shape[0] = 1;
	wrap->input_shape[1] = 1;
	wrap->input_shape[2] = 240;
	wrap->input_shape[3] = 320;

	wrap->api = OrtGetApiBase()->GetApi(ORT_API_VERSION);


	OrtSessionOptions *opts = nullptr;
	ORT(CreateSessionOptions(&opts));

	ORT(SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_ALL));
	ORT(SetIntraOpNumThreads(opts, 1));

	ORT(CreateEnv(ORT_LOGGING_LEVEL_FATAL, "monado_ht", &wrap->env));

	ORT(CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &wrap->meminfo));

	ORT(CreateSession(wrap->env, path.c_str(), opts, &wrap->session));
	assert(wrap->session != NULL);

	size_t input_size = wrap->input_shape[0] * wrap->input_shape[1] * wrap->input_shape[2] * wrap->input_shape[3];

	wrap->data = (float *)malloc(input_size * sizeof(float));

	ORT(CreateTensorWithDataAsOrtValue(wrap->meminfo,                       //
	                                   wrap->data,                          //
	                                   input_size * sizeof(float),          //
	                                   wrap->input_shape,                   //
	                                   4,                                   //
	                                   ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, //
	                                   &wrap->tensor));

	assert(wrap->tensor);
	int is_tensor;
	ORT(IsTensor(wrap->tensor, &is_tensor));
	assert(is_tensor);


	wrap->api->ReleaseSessionOptions(opts);
}


void
run_hand_detection(void *ptr)
{
	XRT_TRACE_MARKER();

	// ht_view *view = (ht_view *)ptr;

	hand_detection_run_info *info = (hand_detection_run_info *)ptr;
	ht_view *view = info->view;

	HandTracking *hgt = view->hgt;
	onnx_wrap *wrap = &view->detection;
	cv::Mat &data_400x640 = view->run_model_on_this;


	cv::Mat _240x320_uint8;

	xrt_size desire;
	desire.h = 240;
	desire.w = 320;

	cv::Matx23f go_back = blackbar(data_400x640, view->camera_info.camera_orientation, _240x320_uint8, desire);


	cv::Mat _240x320(cv::Size(320, 240), CV_32FC1, wrap->data, 320 * sizeof(float));

	normalizeGrayscaleImage(_240x320_uint8, _240x320);

	const char *output_name = "hand_locations_radii";

	OrtValue *output_tensor = nullptr;
	ORT(Run(wrap->session, nullptr, &wrap->input_name, &wrap->tensor, 1, &output_name, 1, &output_tensor));


	float *out_data = nullptr;

	ORT(GetTensorMutableData(output_tensor, (void **)&out_data));

	size_t plane_size = 80 * 60;

	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
		float *this_side_data = out_data + hand_idx * plane_size * 2;
		int max_idx = argmax(this_side_data, 4800);

		hand_bounding_box *output = info->outputs[hand_idx];

		output->found = this_side_data[max_idx] > 0.3;

		if (output->found) {
			output->confidence = this_side_data[max_idx];

			int row = max_idx / 80;
			int col = max_idx % 80;

			float size = average_size(this_side_data + plane_size, this_side_data, col, row, 80, 60);

			// model output width is between 0 and 1. multiply by image width and tuned factor
			constexpr float fac = 2.0f;
			size *= 320 * fac;
			size *= m_vec2_len({go_back(0, 0), go_back(0, 1)});

			float refined_x, refined_y;

			refine_center_of_distribution(this_side_data, col, row, 80, 60, &refined_x, &refined_y);

			cv::Mat &debug_frame = view->debug_out_to_this;

			xrt_vec2 _pt = {refined_x * 4, refined_y * 4};
			_pt = transformVecBy2x3(_pt, go_back);

			output->center = _pt;
			output->size_px = size;

			if (hgt->debug_scribble) {
				cv::Point2i pt(_pt.x, _pt.y);
				cv::rectangle(debug_frame,
				              cv::Rect(pt - cv::Point2i(size / 2, size / 2), cv::Size(size, size)),
				              PINK, 1);
			}
		}

		if (hgt->debug_scribble) {
			// note: this will multiply the model outputs by 255, don't do anything with them after this.
			int top_of_rect_y = 8; // 8 + 128 + 8 + 128 + 8;
			int left_of_rect_x = 8 + ((128 + 8) * 4);
			int start_y = top_of_rect_y + ((240 + 8) * view->view);
			int start_x = left_of_rect_x + 8 + 320 + 8;
			cv::Rect p = cv::Rect(left_of_rect_x, start_y, 320, 240);

			_240x320_uint8.copyTo(hgt->visualizers.mat(p));

			{
				cv::Rect p = cv::Rect(start_x + (hand_idx * (80 + 8)), start_y, 80, 60);
				cv::Mat start(cv::Size(80, 60), CV_32FC1, this_side_data, 80 * sizeof(float));
				start *= 255.0;
				start.copyTo(hgt->visualizers.mat(p));
			}

			{
				cv::Rect p = cv::Rect(start_x + (hand_idx * (80 + 8)), start_y + ((60 + 8)), 80, 60);
				cv::Mat start(cv::Size(80, 60), CV_32FC1, this_side_data + 4800, 80 * sizeof(float));
				start *= 255.0;
				start.copyTo(hgt->visualizers.mat(p));
			}
		}
	}

	wrap->api->ReleaseValue(output_tensor);
}

void
init_keypoint_estimation(HandTracking *hgt, onnx_wrap *wrap)
{

	std::filesystem::path path = hgt->models_folder;

	path /= "grayscale_keypoint_simdr.onnx";

	wrap->input_name = "inputImg";
	wrap->input_shape[0] = 1;
	wrap->input_shape[1] = 1;
	wrap->input_shape[2] = 128;
	wrap->input_shape[3] = 128;

	wrap->api = OrtGetApiBase()->GetApi(ORT_API_VERSION);


	OrtSessionOptions *opts = nullptr;
	ORT(CreateSessionOptions(&opts));

	ORT(SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_ALL));
	ORT(SetIntraOpNumThreads(opts, 1));


	ORT(CreateEnv(ORT_LOGGING_LEVEL_FATAL, "monado_ht", &wrap->env));

	ORT(CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &wrap->meminfo));

	ORT(CreateSession(wrap->env, path.c_str(), opts, &wrap->session));
	assert(wrap->session != NULL);

	size_t input_size = wrap->input_shape[0] * wrap->input_shape[1] * wrap->input_shape[2] * wrap->input_shape[3];

	wrap->data = (float *)malloc(input_size * sizeof(float));

	ORT(CreateTensorWithDataAsOrtValue(wrap->meminfo,                       //
	                                   wrap->data,                          //
	                                   input_size * sizeof(float),          //
	                                   wrap->input_shape,                   //
	                                   4,                                   //
	                                   ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, //
	                                   &wrap->tensor));

	assert(wrap->tensor);
	int is_tensor;
	ORT(IsTensor(wrap->tensor, &is_tensor));
	assert(is_tensor);


	wrap->api->ReleaseSessionOptions(opts);
}

void
run_keypoint_estimation(void *ptr)
{
	// data has already been filled with what we're meant to detect on
	XRT_TRACE_MARKER();

	keypoint_estimation_run_info *info = (keypoint_estimation_run_info *)ptr;

	onnx_wrap *wrap = &info->view->keypoint[info->hand_idx];
	struct HandTracking *hgt = info->view->hgt;

	cv::Mat &debug = info->view->debug_out_to_this;

	hand_bounding_box *output = &info->view->bboxes_this_frame[info->hand_idx];

	cv::Point2f src_tri[3];
	cv::Point2f dst_tri[3];
	// top-left
	cv::Point2f center = {output->center.x, output->center.y};

	cv::Point2f go_right = {output->size_px / 2, 0};
	cv::Point2f go_down = {0, output->size_px / 2};

	if (info->hand_idx == 1) {
		go_right *= -1;
	}
	// top left
	src_tri[0] = {center - go_down - go_right};

	// bottom left
	src_tri[1] = {center + go_down - go_right};

	// top right
	src_tri[2] = {center - go_down + go_right};

	dst_tri[0] = {0, 0};
	dst_tri[1] = {0, 128};
	dst_tri[2] = {128, 0};

	cv::Matx23f go_there = getAffineTransform(src_tri, dst_tri);
	cv::Matx23f go_back = getAffineTransform(dst_tri, src_tri);

	cv::Mat data_128x128_uint8;
	cv::warpAffine(info->view->run_model_on_this, data_128x128_uint8, go_there, cv::Size(128, 128),
	               cv::INTER_LINEAR);

	cv::Mat data_128x128_float(cv::Size(128, 128), CV_32FC1, wrap->data, 128 * sizeof(float));

	normalizeGrayscaleImage(data_128x128_uint8, data_128x128_float);

	const char *output_names[2] = {"x_axis_hmap", "y_axis_hmap"};

	OrtValue *output_tensor[2] = {nullptr, nullptr};

	ORT(Run(wrap->session, nullptr, &wrap->input_name, &wrap->tensor, 1, output_names, 2, output_tensor));

	// To here

	float *out_data_x = nullptr;
	float *out_data_y = nullptr;


	ORT(GetTensorMutableData(output_tensor[0], (void **)&out_data_x));
	ORT(GetTensorMutableData(output_tensor[1], (void **)&out_data_y));

	Hand2D &px_coord = info->view->keypoint_outputs[info->hand_idx].hand_px_coord;
	Hand2D &tan_space = info->view->keypoint_outputs[info->hand_idx].hand_tan_space;
	xrt_vec2 *keypoints_global = px_coord.kps;


	cv::Mat x(cv::Size(128, 21), CV_32FC1, out_data_x);
	cv::Mat y(cv::Size(128, 21), CV_32FC1, out_data_y);

	for (int i = 0; i < 21; i++) {
		int loc_x = argmax(&out_data_x[i * 128], 128);
		int loc_y = argmax(&out_data_y[i * 128], 128);
		xrt_vec2 loc;
		loc.x = loc_x;
		loc.y = loc_y;
		loc = transformVecBy2x3(loc, go_back);
		px_coord.kps[i] = loc;

		tan_space.kps[i] = raycoord(info->view, loc);
	}

	if (hgt->debug_scribble && hgt->tuneable_values.scribble_keypoint_model_outputs) {
		for (int finger = 0; finger < 5; finger++) {
			cv::Point last = {(int)keypoints_global[0].x, (int)keypoints_global[0].y};
			for (int joint = 0; joint < 4; joint++) {
				cv::Point the_new = {(int)keypoints_global[1 + finger * 4 + joint].x,
				                     (int)keypoints_global[1 + finger * 4 + joint].y};

				cv::line(debug, last, the_new, colors[info->hand_idx]);
				last = the_new;
			}
		}

		for (int i = 0; i < 21; i++) {
			xrt_vec2 loc = keypoints_global[i];
			handDot(debug, loc, 2, (float)(i) / 21.0, 1, 2);
		}
	}

	wrap->api->ReleaseValue(output_tensor[0]);
	wrap->api->ReleaseValue(output_tensor[1]);
}


void
init_keypoint_estimation_new(HandTracking *hgt, onnx_wrap *wrap)
{

	std::filesystem::path path = hgt->models_folder;

	path /= "grayscale_keypoint_new.onnx";

	// input_names = {"input_image_grayscale"};
	wrap->input_name = "inputImg";
	wrap->input_shape[0] = 1;
	wrap->input_shape[1] = 1;
	wrap->input_shape[2] = 128;
	wrap->input_shape[3] = 128;

	wrap->api = OrtGetApiBase()->GetApi(ORT_API_VERSION);


	OrtSessionOptions *opts = nullptr;
	ORT(CreateSessionOptions(&opts));

	// TODO review options, config for threads?
	ORT(SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_ALL));
	ORT(SetIntraOpNumThreads(opts, 1));


	ORT(CreateEnv(ORT_LOGGING_LEVEL_FATAL, "monado_ht", &wrap->env));

	ORT(CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &wrap->meminfo));

	// HG_DEBUG(this->device, "Loading hand detection model from file '%s'", path.c_str());
	ORT(CreateSession(wrap->env, path.c_str(), opts, &wrap->session));
	assert(wrap->session != NULL);

	size_t input_size = wrap->input_shape[0] * wrap->input_shape[1] * wrap->input_shape[2] * wrap->input_shape[3];

	wrap->data = (float *)malloc(input_size * sizeof(float));

	ORT(CreateTensorWithDataAsOrtValue(wrap->meminfo,                       //
	                                   wrap->data,                          //
	                                   input_size * sizeof(float),          //
	                                   wrap->input_shape,                   //
	                                   4,                                   //
	                                   ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, //
	                                   &wrap->tensor));

	assert(wrap->tensor);
	int is_tensor;
	ORT(IsTensor(wrap->tensor, &is_tensor));
	assert(is_tensor);


	wrap->api->ReleaseSessionOptions(opts);
}

void
calc_src_tri(cv::Point2f center,
             cv::Point2f go_right,
             cv::Point2f go_down,
             enum t_camera_orientation rot,
             cv::Point2f out_src_tri[3])
{
	// cv::Point2f go_right = {size_px / 2, 0};
	// cv::Point2f go_down = {0, size_px / 2};

	cv::Point2f top_left = {center - go_down - go_right};
	cv::Point2f bottom_left = {center + go_down - go_right};
	cv::Point2f bottom_right = {center + go_down + go_right};
	cv::Point2f top_right = {center - go_down + go_right};

	switch (rot) {
	case CAMERA_ORIENTATION_0: {

		// top left
		out_src_tri[0] = top_left; // {center - go_down - go_right};

		// bottom left
		out_src_tri[1] = bottom_left; //{center + go_down - go_right};

		// top right
		out_src_tri[2] = top_right; //{center - go_down + go_right};
	} break;
	case CAMERA_ORIENTATION_90: {

		// top left (becomes bottom left)
		out_src_tri[0] = bottom_left; //{center + go_down - go_right};

		// bottom left (becomes bottom right)
		out_src_tri[1] = bottom_right; //{center + go_down + go_right};

		// top right (becomes top left)
		out_src_tri[2] = top_left; //{center - go_down - go_right};
	} break;
	case CAMERA_ORIENTATION_180: {
		// top left (becomes bottom right)
		out_src_tri[0] = bottom_right;

		// bottom left (becomes top right)
		out_src_tri[1] = top_right;

		// top right (becomes bottom left)
		out_src_tri[2] = bottom_left;
	} break;
	case CAMERA_ORIENTATION_270: {
		// top left (becomes top right)
		out_src_tri[0] = top_right;

		// bottom left (becomes top left)
		out_src_tri[1] = top_left;

		// top right (becomes bottom right)
		out_src_tri[2] = bottom_right;
	}
	default: assert(false);
	}
}

void
make_keypoint_heatmap_output(int camera_idx, int hand_idx, int grid_pt_x, int grid_pt_y, float *plane, cv::Mat &out)
{
	int root_x = 8 + ((1 + 2 * hand_idx) * (128 + 8));
	int root_y = 8 + (camera_idx * (128 + 8));

	int org_x = (root_x) + (grid_pt_x * 25);
	int org_y = (root_y) + (grid_pt_y * 25);
	cv::Rect p = cv::Rect(org_x, org_y, 22, 22);


	cv::Mat start(cv::Size(22, 22), CV_32FC1, plane, 22 * sizeof(float));
	// cv::Mat start(cv::Size(40, 42), CV_32FC1, plane, 40 * 42 * sizeof(float));
	start *= 255.0;

	start.copyTo(out(p));
}


void
run_keypoint_estimation_new(void *ptr)
{
	XRT_TRACE_MARKER();
	keypoint_estimation_run_info *info = (keypoint_estimation_run_info *)ptr;

	onnx_wrap *wrap = &info->view->keypoint[info->hand_idx];
	struct HandTracking *hgt = info->view->hgt;

	// Factor out starting here

	cv::Mat &debug = info->view->debug_out_to_this;

	hand_bounding_box *output = &info->view->bboxes_this_frame[info->hand_idx];

	cv::Point2f src_tri[3];
	cv::Point2f dst_tri[3];
	// top-left
	cv::Point2f center = {output->center.x, output->center.y};

	cv::Point2f go_right = {output->size_px / 2, 0};
	cv::Point2f go_down = {0, output->size_px / 2};

	if (info->hand_idx == 1) {
		go_right *= -1;
	}

	calc_src_tri(center, go_right, go_down, info->view->camera_info.camera_orientation, src_tri);

	dst_tri[0] = {0, 0};
	dst_tri[1] = {0, 128};
	dst_tri[2] = {128, 0};

	cv::Matx23f go_there = getAffineTransform(src_tri, dst_tri);
	cv::Matx23f go_back = getAffineTransform(dst_tri, src_tri);

	cv::Mat data_128x128_uint8;

	{
		XRT_TRACE_IDENT(transforms);

		cv::warpAffine(info->view->run_model_on_this, data_128x128_uint8, go_there, cv::Size(128, 128),
		               cv::INTER_LINEAR);

		cv::Mat data_128x128_float(cv::Size(128, 128), CV_32FC1, wrap->data, 128 * sizeof(float));

		normalizeGrayscaleImage(data_128x128_uint8, data_128x128_float);
	}

	// Ending here

	const char *output_names[2] = {"heatmap"};

	OrtValue *output_tensor = nullptr;

	{
		XRT_TRACE_IDENT(model);
		ORT(Run(wrap->session, nullptr, &wrap->input_name, &wrap->tensor, 1, output_names, 1, &output_tensor));
	}

	// To here

	float *out_data = nullptr;


	ORT(GetTensorMutableData(output_tensor, (void **)&out_data));

	Hand2D &px_coord = info->view->keypoint_outputs[info->hand_idx].hand_px_coord;
	Hand2D &tan_space = info->view->keypoint_outputs[info->hand_idx].hand_tan_space;
	float *confidences = info->view->keypoint_outputs[info->hand_idx].hand_tan_space.confidences;
	xrt_vec2 *keypoints_global = px_coord.kps;

	size_t plane_size = 22 * 22;

	for (int i = 0; i < 21; i++) {
		float *data = &out_data[i * plane_size];
		int out_idx = argmax(data, 22 * 22);
		int row = out_idx / 22;
		int col = out_idx % 22;

		xrt_vec2 loc;


		refine_center_of_distribution(data, col, row, 22, 22, &loc.x, &loc.y);

		loc.x *= 128.0f / 22.0f;
		loc.y *= 128.0f / 22.0f;

		loc = transformVecBy2x3(loc, go_back);

		confidences[i] = data[out_idx];
		px_coord.kps[i] = loc;

		tan_space.kps[i] = raycoord(info->view, loc);
	}

	if (hgt->debug_scribble) {
		int data_acc_idx = 0;

		int root_x = 8 + ((2 * info->hand_idx) * (128 + 8));
		int root_y = 8 + (info->view->view * (128 + 8));

		cv::Rect p = cv::Rect(root_x, root_y, 128, 128);

		data_128x128_uint8.copyTo(hgt->visualizers.mat(p));

		make_keypoint_heatmap_output(info->view->view, info->hand_idx, 0, 0,
		                             out_data + (data_acc_idx * plane_size), hgt->visualizers.mat);
		data_acc_idx++;

		for (int finger = 0; finger < 5; finger++) {
			for (int joint = 0; joint < 4; joint++) {

				make_keypoint_heatmap_output(info->view->view, info->hand_idx, 1 + joint, finger,
				                             out_data + (data_acc_idx * plane_size),
				                             hgt->visualizers.mat);
				data_acc_idx++;
			}
		}



		if (hgt->tuneable_values.scribble_keypoint_model_outputs) {
			for (int finger = 0; finger < 5; finger++) {
				cv::Point last = {(int)keypoints_global[0].x, (int)keypoints_global[0].y};
				for (int joint = 0; joint < 4; joint++) {
					cv::Point the_new = {(int)keypoints_global[1 + finger * 4 + joint].x,
					                     (int)keypoints_global[1 + finger * 4 + joint].y};

					cv::line(debug, last, the_new, colors[info->hand_idx]);
					last = the_new;
				}
			}

			for (int i = 0; i < 21; i++) {
				xrt_vec2 loc = keypoints_global[i];
				handDot(debug, loc, 2, (float)(i) / 21.0, 1, 2);
			}
		}
	}

	wrap->api->ReleaseValue(output_tensor);
}

void
release_onnx_wrap(onnx_wrap *wrap)
{
	wrap->api->ReleaseMemoryInfo(wrap->meminfo);
	wrap->api->ReleaseSession(wrap->session);
	wrap->api->ReleaseValue(wrap->tensor);
	wrap->api->ReleaseEnv(wrap->env);
	free(wrap->data);
}

#ifdef USE_NCNN
int
ncnn_extractor_input_wrap(ncnn_extractor_t ex, const char *name, const ncnn_mat_t mat)
{
	XRT_TRACE_MARKER();
	return ncnn_extractor_input(ex, name, mat);
}

int
ncnn_extractor_extract_wrap(ncnn_extractor_t ex, const char *name, ncnn_mat_t *mat)
{
	XRT_TRACE_MARKER();
	return ncnn_extractor_extract(ex, name, mat);
}
ncnn_mat_t
ncnn_mat_from_pixels_resize_wrap(const unsigned char *pixels,
                                 int type,
                                 int w,
                                 int h,
                                 int stride,
                                 int target_width,
                                 int target_height,
                                 ncnn_allocator_t allocator)
{
	XRT_TRACE_MARKER();
	return ncnn_mat_from_pixels_resize(pixels, type, w, h, stride, target_width, target_height, allocator);
}

void
ncnn_mat_substract_mean_normalize_wrap(ncnn_mat_t mat, const float *mean_vals, const float *norm_vals)
{
	XRT_TRACE_MARKER();
	return ncnn_mat_substract_mean_normalize(mat, mean_vals, norm_vals);
}

void
run_hand_detection_ncnn(void *ptr)
{

	ht_view *view = (ht_view *)ptr;
	HandTracking *hgt = view->hgt;
	onnx_wrap *wrap = &view->detection;
	cv::Mat &data_400x640 = view->run_model_on_this;

	ncnn_mat_t in = ncnn_mat_from_pixels_resize(view->run_model_on_this.data, NCNN_MAT_PIXEL_GRAY, 640, 400,
	                                            view->run_model_on_this.step[0], 320, 240, NULL);

	const float norm_vals[3] = {1 / 255.0, 1 / 255.0, 1 / 255.0};
	ncnn_mat_substract_mean_normalize_wrap(in, 0, norm_vals);

	ncnn_option_t opt = ncnn_option_create();
	ncnn_option_set_use_vulkan_compute(opt, 1);

	ncnn_extractor_t ex = ncnn_extractor_create(hgt->net);

	ncnn_extractor_set_option(ex, opt);

	ncnn_extractor_input_wrap(ex, "input_image_grayscale", in);

	ncnn_mat_t out;
	ncnn_extractor_extract_wrap(ex, "hand_locations_radii", &out);
#if 0
	{
		int out_dims = ncnn_mat_get_dims(out);
		const int out_w = ncnn_mat_get_w(out);
		const int out_h = ncnn_mat_get_h(out);
		const int out_d = ncnn_mat_get_d(out);
		const int out_c = ncnn_mat_get_c(out);
		U_LOG_E("out: %d: %d %d %d %d", out_dims, out_w, out_h, out_d, out_c);
	}
	{
		int out_dims = ncnn_mat_get_dims(in);
		const int out_w = ncnn_mat_get_w(in);
		const int out_h = ncnn_mat_get_h(in);
		const int out_d = ncnn_mat_get_d(in);
		const int out_c = ncnn_mat_get_c(in);
		U_LOG_E("in: %d: %d %d %d %d", out_dims, out_w, out_h, out_d, out_c);
	}
#endif
	const float *out_data = (const float *)ncnn_mat_get_data(out);

	size_t plane_size = 80 * 60;


	cv::Scalar colors[2] = {{255, 255, 0}, {255, 0, 0}};


	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
		const float *this_side_data = out_data + hand_idx * plane_size * 2;
		int max_idx;

		det_output *output = &view->det_outputs[hand_idx];

		output->found = argmax(this_side_data, 4800, &max_idx) && this_side_data[max_idx] > 0.3;

		if (output->found) {

			int row = max_idx / 80;
			int col = max_idx % 80;



			output->size_px = average_size(this_side_data + plane_size, this_side_data, col, row, 80, 60);
			// output->size_px *= 640 * 1.6;
			// output->size_px *= 640 * 1.8;
			output->size_px *= 640 * 2.0;

			float size = output->size_px;

			float refined_x, refined_y;

			refine_center_of_distribution(this_side_data, col, row, 80, 60, &refined_x, &refined_y);

			output->center.x = refined_x * 8.0;
			output->center.y = refined_y * 6.6666666666666666667;
			// cv::Point2i pt(refined_x * 16.0 * .5, refined_y * 13.333333333333334 * .5);
			cv::Point2i pt(output->center.x, output->center.y);

			cv::Mat &debug_frame = view->debug_out_to_this;

			cv::rectangle(debug_frame, cv::Rect(pt - cv::Point2i(size / 2, size / 2), cv::Size(size, size)),
			              colors[hand_idx], 1);
		}
	}

	ncnn_extractor_destroy(ex);
	ncnn_mat_destroy(in);
	ncnn_mat_destroy(out);
}

void
run_keypoint_estimation_new_ncnn(void *ptr)
{
	XRT_TRACE_MARKER();
	keypoint_estimation_run_info *info = (keypoint_estimation_run_info *)ptr;

	struct HandTracking *hgt = info->view->hgt;

	// Factor out starting here

	cv::Mat &debug = info->view->debug_out_to_this;

	det_output *output = &info->view->det_outputs[info->hand_idx];

	cv::Point2f src_tri[3];
	cv::Point2f dst_tri[3];
	// top-left
	cv::Point2f center = {output->center.x, output->center.y};

	cv::Point2f go_right = {output->size_px / 2, 0};
	cv::Point2f go_down = {0, output->size_px / 2};

	if (info->hand_idx == 1) {
		go_right *= -1;
	}
	// top left
	src_tri[0] = {center - go_down - go_right};

	// bottom left
	src_tri[1] = {center + go_down - go_right};

	// top right
	src_tri[2] = {center - go_down + go_right};

	dst_tri[0] = {0, 0};
	dst_tri[1] = {0, 128};
	dst_tri[2] = {128, 0};

	cv::Matx23f go_there = getAffineTransform(src_tri, dst_tri);
	cv::Matx23f go_back = getAffineTransform(dst_tri, src_tri);

	XRT_TRACE_IDENT(transforms);

	cv::Mat data_128x128_uint8;

	cv::warpAffine(info->view->run_model_on_this, data_128x128_uint8, go_there, cv::Size(128, 128),
	               cv::INTER_LINEAR);

	// cv::Mat data_128x128_float(cv::Size(128, 128), CV_32FC1);

	// normalizeGrayscaleImage(data_128x128_uint8, data_128x128_float);

	ncnn_mat_t in = ncnn_mat_from_pixels(data_128x128_uint8.data, NCNN_MAT_PIXEL_GRAY, 128, 128,
	                                     data_128x128_uint8.step[0], NULL);

	const float norm_vals[3] = {1 / 255.0, 1 / 255.0, 1 / 255.0};
	ncnn_mat_substract_mean_normalize_wrap(in, 0, norm_vals);

	ncnn_option_t opt = ncnn_option_create();
	ncnn_option_set_use_vulkan_compute(opt, 1);

	ncnn_extractor_t ex = ncnn_extractor_create(hgt->net_keypoint);

	ncnn_extractor_set_option(ex, opt);

	ncnn_extractor_input_wrap(ex, "inputImg", in);

	ncnn_mat_t out;
	ncnn_extractor_extract_wrap(ex, "heatmap", &out);



	// Ending here


	const float *out_data = (const float *)ncnn_mat_get_data(out);



	Hand2D &px_coord = info->view->keypoint_outputs[info->hand_idx].hand_px_coord;
	Hand2D &tan_space = info->view->keypoint_outputs[info->hand_idx].hand_tan_space;
	xrt_vec2 *keypoints_global = px_coord.kps;

	size_t plane_size = 22 * 22;

	for (int i = 0; i < 21; i++) {
		const float *data = &out_data[i * plane_size];
		int out_idx = 0;
		argmax(data, 22 * 22, &out_idx);
		int row = out_idx / 22;
		int col = out_idx % 22;

		xrt_vec2 loc;

		refine_center_of_distribution(data, col, row, 22, 22, &loc.x, &loc.y);

		loc.x *= 128.0f / 22.0f;
		loc.y *= 128.0f / 22.0f;

		loc = transformVecBy2x3(loc, go_back);
		px_coord.kps[i] = loc;

		tan_space.kps[i] = raycoord(info->view, loc);
	}

	if (hgt->debug_scribble) {
		for (int finger = 0; finger < 5; finger++) {
			cv::Point last = {(int)keypoints_global[0].x, (int)keypoints_global[0].y};
			for (int joint = 0; joint < 4; joint++) {
				cv::Point the_new = {(int)keypoints_global[1 + finger * 4 + joint].x,
				                     (int)keypoints_global[1 + finger * 4 + joint].y};

				cv::line(debug, last, the_new, colors[info->hand_idx]);
				last = the_new;
			}
		}

		for (int i = 0; i < 21; i++) {
			xrt_vec2 loc = keypoints_global[i];
			handDot(debug, loc, 2, (float)(i) / 21.0, 1, 2);
		}
	}
	ncnn_extractor_destroy(ex);
	ncnn_mat_destroy(in);
	ncnn_mat_destroy(out);
}

#endif

} // namespace xrt::tracking::hand::mercury
