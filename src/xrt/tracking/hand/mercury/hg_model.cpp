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
			go(0,0) = 0.0f;        go(0,1) = -scale_down;   go(0,2) = -translate_x+out_size.w-1;
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
setup_ort_api(HandTracking *hgt, onnx_wrap *wrap, std::filesystem::path path)
{
	wrap->api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
	OrtSessionOptions *opts = nullptr;

	ORT(CreateSessionOptions(&opts));

	ORT(SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_ALL));
	ORT(SetIntraOpNumThreads(opts, 1));

	ORT(CreateEnv(ORT_LOGGING_LEVEL_FATAL, "monado_ht", &wrap->env));

	ORT(CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &wrap->meminfo));

	ORT(CreateSession(wrap->env, path.c_str(), opts, &wrap->session));
	assert(wrap->session != NULL);
	wrap->api->ReleaseSessionOptions(opts);
}

void
setup_model_image_input(HandTracking *hgt, onnx_wrap *wrap, const char *name, int64_t w, int64_t h)
{
	model_input_wrap inputimg = {};
	inputimg.name = name;
	inputimg.dimensions.push_back(1);
	inputimg.dimensions.push_back(1);
	inputimg.dimensions.push_back(h);
	inputimg.dimensions.push_back(w);
	size_t data_size = w * h * sizeof(float);
	inputimg.data = (float *)malloc(data_size);

	ORT(CreateTensorWithDataAsOrtValue(wrap->meminfo,                       //
	                                   inputimg.data,                       //
	                                   data_size,                           //
	                                   inputimg.dimensions.data(),          //
	                                   inputimg.dimensions.size(),          //
	                                   ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, //
	                                   &inputimg.tensor));

	assert(inputimg.tensor);
	int is_tensor;
	ORT(IsTensor(inputimg.tensor, &is_tensor));
	assert(is_tensor);

	wrap->wraps.push_back(inputimg);
}

void
init_hand_detection(HandTracking *hgt, onnx_wrap *wrap)
{
	std::filesystem::path path = hgt->models_folder;

	path /= "grayscale_detection_160x160.onnx";

	wrap->wraps.clear();

	setup_ort_api(hgt, wrap, path);

	setup_model_image_input(hgt, wrap, "inputImg", kDetectionInputSize, kDetectionInputSize);
}


void
run_hand_detection(void *ptr)
{
	XRT_TRACE_MARKER();

	hand_detection_run_info *info = (hand_detection_run_info *)ptr;
	ht_view *view = info->view;
	HandTracking *hgt = view->hgt;
	onnx_wrap *wrap = &view->detection;

	cv::Mat &orig_data = view->run_model_on_this;

	cv::Mat binned_uint8;

	xrt_size desired_bin_size;
	desired_bin_size.h = kDetectionInputSize;
	desired_bin_size.w = kDetectionInputSize;

	cv::Matx23f go_back = blackbar(orig_data, view->camera_info.camera_orientation, binned_uint8, desired_bin_size);

	cv::Mat binned_float_wrapper_mat(cv::Size(kDetectionInputSize, kDetectionInputSize),
	                                 CV_32FC1,            //
	                                 wrap->wraps[0].data, //
	                                 kDetectionInputSize * sizeof(float));

	normalizeGrayscaleImage(binned_uint8, binned_float_wrapper_mat);

	const OrtValue *inputs[] = {wrap->wraps[0].tensor};
	const char *input_names[] = {wrap->wraps[0].name};

	OrtValue *output_tensors[] = {nullptr, nullptr, nullptr, nullptr};
	const char *output_names[] = {"hand_exists", "cx", "cy", "size"};

	{
		XRT_TRACE_IDENT(model);
		static_assert(ARRAY_SIZE(input_names) == ARRAY_SIZE(inputs));
		static_assert(ARRAY_SIZE(output_names) == ARRAY_SIZE(output_tensors));
		ORT(Run(wrap->session, nullptr, input_names, inputs, ARRAY_SIZE(input_names), output_names,
		        ARRAY_SIZE(output_names), output_tensors));
	}

	float *hand_exists = nullptr;
	float *cx = nullptr;
	float *cy = nullptr;
	float *sizee = nullptr;

	ORT(GetTensorMutableData(output_tensors[0], (void **)&hand_exists));
	ORT(GetTensorMutableData(output_tensors[1], (void **)&cx));
	ORT(GetTensorMutableData(output_tensors[2], (void **)&cy));
	ORT(GetTensorMutableData(output_tensors[3], (void **)&sizee));



	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
		hand_bounding_box *output = info->outputs[hand_idx];

		output->found = hand_exists[hand_idx] > 0.3;

		if (output->found) {
			output->confidence = hand_exists[hand_idx];

			xrt_vec2 _pt = {};
			_pt.x = math_map_ranges(cx[hand_idx], -1, 1, 0, kDetectionInputSize);
			_pt.y = math_map_ranges(cy[hand_idx], -1, 1, 0, kDetectionInputSize);

			float size = sizee[hand_idx];



			constexpr float fac = 2.0f;
			size *= kDetectionInputSize * fac;
			size *= m_vec2_len({go_back(0, 0), go_back(0, 1)});


			cv::Mat &debug_frame = view->debug_out_to_this;

			_pt = transformVecBy2x3(_pt, go_back);

			output->center = _pt;
			output->size_px = size;

			if (hgt->debug_scribble) {
				cv::Point2i pt((int)output->center.x, (int)output->center.y);
				cv::rectangle(debug_frame,
				              cv::Rect(pt - cv::Point2i(size / 2, size / 2), cv::Size(size, size)),
				              PINK, 1);
			}
		}

		if (hgt->debug_scribble) {
			// note: this will multiply the model outputs by 255, don't do anything with them after this.
			int top_of_rect_y = kVisSpacerSize; // 8 + 128 + 8 + 128 + 8;
			int left_of_rect_x = kVisSpacerSize + ((kKeypointInputSize + kVisSpacerSize) * 4);
			int start_y = top_of_rect_y + ((kDetectionInputSize + kVisSpacerSize) * view->view);
			cv::Rect p = cv::Rect(left_of_rect_x, start_y, kDetectionInputSize, kDetectionInputSize);

			binned_uint8.copyTo(hgt->visualizers.mat(p));
		}
	}

	for (size_t i = 0; i < ARRAY_SIZE(output_tensors); i++) {
		wrap->api->ReleaseValue(output_tensors[i]);
	}
}

