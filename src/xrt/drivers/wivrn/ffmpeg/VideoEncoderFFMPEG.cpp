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

#include "VideoEncoderFFMPEG.h"
#include <algorithm>
#include <array>
#include <stdexcept>

extern "C"
{
#include <libavcodec/avcodec.h>
}

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

namespace
{


bool should_keep_nal_h264(const uint8_t * header_start)
{
	uint8_t nal_type = (header_start[2] == 0 ? header_start[4] : header_start[3]) & 0x1F;
	switch (nal_type)
	{
	  case H264_NAL_SPS:
		case H264_NAL_PPS:
			return false;
		case 6: // supplemental enhancement information
		case 9: // access unit delimiter
			return false;
		default:
			return true;
	}
}

bool should_keep_nal_h265(const uint8_t * header_start)
{
	uint8_t nal_type = ((header_start[2] == 0 ? header_start[4] : header_start[3]) >> 1) & 0x3F;
	switch (nal_type)
	{
	  case HEVC_NAL_VPS:
		case HEVC_NAL_SPS:
		case HEVC_NAL_PPS:
			return false;
		case 35: // access unit delimiter
		case 39: // supplemental enhancement information
			return false;
		default:
			return true;
	}
}

bool should_keep_nal_h264_csd(const uint8_t * header_start)
{
	uint8_t nal_type = (header_start[2] == 0 ? header_start[4] : header_start[3]) & 0x1F;
	switch (nal_type)
	{
		case H264_NAL_SPS:
		case H264_NAL_PPS:
			return true;
		default:
			return false;
	}
}

bool should_keep_nal_h265_csd(const uint8_t * header_start)
{
	uint8_t nal_type = ((header_start[2] == 0 ? header_start[4] : header_start[3]) >> 1) & 0x3F;
	switch (nal_type)
	{
		case HEVC_NAL_VPS:
		case HEVC_NAL_SPS:
		case HEVC_NAL_PPS:
			return true;
		default:
			return false;
	}
}

void filter_NAL(const uint8_t * input, size_t input_size, std::vector<uint8_t> & out, VideoEncoderFFMPEG::Codec codec)
{
	if (input_size < 4)
		return;
	std::array<uint8_t, 3> header = {{0, 0, 1}};
	auto end = input + input_size;
	auto header_start = input;
	while (header_start != end)
	{
		auto next_header = std::search(header_start + 3, end, header.begin(), header.end());
		if (next_header != end and next_header[-1] == 0)
		{
			next_header--;
		}
		if (codec == VideoEncoderFFMPEG::Codec::h264 and (should_keep_nal_h264(header_start) || should_keep_nal_h264_csd(header_start)))
			out.insert(out.end(), header_start, next_header);
		if (codec == VideoEncoderFFMPEG::Codec::h265 and (should_keep_nal_h265(header_start) || should_keep_nal_h265_csd(header_start)))
			out.insert(out.end(), header_start, next_header);
		header_start = next_header;
	}
}

} // namespace

void VideoEncoderFFMPEG::filter_NAL_ql(const uint8_t * input, size_t input_size, int index)
{
	if (input_size < 4)
		return;
	std::array<uint8_t, 3> header = {{0, 0, 1}};
	auto end = input + input_size;
	auto header_start = input;
	while (header_start != end)
	{
		auto next_header = std::search(header_start + 3, end, header.begin(), header.end());
		if (next_header != end and next_header[-1] == 0)
		{
			next_header--;
		}
		
		std::vector<uint8_t> out;
		if (codec == VideoEncoderFFMPEG::Codec::h264 and should_keep_nal_h264_csd(header_start)) {
			out.insert(out.end(), header_start, next_header);
			SendCSD(std::move(out), index);
		}
		if (codec == VideoEncoderFFMPEG::Codec::h265 and should_keep_nal_h265_csd(header_start)) {
			out.insert(out.end(), header_start, next_header);
			SendCSD(std::move(out), index);
		}
		if (codec == VideoEncoderFFMPEG::Codec::h264 and should_keep_nal_h264(header_start)) {
			out.insert(out.end(), header_start, next_header);
			SendIDR(std::move(out), index);
		}
		if (codec == VideoEncoderFFMPEG::Codec::h265 and should_keep_nal_h265(header_start)) {
			out.insert(out.end(), header_start, next_header);
			SendIDR(std::move(out), index);
		}
		header_start = next_header;
	}
}

void VideoEncoderFFMPEG::Encode(int index, bool idr, std::chrono::steady_clock::time_point target_timestamp)
{
	PushFrame(index, idr, target_timestamp);
	AVPacket * enc_pkt = av_packet_alloc();
	
	printf("Start enc\n");
	int err = avcodec_receive_packet(encoder_ctx.get(), enc_pkt);
	if (err == 0)
	{
	  filter_NAL_ql(enc_pkt->data, enc_pkt->size, index);
	  
		//SendIDR({enc_pkt->data, enc_pkt->data + enc_pkt->size});
		av_packet_free(&enc_pkt);
	}
	
	int64_t ns_display = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::time_point_cast<std::chrono::nanoseconds>(target_timestamp).time_since_epoch()).count();
	FlushFrame(ns_display, index);
	printf("done!\n");
	
	if (err == AVERROR(EAGAIN))
	{
		return;
	}
	if (err)
	{
		throw std::runtime_error("frame encoding failed, code " + std::to_string(err));
	}
}
