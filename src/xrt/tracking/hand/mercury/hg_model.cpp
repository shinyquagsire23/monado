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
#include "hg_numerics_checker.hpp"


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

static bool
hand_depth_center_of_mass(struct HandTracking *hgt, float data[22], float *out_depth, float *out_confidence)
{
	float avg_location_px_coord = 0;
	float sum = 0;

	for (int i = 0; i < 22; i++) {
		data[i] = fmin(1.0, fmax(data[i], 0.0));
		sum += data[i];
		avg_location_px_coord += data[i] * (float)i;
	}

	if (sum < 1e-5) {
		HG_DEBUG(hgt, "All depth outputs were zero!");
		return false;
	}

	avg_location_px_coord /= sum;

	// std::cout << avg_location_px_coord << std::endl;

	// bounds check
	if (avg_location_px_coord < 0 || avg_location_px_coord > 21) {
		HG_DEBUG(hgt, "Very bad! avg_location_px_coord was %f", avg_location_px_coord);
		for (int i = 0; i < 22; i++) {
			HG_DEBUG(hgt, "%f", data[i]);
		}

		avg_location_px_coord = fmin(21.0, fmax(0.0, avg_location_px_coord));
		return false;
	}

	// nan check
	if (avg_location_px_coord != avg_location_px_coord) {
		HG_DEBUG(hgt, "Very bad! avg_location_px_coord was not a number: %f", avg_location_px_coord);
		for (int i = 0; i < 22; i++) {
			HG_DEBUG(hgt, "%f", data[i]);
		}
		*out_depth = 0;
		*out_confidence = 0;
		return false;
	}
	// printf("%f %d\n", avg_location_px_coord, (int)avg_location_px_coord);
	*out_confidence = data[(int)avg_location_px_coord];



	float depth_value = avg_location_px_coord + 0.5;
	depth_value /= 22;
	depth_value -= 0.5;
	depth_value *= 2 * 1.5;

	*out_depth = depth_value;
	return true;
}

static bool
refine_center_of_distribution(struct HandTracking *hgt, //
                              const float *data,        //
                              int coarse_x,             //
                              int coarse_y,             //
                              int w,                    //
                              int h,                    //
                              float *out_refined_x,     //
                              float *out_refined_y)
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
		HG_DEBUG(hgt, "Failed! %d %d %d %d %d", coarse_x, coarse_y, w, h, max_kern_width);
		return false;
	}
	*out_refined_x = sum_of_values_times_locations_x / sum_of_values;
	*out_refined_y = sum_of_values_times_locations_y / sum_of_values;
	return true;
}