void
init_keypoint_estimation(HandTracking *hgt, onnx_wrap *wrap)
{

	std::filesystem::path path = hgt->models_folder;

	path /= "grayscale_keypoint_new.onnx";

	wrap->wraps.clear();

	setup_ort_api(hgt, wrap, path);

	setup_model_image_input(hgt, wrap, "inputImg", kKeypointInputSize, kKeypointInputSize);
}

void
calc_src_tri(cv::Point2f center,
             cv::Point2f go_right,
             cv::Point2f go_down,
             enum t_camera_orientation rot,
             cv::Point2f out_src_tri[3])
{
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
		// Need to rotate the view back by -90°
		// top left (becomes top right)
		out_src_tri[0] = top_right;

		// bottom left (becomes top left)
		out_src_tri[1] = top_left;

		// top right (becomes bottom right)
		out_src_tri[2] = bottom_right;
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
		// Need to rotate the view clockwise 90°
		// top left (becomes bottom left)
		out_src_tri[0] = bottom_left; //{center + go_down - go_right};

		// bottom left (becomes bottom right)
		out_src_tri[1] = bottom_right; //{center + go_down + go_right};

		// top right (becomes top left)
		out_src_tri[2] = top_left; //{center - go_down - go_right};
	} break;
	default: assert(false);
	}
}

void
make_keypoint_heatmap_output(int camera_idx, int hand_idx, int grid_pt_x, int grid_pt_y, float *plane, cv::Mat &out)
{
	int root_x = kVisSpacerSize + ((1 + 2 * hand_idx) * (kKeypointInputSize + kVisSpacerSize));
	int root_y = kVisSpacerSize + (camera_idx * (kKeypointInputSize + kVisSpacerSize));

	int org_x = (root_x) + (grid_pt_x * 25);
	int org_y = (root_y) + (grid_pt_y * 25);
	cv::Rect p = cv::Rect(org_x, org_y, 22, 22);


	cv::Mat start(cv::Size(22, 22), CV_32FC1, plane, 22 * sizeof(float));
	start *= 255.0;

	start.copyTo(out(p));
}


