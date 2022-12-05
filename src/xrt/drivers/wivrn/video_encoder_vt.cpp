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

#include <CoreVideo/CoreVideo.h>
#include <CoreMedia/CMSampleBuffer.h>
#include <CoreMedia/CMFormatDescription.h>
#include <CoreMedia/CMTime.h>
#include <VideoToolbox/VideoToolbox.h>
#include <VideoToolbox/VTSession.h>
#include <VideoToolbox/VTCompressionProperties.h>
#include <VideoToolbox/VTCompressionSession.h>
#include <VideoToolbox/VTDecompressionSession.h>
#include <VideoToolbox/VTErrors.h>

#include "h264bitstream/h264_stream.h"

typedef void* RROutput;
#define _RENDER_H_ // X11 HACK
#define _XRENDER_H_
#define _XRANDR_H_
#include "video_encoder_vt.h"

#include <stdexcept>

#define H264_NAL_UNSPECIFIED      (0)
#define H264_NAL_CODED_NON_IDR    (1)
#define H264_NAL_CODED_PART_A     (2)
#define H264_NAL_CODED_PART_B     (3)
#define H264_NAL_CODED_PART_C     (4)
#define H264_NAL_IDR              (5)
#define H264_NAL_SEI              (6)
#define H264_NAL_SPS              (7)
#define H264_NAL_PPS              (8)
#define H264_NAL_AUX              (9)
#define H264_NAL_END_SEQ          (10)
#define H264_NAL_END_STREAM       (11)
#define H264_NAL_FILLER           (12)
#define H264_NAL_SPS_EXT          (13)
#define H264_NAL_PREFIX           (14)
#define H264_NAL_SUBSET_SPS       (15)
#define H264_NAL_DEPTH            (16)
#define H264_NAL_CODED_AUX_NOPART (19)
#define H264_NAL_CODED_SLICE      (20)
#define H264_NAL_CODED_DEPTH      (21)

#define HEVC_NAL_TRAIL_N        (0)
#define HEVC_NAL_TRAIL_R        (1)
#define HEVC_NAL_TSA_N          (2)
#define HEVC_NAL_TSA_R          (3)
#define HEVC_NAL_STSA_N         (4)
#define HEVC_NAL_STSA_R         (5)
#define HEVC_NAL_RADL_N         (6)
#define HEVC_NAL_RADL_R         (7)
#define HEVC_NAL_RASL_N         (8)
#define HEVC_NAL_RASL_R         (9)
#define HEVC_NAL_BLA_W_LP       (16)
#define HEVC_NAL_BLA_W_RADL     (17)
#define HEVC_NAL_BLA_N_LP       (18)
#define HEVC_NAL_IDR_W_RADL     (19)
#define HEVC_NAL_IDR_N_LP       (20)
#define HEVC_NAL_CRA_NUT        (21)
#define HEVC_NAL_VPS            (32)
#define HEVC_NAL_SPS            (33)
#define HEVC_NAL_PPS            (34)
#define HEVC_NAL_AUD            (35)
#define HEVC_NAL_EOS_NUT        (36)
#define HEVC_NAL_EOB_NUT        (37)
#define HEVC_NAL_FD_NUT         (38)
#define HEVC_NAL_SEI_PREFIX     (39)
#define HEVC_NAL_SEI_SUFFIX     (40)

static void hex_dump(const uint8_t* b, size_t amt)
{
    for (size_t i = 0; i < amt; i++)
    {
        if (i && i % 16 == 0) {
            printf("\n");
        }
        printf("%02x ", b[i]);
    }
    printf("\n");
}

// Convenience function for creating a dictionary.
static CFDictionaryRef CreateCFTypeDictionary(CFTypeRef* keys,
                                              CFTypeRef* values,
                                              size_t size) {
  return CFDictionaryCreate(kCFAllocatorDefault, keys, values, size,
                            &kCFTypeDictionaryKeyCallBacks,
                            &kCFTypeDictionaryValueCallBacks);
}