static bool
normalizeGrayscaleImage(cv::Mat &data_in, cv::Mat &data_out)
{
	data_in.convertTo(data_out, CV_32FC1, 1 / 255.0);

	cv::Mat mean;
	cv::Mat stddev;
	cv::meanStdDev(data_out, mean, stddev);

	if (stddev.at<double>(0, 0) == 0) {
		U_LOG_W("Got image with zero standard deviation!");
		return false;
	}

	data_out *= 0.25 / stddev.at<double>(0, 0);

	// Calculate it again; mean has changed. Yes we don't need to but it's easy
	//! @todo optimize
	cv::meanStdDev(data_out, mean, stddev);
	data_out += (0.5 - mean.at<double>(0, 0));
	return true;
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
	inputimg.dimensions[0] = 1;
	inputimg.dimensions[1] = 1;
	inputimg.dimensions[2] = h;
	inputimg.dimensions[3] = w;
	inputimg.num_dimensions = 4;
	size_t data_size = w * h * sizeof(float);
	inputimg.data = (float *)malloc(data_size);

	ORT(CreateTensorWithDataAsOrtValue(wrap->meminfo,                       //
	                                   inputimg.data,                       //
	                                   data_size,                           //
	                                   inputimg.dimensions,                 //
	                                   inputimg.num_dimensions,             //
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
		hand_region_of_interest &output = info->outputs[hand_idx];

		output.found = hand_exists[hand_idx] > 0.3;

		if (output.found) {
			output.hand_detection_confidence = hand_exists[hand_idx];

			xrt_vec2 _pt = {};
			_pt.x = math_map_ranges(cx[hand_idx], -1, 1, 0, kDetectionInputSize);
			_pt.y = math_map_ranges(cy[hand_idx], -1, 1, 0, kDetectionInputSize);

			float size = sizee[hand_idx];



			constexpr float fac = 2.0f;
			size *= kDetectionInputSize * fac;
			size *= m_vec2_len({go_back(0, 0), go_back(0, 1)});


			cv::Mat &debug_frame = view->debug_out_to_this;

			_pt = transformVecBy2x3(_pt, go_back);

			output.center_px = _pt;
			output.size_px = size;

			if (hgt->debug_scribble) {
				handSquare(debug_frame, output.center_px, output.size_px, PINK);
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

	path /= "grayscale_keypoint_jan18.onnx";

	wrap->wraps.clear();


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

	// size_t input_size = wrap->input_shape[0] * wrap->input_shape[1] * wrap->input_shape[2] *
	// wrap->input_shape[3];

	// wrap->data = (float *)malloc(input_size * sizeof(float));
	{
		model_input_wrap inputimg = {};
		inputimg.name = "inputImg";
		inputimg.dimensions[0] = 1;
		inputimg.dimensions[1] = 1;
		inputimg.dimensions[2] = 128;
		inputimg.dimensions[3] = 128;
		inputimg.num_dimensions = 4;

		inputimg.data = (float *)malloc(128 * 128 * sizeof(float)); // SORRY IM BUSY



		ORT(CreateTensorWithDataAsOrtValue(wrap->meminfo,                       //
		                                   inputimg.data,                       //
		                                   128 * 128 * sizeof(float),           //
		                                   inputimg.dimensions,                 //
		                                   inputimg.num_dimensions,             //
		                                   ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, //
		                                   &inputimg.tensor));

		assert(inputimg.tensor);
		int is_tensor;
		ORT(IsTensor(inputimg.tensor, &is_tensor));
		assert(is_tensor);

		wrap->wraps.push_back(inputimg);
	}

	{
		model_input_wrap inputimg = {};
		inputimg.name = "lastKeypoints";
		inputimg.dimensions[0] = 1;
		inputimg.dimensions[1] = 42;
		inputimg.num_dimensions = 2;

		inputimg.data = (float *)malloc(1 * 42 * sizeof(float)); // SORRY IM BUSY



		ORT(CreateTensorWithDataAsOrtValue(wrap->meminfo,                       //
		                                   inputimg.data,                       //
		                                   1 * 42 * sizeof(float),              //
		                                   inputimg.dimensions,                 //
		                                   inputimg.num_dimensions,             //
		                                   ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, //
		                                   &inputimg.tensor));

		assert(inputimg.tensor);
		int is_tensor;
		ORT(IsTensor(inputimg.tensor, &is_tensor));
		assert(is_tensor);
		wrap->wraps.push_back(inputimg);
	}

	{
		model_input_wrap inputimg = {};
		inputimg.name = "useLastKeypoints";
		inputimg.dimensions[0] = 1;
		inputimg.num_dimensions = 1;

		inputimg.data = (float *)malloc(1 * sizeof(float)); // SORRY IM BUSY



		ORT(CreateTensorWithDataAsOrtValue(wrap->meminfo,                       //
		                                   inputimg.data,                       //
		                                   1 * sizeof(float),                   //
		                                   inputimg.dimensions,                 //
		                                   inputimg.num_dimensions,             //
		                                   ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, //
		                                   &inputimg.tensor));

		assert(inputimg.tensor);
		int is_tensor;
		ORT(IsTensor(inputimg.tensor, &is_tensor));
		assert(is_tensor);
		wrap->wraps.push_back(inputimg);
	}


	wrap->api->ReleaseSessionOptions(opts);
}

enum xrt_hand_joint joints_ml_to_xr[21]{
    XRT_HAND_JOINT_WRIST,               //
    XRT_HAND_JOINT_THUMB_METACARPAL,    //
    XRT_HAND_JOINT_THUMB_PROXIMAL,      //
    XRT_HAND_JOINT_THUMB_DISTAL,        //
    XRT_HAND_JOINT_THUMB_TIP,           //
                                        //
    XRT_HAND_JOINT_INDEX_PROXIMAL,      //
    XRT_HAND_JOINT_INDEX_INTERMEDIATE,  //
    XRT_HAND_JOINT_INDEX_DISTAL,        //
    XRT_HAND_JOINT_INDEX_TIP,           //
                                        //
    XRT_HAND_JOINT_MIDDLE_PROXIMAL,     //
    XRT_HAND_JOINT_MIDDLE_INTERMEDIATE, //
    XRT_HAND_JOINT_MIDDLE_DISTAL,       //
    XRT_HAND_JOINT_MIDDLE_TIP,          //
                                        //
    XRT_HAND_JOINT_RING_PROXIMAL,       //
    XRT_HAND_JOINT_RING_INTERMEDIATE,   //
    XRT_HAND_JOINT_RING_DISTAL,         //
    XRT_HAND_JOINT_RING_TIP,            //
                                        //
    XRT_HAND_JOINT_LITTLE_PROXIMAL,     //
    XRT_HAND_JOINT_LITTLE_INTERMEDIATE, //
    XRT_HAND_JOINT_LITTLE_DISTAL,       //
    XRT_HAND_JOINT_LITTLE_TIP,          //
};

void
make_keypoint_heatmap_output(int camera_idx, int hand_idx, int grid_pt_x, int grid_pt_y, float *plane, cv::Mat &out)
{
	int root_x = 8 + ((1 + 2 * hand_idx) * (128 + 8));
	int root_y = 8 + ((2 * camera_idx) * (128 + 8));

	int org_x = (root_x) + (grid_pt_x * 25);
	int org_y = (root_y) + (grid_pt_y * 25);
	cv::Rect p = cv::Rect(org_x, org_y, 22, 22);


	cv::Mat start(cv::Size(22, 22), CV_32FC1, plane, 22 * sizeof(float));
	start *= 255.0;

	start.copyTo(out(p));
}

void
make_keypoint_depth_heatmap_output(int camera_idx, //
                                   int hand_idx,   //
                                   int grid_pt_x,  //
                                   int grid_pt_y,
                                   float *plane, //
                                   cv::Mat &out)
{
	int root_x = 8 + ((1 + 2 * hand_idx) * (128 + 8));
	int root_y = 8 + ((1 + 2 * camera_idx) * (128 + 8));

	int org_x = (root_x) + (grid_pt_x * 25);
	int org_y = (root_y) + (grid_pt_y * 25);


	cv::Rect p = cv::Rect(org_x, org_y, 22, 22);


	cv::Mat start(cv::Size(22, 22), CV_32FC1);
	float *ptr = (float *)start.data; // Cope cope cope cope cope


	for (int i = 0; i < 22; i++) {
		for (int j = 0; j < 22; j++) {
			ptr[(i * 22) + j] = plane[i];
		}
	}

	start *= 255.0;

	start.copyTo(out(p));
}

void
set_predicted_zero(float *data)
{
	for (int i = 0; i < 42; i++) {
		data[i] = 0.0f;
	}
}

void
run_keypoint_estimation(void *ptr)
{
	XRT_TRACE_MARKER();
	keypoint_estimation_run_info info = *(keypoint_estimation_run_info *)ptr;

	onnx_wrap *wrap = &info.view->keypoint[info.hand_idx];
	struct HandTracking *hgt = info.view->hgt;

	int view_idx = info.view->view;
	int hand_idx = info.hand_idx;
	one_frame_one_view &this_output = hgt->keypoint_outputs[hand_idx].views[view_idx];
	MLOutput2D &px_coord = this_output.keypoints_in_scaled_stereographic;
	// Factor out starting here

	hand_region_of_interest &output = info.view->regions_of_interest_this_frame[hand_idx];

	cv::Mat data_128x128_uint8;

	projection_instructions instr(info.view->hgdist);
	instr.rot_quat = Eigen::Quaternionf::Identity();
	instr.stereographic_radius = 0.4;



	t_camera_model_params dist = info.view->hgdist;


	float twist = 0;

	if (output.provenance == ROIProvenance::HAND_DETECTION) {

		xrt_vec3 center;

		xrt_vec3 edges[4];


		t_camera_models_unproject_and_flip(&hgt->views[view_idx].hgdist, output.center_px.x, output.center_px.y,
		                                   &center.x, &center.y, &center.z);

		xrt_vec2 r = {output.size_px / 2, 0};
		xrt_vec2 d = {0, output.size_px / 2};
		xrt_vec2 v;

		// note! we do not need to rotate this! it's *already* in camera space.
		int acc_idx = 0;
		v = output.center_px + r + d;
		t_camera_models_unproject_and_flip(&hgt->views[view_idx].hgdist, v.x, v.y, &edges[acc_idx].x,
		                                   &edges[acc_idx].y, &edges[acc_idx].z);
		acc_idx++;

		v = output.center_px - r + d;
		t_camera_models_unproject_and_flip(&hgt->views[view_idx].hgdist, v.x, v.y, &edges[acc_idx].x,
		                                   &edges[acc_idx].y, &edges[acc_idx].z);
		acc_idx++;

		v = output.center_px - r - d;
		t_camera_models_unproject_and_flip(&hgt->views[view_idx].hgdist, v.x, v.y, &edges[acc_idx].x,
		                                   &edges[acc_idx].y, &edges[acc_idx].z);
		acc_idx++;

		v = output.center_px + r - d;
		t_camera_models_unproject_and_flip(&hgt->views[view_idx].hgdist, v.x, v.y, &edges[acc_idx].x,
		                                   &edges[acc_idx].y, &edges[acc_idx].z);


		float angle = 0;

		for (int i = 0; i < 4; i++) {
			angle = fmaxf(angle, m_vec3_angle(center, edges[i]));
		}


		make_projection_instructions_angular(center, hand_idx, angle,
		                                     hgt->tuneable_values.after_detection_fac.val, twist, instr);

		wrap->wraps[2].data[0] = 0.0f;
		set_predicted_zero(wrap->wraps[1].data);
	} else {
		Eigen::Array<float, 3, 21> keypoints_in_camera;


		if (view_idx == 0) {
			keypoints_in_camera = hgt->pose_predicted_keypoints[hand_idx];
		} else {
			for (int i = 0; i < 21; i++) {

				Eigen::Quaternionf ori = map_quat(hgt->left_in_right.orientation);
				Eigen::Vector3f tmp = hgt->pose_predicted_keypoints[hand_idx].col(i);

				tmp = ori * tmp;
				tmp += map_vec3(hgt->left_in_right.position);
				keypoints_in_camera.col(i) = tmp;
			}
		}

		hand21_2d bleh;

		make_projection_instructions(dist, hand_idx, hgt->tuneable_values.dyn_radii_fac.val, twist,
		                             keypoints_in_camera, instr, bleh);

		if (hgt->tuneable_values.enable_pose_predicted_input) {
			for (int ml_joint_idx = 0; ml_joint_idx < 21; ml_joint_idx++) {
				float *data = wrap->wraps[1].data;
				data[(ml_joint_idx * 2) + 0] = bleh[ml_joint_idx].pos_2d.x;
				data[(ml_joint_idx * 2) + 1] = bleh[ml_joint_idx].pos_2d.y;
				// data[(ml_joint_idx * 2) + 2] = bleh[ml_joint_idx].depth_relative_to_midpxm;
			}


			wrap->wraps[2].data[0] = 1.0f;
		} else {
			wrap->wraps[2].data[0] = 0.0f;
			set_predicted_zero(wrap->wraps[1].data);
		}
	}

	stereographic_project_image(dist, instr, hgt->views[view_idx].run_model_on_this,
	                            &hgt->views[view_idx].debug_out_to_this, info.hand_idx ? RED : YELLOW,
	                            data_128x128_uint8);


	xrt::auxiliary::math::map_quat(this_output.look_dir) = instr.rot_quat;
	this_output.stereographic_radius = instr.stereographic_radius;

	bool is_hand = true;

	{
		XRT_TRACE_IDENT(convert_format);

		// here!
		cv::Mat data_128x128_float(cv::Size(128, 128), CV_32FC1, wrap->wraps[0].data, 128 * sizeof(float));

		is_hand = is_hand && normalizeGrayscaleImage(data_128x128_uint8, data_128x128_float);
	}


	// Ending here


	const OrtValue *inputs[] = {wrap->wraps[0].tensor, wrap->wraps[1].tensor, wrap->wraps[2].tensor};
	const char *input_names[] = {wrap->wraps[0].name, wrap->wraps[1].name, wrap->wraps[2].name};

	OrtValue *output_tensors[] = {nullptr, nullptr, nullptr, nullptr};
	const char *output_names[] = {"heatmap_xy", "heatmap_depth", "scalar_extras", "curls"};

	{
		XRT_TRACE_IDENT(model);
		assert(ARRAY_SIZE(input_names) == ARRAY_SIZE(inputs));
		assert(ARRAY_SIZE(output_names) == ARRAY_SIZE(output_tensors));
		ORT(Run(wrap->session, nullptr, input_names, inputs, ARRAY_SIZE(input_names), output_names,
		        ARRAY_SIZE(output_names), output_tensors));
	}

	// To here

	// Interpret model outputs!


	float *out_data = nullptr;


	ORT(GetTensorMutableData(output_tensors[0], (void **)&out_data));

	// I don't know why this was added
	// float *confidences = info.view->keypoint_outputs.views[hand_idx].confidences;

	// This was added for debug scribbling, and should be added back at some point.
	// xrt_vec2 *keypoints_global = px_coord.kps;

	size_t plane_size = 22 * 22;

	for (int i = 0; i < 21; i++) {
		float *data = &out_data[i * plane_size];


		// This will be optimized out if nan checking is disabled in hg_numerics_checker
		for (size_t x = 0; x < plane_size; x++) {
			CHECK_NOT_NAN(data[i]);
		}


		int out_idx = argmax(data, 22 * 22);
		int row = out_idx / 22;
		int col = out_idx % 22;

		xrt_vec2 loc = {};

		// This is a good start but rethink it. Maybe fail if less than 18/21 joints failed?
		// is_hand = is_hand &&
		refine_center_of_distribution(hgt, data, col, row, 22, 22, &loc.x, &loc.y);

		if (hand_idx == 0) {
			px_coord[i].pos_2d.x = math_map_ranges(loc.x, 0, 22, -1, 1);
		} else {
			px_coord[i].pos_2d.x = math_map_ranges(loc.x, 0, 22, 1, -1);
		}

		//!@todo when you change this to have +Z-forward
		// note note note the flip!!!
		px_coord[i].pos_2d.y = math_map_ranges(loc.y, 0, 22, 1, -1);

		px_coord[i].confidence_xy = data[out_idx];
	}


	float *out_data_depth = nullptr;
	ORT(GetTensorMutableData(output_tensors[1], (void **)&out_data_depth));

	for (int joint_idx = 0; joint_idx < 21; joint_idx++) {
		float *p_ptr = &out_data_depth[(joint_idx * 22)];


		float depth = 0;
		float confidence = 0;

		// This function can fail
		if (hand_depth_center_of_mass(hgt, p_ptr, &depth, &confidence)) {

			px_coord[joint_idx].depth_relative_to_midpxm = depth;
			px_coord[joint_idx].confidence_depth = confidence;
		} else {
			px_coord[joint_idx].depth_relative_to_midpxm = 0;
			px_coord[joint_idx].confidence_depth = 0;
		}
	}

	float *out_data_extras = nullptr;
	ORT(GetTensorMutableData(output_tensors[2], (void **)&out_data_extras));

	float is_hand_explicit = out_data_extras[0];

	is_hand_explicit = (1.0) / (1.0 + powf(2.71828182845904523536, -is_hand_explicit));

	// When the model is sure, it's _really_ sure.
	// Index was fine with 0.99.
	// North Star seemed to need 0.97.
	if (is_hand_explicit < 0.97) {
		U_LOG_D("Not hand! %f", is_hand_explicit);
		is_hand = false;
	}

	this_output.active = is_hand;


	float *out_data_curls = nullptr;
	ORT(GetTensorMutableData(output_tensors[3], (void **)&out_data_curls));

	for (int i = 0; i < 5; i++) {
		float curl = out_data_curls[i];
		float variance = out_data_curls[5 + i];

		// Next two lines directly correspond to py_training/settings.py
		// We don't want it to be negative
		variance = fabsf(variance);
		// We don't want it to be possible to be zero
		variance += 0.01;

		this_output.curls[i].value = curl;
		this_output.curls[i].variance = curl;
	}



	if (hgt->debug_scribble) {
		int data_acc_idx = 0;

		int root_x = 8 + ((2 * hand_idx) * (128 + 8));
		int root_y = 8 + (2 * info.view->view * (128 + 8));

		cv::Rect p = cv::Rect(root_x, root_y, 128, 128);

		data_128x128_uint8.copyTo(hgt->visualizers.mat(p));

		make_keypoint_heatmap_output(info.view->view, hand_idx, 0, 0, out_data + (data_acc_idx * plane_size),
		                             hgt->visualizers.mat);
		make_keypoint_depth_heatmap_output(info.view->view, hand_idx, 0, 0,
		                                   out_data_depth + (data_acc_idx * 22), hgt->visualizers.mat);

		data_acc_idx++;

		for (int finger = 0; finger < 5; finger++) {
			for (int joint = 0; joint < 4; joint++) {

				make_keypoint_heatmap_output(info.view->view, hand_idx, 1 + joint, finger,
				                             out_data + (data_acc_idx * plane_size),
				                             hgt->visualizers.mat);
				make_keypoint_depth_heatmap_output(info.view->view, hand_idx, 1 + joint, finger,
				                                   out_data_depth + (data_acc_idx * 22),
				                                   hgt->visualizers.mat);
				data_acc_idx++;
			}
		}

		// Hand existence
		char amt[5];

		snprintf(amt, ARRAY_SIZE(amt), "%.2f", is_hand_explicit);

		cv::Point2i text_origin;

		text_origin.x = root_x + 128 + 2;
		text_origin.y = root_y + 60;

		// Clear out what was there before
		cv::rectangle(hgt->visualizers.mat, cv::Rect(text_origin - cv::Point2i{0, 25}, cv::Size{30, 30}), {255},
		              cv::FILLED);


		cv::putText(hgt->visualizers.mat, amt, text_origin, cv::FONT_HERSHEY_SIMPLEX, 0.3, {0, 0, 0});

		// Curls
		cv::rectangle(hgt->visualizers.mat, cv::Rect(cv::Point2i{root_x, root_y + 128 + 22}, cv::Size{128, 60}),
		              {255}, cv::FILLED);
		for (int i = 0; i < 5; i++) {
			int r = 15;

			cv::Point2i center = {root_x + r + (20 * i), root_y + 128 + 60};

			cv::circle(hgt->visualizers.mat, center, 1, {0}, 1);

			float c = this_output.curls[i].value * 0.3;
			int x = cos(c) * r;
			// Remember, OpenCV has (0,0) at top left
			int y = -sin(c) * r;

			cv::Point2i pt2 = {x, y};
			pt2 += center;
			cv::circle(hgt->visualizers.mat, pt2, 1, {0}, 1);
			cv::line(hgt->visualizers.mat, center, pt2, {0}, 1);
		}
	}

	for (size_t i = 0; i < ARRAY_SIZE(output_tensors); i++) {
		wrap->api->ReleaseValue(output_tensors[i]);
	}
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