void
run_keypoint_estimation(void *ptr)
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

	calc_src_tri(center, go_right, go_down, info->view->camera_info.camera_orientation, src_tri);

	/* For the right hand, flip the result horizontally since
	 * the model is trained on left hands.
	 * Top left, bottom left, top right */
	if (info->hand_idx == 1) {
		dst_tri[0] = {kKeypointInputSize, 0};
		dst_tri[1] = {kKeypointInputSize, kKeypointInputSize};
		dst_tri[2] = {0, 0};
	} else {
		dst_tri[0] = {0, 0};
		dst_tri[1] = {0, kKeypointInputSize};
		dst_tri[2] = {kKeypointInputSize, 0};
	}

	cv::Matx23f go_there = getAffineTransform(src_tri, dst_tri);
	cv::Matx23f go_back = getAffineTransform(dst_tri, src_tri); // NOLINT

	cv::Mat cropped_image_uint8;

	{
		XRT_TRACE_IDENT(transforms);

		cv::warpAffine(info->view->run_model_on_this, cropped_image_uint8, go_there,
		               cv::Size(kKeypointInputSize, kKeypointInputSize), cv::INTER_LINEAR);

		cv::Mat cropped_image_float_wrapper(cv::Size(kKeypointInputSize, kKeypointInputSize), //
		                                    CV_32FC1,                                         //
		                                    wrap->wraps[0].data,                              //
		                                    kKeypointInputSize * sizeof(float));

		normalizeGrayscaleImage(cropped_image_uint8, cropped_image_float_wrapper);
	}

	// Ending here

	const OrtValue *inputs[] = {wrap->wraps[0].tensor};
	const char *input_names[] = {wrap->wraps[0].name};

	OrtValue *output_tensors[] = {nullptr};

	const char *output_names[] = {"heatmap"};


	// OrtValue *output_tensor = nullptr;

	{
		XRT_TRACE_IDENT(model);
		static_assert(ARRAY_SIZE(input_names) == ARRAY_SIZE(inputs));
		static_assert(ARRAY_SIZE(output_names) == ARRAY_SIZE(output_tensors));
		ORT(Run(wrap->session, nullptr, input_names, inputs, ARRAY_SIZE(input_names), output_names,
		        ARRAY_SIZE(output_names), output_tensors));
	}

	// To here

	float *out_data = nullptr;


	ORT(GetTensorMutableData(output_tensors[0], (void **)&out_data));

	Hand2D &px_coord = info->view->keypoint_outputs[info->hand_idx].hand_px_coord;
	Hand2D &tan_space = info->view->keypoint_outputs[info->hand_idx].hand_tan_space;
	float *confidences = info->view->keypoint_outputs[info->hand_idx].hand_tan_space.confidences;
	xrt_vec2 *keypoints_global = px_coord.kps;

	size_t plane_size = kKeypointOutputHeatmapSize * kKeypointOutputHeatmapSize;

	for (int i = 0; i < 21; i++) {
		float *data = &out_data[i * plane_size];
		int out_idx = argmax(data, kKeypointOutputHeatmapSize * kKeypointOutputHeatmapSize);
		int row = out_idx / kKeypointOutputHeatmapSize;
		int col = out_idx % kKeypointOutputHeatmapSize;

		xrt_vec2 loc;


		refine_center_of_distribution(data, col, row, kKeypointOutputHeatmapSize, kKeypointOutputHeatmapSize,
		                              &loc.x, &loc.y);

		// 128.0/22.0f
		loc.x *= float(kKeypointInputSize) / float(kKeypointOutputHeatmapSize);
		loc.y *= float(kKeypointInputSize) / float(kKeypointOutputHeatmapSize);

		loc = transformVecBy2x3(loc, go_back);

		confidences[i] = data[out_idx];
		px_coord.kps[i] = loc;

		tan_space.kps[i] = raycoord(info->view, loc);
	}

	if (hgt->debug_scribble) {
		int data_acc_idx = 0;

		int root_x = kVisSpacerSize + ((2 * info->hand_idx) * (kKeypointInputSize + kVisSpacerSize));
		int root_y = kVisSpacerSize + (info->view->view * (kKeypointInputSize + kVisSpacerSize));

		cv::Rect p = cv::Rect(root_x, root_y, kKeypointInputSize, kKeypointInputSize);

		cropped_image_uint8.copyTo(hgt->visualizers.mat(p));

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

	wrap->api->ReleaseValue(output_tensors[0]);
}

void
release_onnx_wrap(onnx_wrap *wrap)
{
	wrap->api->ReleaseMemoryInfo(wrap->meminfo);
	wrap->api->ReleaseSession(wrap->session);
	for (model_input_wrap &a : wrap->wraps) {
		wrap->api->ReleaseValue(a.tensor);
		free(a.data);
	}
	wrap->api->ReleaseEnv(wrap->env);
}

} // namespace xrt::tracking::hand::mercury