void VideoEncoderVT::CopyNals(VideoEncoderVT* ctx,
				      char* avcc_buffer,
                      const size_t avcc_size,
                      size_t size_len) {
    size_t nal_size;

    size_t bytes_left = avcc_size;
    while (bytes_left > 0) {
        nal_size = 0;
        for (size_t i = 0; i < size_len; i++)
        {
            nal_size |= *(uint8_t*)avcc_buffer;

            bytes_left -= 1;
            avcc_buffer += 1;

            if (i != size_len-1) {
                nal_size <<= 8;
            }
        }

        int type = (avcc_buffer[0] & 0x7E) >> 1;
        //printf("Type: %u\n", type);
        if (type == HEVC_NAL_VPS || type == HEVC_NAL_SPS || type == HEVC_NAL_PPS) {
            bytes_left -= nal_size;
            avcc_buffer += nal_size;
            continue;
        }


        //hex_dump((uint8_t*)avcc_buffer, nal_size);
        
        std::vector<uint8_t> data(sizeof(kAnnexBHeaderBytes)+nal_size);

        memcpy(reinterpret_cast<char*>(data.data()), (void*)kAnnexBHeaderBytes, sizeof(kAnnexBHeaderBytes));
        memcpy(reinterpret_cast<char*>(data.data())+sizeof(kAnnexBHeaderBytes), (char*)avcc_buffer, nal_size);
        ctx->SendIDR(std::move(data));

        bytes_left -= nal_size;
        avcc_buffer += nal_size;
    }
}

void VideoEncoderVT::vtCallback(void *outputCallbackRefCon,
        void *sourceFrameRefCon,
        OSStatus status,
        VTEncodeInfoFlags infoFlags,
        CMSampleBufferRef sampleBuffer ) 
{
    VideoEncoderVT* ctx = (VideoEncoderVT*)outputCallbackRefCon;
    self_encode_params* encode_params = (self_encode_params*)sourceFrameRefCon;

    // Frame skipped
    if (!sampleBuffer) return;


    CFDictionaryRef sample_attachments = (CFDictionaryRef)CFArrayGetValueAtIndex(CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, true), 0);
    bool keyframe = !CFDictionaryContainsKey(sample_attachments, kCMSampleAttachmentKey_NotSync);

    // Get the sample buffer's block buffer and format description.
    CMBlockBufferRef bb = CMSampleBufferGetDataBuffer(sampleBuffer);
    CMFormatDescriptionRef fdesc = CMSampleBufferGetFormatDescription(sampleBuffer);
    size_t bb_size = CMBlockBufferGetDataLength(bb);
    size_t total_bytes = bb_size;
    size_t pset_count;
    int nal_size_field_bytes;
    status = CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fdesc, 0, NULL, NULL, &pset_count, &nal_size_field_bytes);
    if (status == kCMFormatDescriptionBridgeError_InvalidParameter) 
    {
        pset_count = 2;
        nal_size_field_bytes = 4;
    } 
    else if (status != noErr) 
    {
        printf("CMVideoFormatDescriptionGetHEVCParameterSetAtIndex failed\n");
        return;
    }
    
    // Get the total size of the parameter sets
    if (keyframe) {
        const uint8_t* pset;
        size_t pset_size;
        for (size_t pset_i = 0; pset_i < pset_count; ++pset_i) {
            status = CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fdesc, pset_i, &pset, &pset_size, NULL, NULL);
            if (status != noErr) {
              printf("CMVideoFormatDescriptionGetHEVCParameterSetAtIndex failed\n");
              return;
            }
            total_bytes += pset_size + nal_size_field_bytes;
        }
    }

    // Copy all parameter sets separately
    if (keyframe) {
        const uint8_t* pset;
        size_t pset_size;

        for (size_t pset_i = 0; pset_i < pset_count; ++pset_i) {
            status = CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fdesc, pset_i, &pset, &pset_size, NULL, NULL);
            if (status != noErr) {
                printf("CMVideoFormatDescriptionGetHEVCParameterSetAtIndex failed\n");
                return;
            }

            int type = (pset[0] & 0x7E) >> 1;
            if (type != HEVC_NAL_VPS && type != HEVC_NAL_SPS && type != HEVC_NAL_PPS) continue;
            //printf("Type: %u\n", type);

            static const char startcode_4[4] = {0, 0, 0, 1};

            std::vector<uint8_t> data(sizeof(startcode_4)+pset_size);
            memcpy(reinterpret_cast<char*>(data.data()), startcode_4, sizeof(startcode_4));
            memcpy(reinterpret_cast<char*>(data.data())+sizeof(startcode_4), (char*)pset, pset_size);
            //hex_dump(data.data(), sizeof(startcode_4)+pset_size);
            ctx->SendCSD(std::move(data), false);
        }
    }

    // Block buffers can be composed of non-contiguous chunks. For the sake of
    // keeping this code simple, flatten non-contiguous block buffers.
    CMBlockBufferRef contiguous_bb;
    if (!CMBlockBufferIsRangeContiguous(bb, 0, 0)) {
        //contiguous_bb.reset();
        status = CMBlockBufferCreateContiguous(
            kCFAllocatorDefault, (OpaqueCMBlockBuffer*)bb, kCFAllocatorDefault, NULL, 0, 0, 0,
            (OpaqueCMBlockBuffer**)&contiguous_bb);
        if (status != noErr) {
            printf("CMBlockBufferCreateContiguous failed\n");
            //DLOG(ERROR) << " CMBlockBufferCreateContiguous failed: " << status;
            return;
        }
    }
    else {
        contiguous_bb = bb;
    }

    // Copy all the NAL units. In the process convert them from AVCC format
    // (length header) to AnnexB format (start code).
    int lengthAtOffset, totalLengthOut;
    char* bb_data;
    status = CMBlockBufferGetDataPointer(contiguous_bb, 0, NULL, NULL, &bb_data);
    if (status != noErr) {
        printf("CMBlockBufferGetDataPointer failed\n");
        return;
    }

    CopyNals(ctx, bb_data, bb_size, nal_size_field_bytes);

    ctx->FlushFrame(0);
}


