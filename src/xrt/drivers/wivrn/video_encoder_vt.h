/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "video_encoder.h"
#include "yuv_converter.h"

#include <list>
#include <mutex>

static const char kAnnexBHeaderBytes[4] = {0, 0, 0, 1};
#define kCMFormatDescriptionBridgeError_InvalidParameter  (-12712)

typedef struct self_encode_params
{
    int frameW;
    int frameH;
} self_encode_params;

typedef struct EncodeContext
{
	void* ctx; // VideoEncoderVT
	int index;
	int64_t display_ns;
	int64_t start_encode_ns;
	struct os_mutex wait_mutex;
} EncodeContext;

typedef struct OpaqueVTCompressionSession* VTCompressionSessionRef;
typedef struct __CVBuffer* CVPixelBufferRef;
typedef int OSStatus;
typedef uint32_t VTEncodeInfoFlags;
typedef struct opaqueCMSampleBuffer* CMSampleBufferRef;

namespace xrt::drivers::wivrn
{

class VideoEncoderVT : public VideoEncoder
{
	struct pending_nal
	{
		int first_mb;
		int last_mb;
		std::vector<uint8_t> data;
	};

	vk_bundle * vk;
	//vt_picture_t pic_in;
	//vt_picture_t pic_out = {};
	std::unique_ptr<YuvConverter> converter;
	self_encode_params encode_params;
    float fps;
    uint32_t frame_idx;

    void* doIdrDict;
    void* doNoIdrDict;

    VTCompressionSessionRef compression_session;
    CVPixelBufferRef pixelBuffer;

	int next_mb;
	std::list<pending_nal> pending_nals;

	EncodeContext encode_contexts[3];

public:
	VideoEncoderVT(vk_bundle * vk, encoder_settings & settings, int input_width, int input_height, int slice_idx, int num_slices, float fps);

	void SetImages(int width,
	               int height,
	               VkFormat format,
	               int num_images,
	               VkImage * images,
	               VkImageView * views,
	               VkDeviceMemory * memory) override;

	void PresentImage(int index, VkCommandBuffer * out_buffer) override;

	void Encode(int index, bool idr, std::chrono::steady_clock::time_point pts) override;

	~VideoEncoderVT();

private:
	static void vtCallback(void *outputCallbackRefCon,
        void *sourceFrameRefCon,
        OSStatus status,
        VTEncodeInfoFlags infoFlags,
        CMSampleBufferRef sampleBuffer);

	static void CopyNals(VideoEncoderVT* ctx, 
					  char* avcc_buffer,
                      const size_t avcc_size,
                      size_t size_len,
                      int index);
};

} // namespace xrt::drivers::wivrn