namespace xrt::drivers::wivrn
{


VideoEncoderVT::VideoEncoderVT(
        vk_bundle * vk,
        encoder_settings & settings,
        int input_width,
        int input_height,
        float fps) :
        vk(vk),
        fps(fps)
{
	if (settings.codec != h264)
	{
		U_LOG_W("requested vt encoder with codec != h264");
		settings.codec = h264;
	}

	//FILE* f = fopen("apple_h264.265", "wb");
    //fclose(f);

    settings.width += settings.width % 2;
	settings.height += settings.height % 2;

    encode_params.frameW = settings.width;
    encode_params.frameH = settings.height;

    converter =
	        std::make_unique<YuvConverter>(vk, VkExtent3D{uint32_t(settings.width), uint32_t(settings.height), 1}, settings.offset_x, settings.offset_y, input_width, input_height);

    settings.range = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
	settings.color_model = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;

    const size_t attributesSize = 1;
    CFTypeRef keys[attributesSize] = {
        kCVPixelBufferPixelFormatTypeKey
    };
    CFDictionaryRef ioSurfaceValue = CreateCFTypeDictionary(NULL, NULL, 0);
    int64_t nv12type = kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
    CFNumberRef pixelFormat = CFNumberCreate(NULL, kCFNumberLongType, &nv12type);
    CFTypeRef values[attributesSize] = {pixelFormat};

    CFDictionaryRef sourceAttributes = CreateCFTypeDictionary(keys, values, attributesSize);

    CFMutableDictionaryRef encoder_specs = CFDictionaryCreateMutable(NULL, 6, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    
    // we need the encoder to pick up the pace, so we lie and say we're at 4x framerate (but only ever feed the real fps)
    int32_t framerate = (int)fps * 4;
    int32_t maxkeyframe = (int)fps * 5;
    int32_t bitrate = (int32_t)settings.bitrate;
    int32_t frames = 1;
    CFNumberRef cfFPS = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &framerate);
    CFNumberRef cfMaxKeyframe = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &maxkeyframe);
    CFNumberRef cfBitrate = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &bitrate);
    CFNumberRef cfFrames = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &frames);

    //CFNumberRef cfBaseFps = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &v);

    CFDictionarySetValue(encoder_specs, kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder, kCFBooleanTrue);


    // Create compression session
    int err = VTCompressionSessionCreate(kCFAllocatorDefault,
                                 encode_params.frameW,
                                 encode_params.frameH,
                                 kCMVideoCodecType_HEVC,
                                 encoder_specs, // use default encoder
                                 sourceAttributes,
                                 NULL, // use default compressed data allocator
                                 (VTCompressionOutputCallback)vtCallback,
                                 this,
                                 &compression_session);


    if(err == noErr) {
        //comp_session = session;
        //printf("Made session!\n");
    }

    err = VTSessionSetProperty(compression_session, kVTCompressionPropertyKey_ExpectedFrameRate, cfFPS);
    if(err != noErr) {
        printf("ExpectedFrameRate fail?\n");
    }
    //VTSessionSetProperty(compression_session, kVTCompressionPropertyKey_Quality, cfQuality);
    //VTSessionSetProperty(compression_session, kVTCompressionPropertyKey_SourceFrameCount, cfFrames);
    VTSessionSetProperty(compression_session, kVTCompressionPropertyKey_AverageBitRate, cfBitrate);
    VTSessionSetProperty(compression_session, kVTCompressionPropertyKey_MaxFrameDelayCount, cfFrames);

    VTSessionSetProperty(compression_session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
    VTSessionSetProperty(compression_session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
    VTSessionSetProperty(compression_session, kVTCompressionPropertyKey_AllowTemporalCompression, kCFBooleanTrue);
    VTSessionSetProperty(compression_session, kVTCompressionPropertyKey_AllowOpenGOP, kCFBooleanFalse);
    VTSessionSetProperty(compression_session, kVTCompressionPropertyKey_MaxFrameDelayCount, cfMaxKeyframe);
	VTSessionSetProperty(compression_session, kVTCompressionPropertyKey_PrioritizeEncodingSpeedOverQuality, kCFBooleanTrue);

    VTSessionSetProperty(compression_session, kVTCompressionPropertyKey_ColorPrimaries, kCVImageBufferColorPrimaries_ITU_R_709_2);
    VTSessionSetProperty(compression_session, kVTCompressionPropertyKey_TransferFunction, kCVImageBufferTransferFunction_ITU_R_709_2);
    VTSessionSetProperty(compression_session, kVTCompressionPropertyKey_YCbCrMatrix, kCVImageBufferYCbCrMatrix_ITU_R_709_2);

    VTSessionSetProperty(compression_session, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_HEVC_Main_AutoLevel);

    VTCompressionSessionPrepareToEncodeFrames(compression_session);

    //VTSessionSetProperty(compression_session, kVTCompressionPropertyKey_YCbCrMatrix, kCVImageBufferYCbCrMatrix_ITU_R_601_4);

    if(err == noErr) {
        //comp_session = session;
        printf("Made session!\n");
    }

    void* planes[2] = {(void *)converter->y.mapped_memory, (void *)converter->uv.mapped_memory};
    size_t planes_w[2] = {size_t(settings.width), size_t(settings.width)};
    size_t planes_h[2] = {size_t(settings.height), size_t(settings.height)};
    size_t planes_stride[2] = {size_t(converter->y.stride), size_t(converter->uv.stride)};

    //CVPixelBufferCreate(kCFAllocatorDefault, encode_params.frameW, encode_params.frameH, kCVPixelFormatType_420YpCbCr8BiPlanarFullRange, NULL, &pixelBuffer);
    CVPixelBufferCreateWithPlanarBytes(kCFAllocatorDefault, encode_params.frameW, encode_params.frameH, kCVPixelFormatType_420YpCbCr8BiPlanarFullRange,
    								   NULL, NULL, 2, planes, planes_w, planes_h, planes_stride, NULL, NULL, NULL, &pixelBuffer);
    

    {
    	CFTypeRef frameProperties_keys[1] = {
	        kVTEncodeFrameOptionKey_ForceKeyFrame
	    };
	    CFTypeRef frameProperties_values[1] = {kCFBooleanTrue};
	    CFDictionaryRef frameProperties = CreateCFTypeDictionary(frameProperties_keys, frameProperties_values, 1);

    	doIdrDict = (void*)frameProperties;
    }

    {
    	CFTypeRef frameProperties_keys[1] = {
	        kVTEncodeFrameOptionKey_ForceKeyFrame
	    };
	    CFTypeRef frameProperties_values[1] = {kCFBooleanFalse};
	    CFDictionaryRef frameProperties = CreateCFTypeDictionary(frameProperties_keys, frameProperties_values, 1);

    	doNoIdrDict = (void*)frameProperties;
    }

    CFRelease(cfFPS);
    CFRelease(cfMaxKeyframe);
    CFRelease(cfBitrate);
    CFRelease(cfFrames);
}

void VideoEncoderVT::SetImages(int width, int height, VkFormat format, int num_images, VkImage * images, VkImageView * views, VkDeviceMemory * memory)
{
	converter->SetImages(num_images, images, views);
}

void VideoEncoderVT::PresentImage(int index, VkCommandBuffer * out_buffer)
{
	*out_buffer = converter->command_buffers[index];
}

void VideoEncoderVT::Encode(int index, bool idr, std::chrono::steady_clock::time_point pts)
{
	CMTime pts_ = CMTimeMake(1000*(index*4), (int)(fps * 4 * 1000));//CMTimeMakeWithSeconds(std::chrono::duration<double>(pts.time_since_epoch()).count(), 1); // (1.0/fps) * index
    CMTime duration = CMTimeMake(1000, (int)(fps* 4 * 1000));//CMTimeMakeWithSeconds(1.0/fps, 1);

    VTCompressionSessionEncodeFrame(compression_session, pixelBuffer, pts_, duration, (CFDictionaryRef)(idr ? doIdrDict : doNoIdrDict), &encode_params, NULL);

    
    // This causes stuttering
    //VTCompressionSessionCompleteFrames(compression_session, kCMTimeInvalid);
}

VideoEncoderVT::~VideoEncoderVT()
{
	VTCompressionSessionInvalidate(compression_session);
	CFRelease(doIdrDict);
	CFRelease(doNoIdrDict);
}

} // namespace xrt::drivers::wivrn
