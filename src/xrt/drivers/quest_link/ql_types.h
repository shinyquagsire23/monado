// Copyright 2022, Collabora, Ltd.
// Copyright 2022 Max Thomas
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to quest_link XRSP protocol.
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "os/os_threading.h"
#include "util/u_logging.h"
#include "util/u_distortion_mesh.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_frameserver.h"
#include "xrt/xrt_prober.h"
#include "xrt/xrt_tracking.h"

typedef struct libusb_context libusb_context;
typedef struct libusb_device_handle libusb_device_handle;
struct ql_hmd;
struct ql_controller;
struct ql_camera;

typedef struct ql_xrsp_echo_payload
{
    int64_t org;
    int64_t recv;
    int64_t xmt;
    int64_t offset;
} ql_xrsp_echo_payload;

typedef struct ql_xrsp_hostinfo_pkt
{
    uint8_t* payload;
    uint32_t payload_size;

    uint8_t message_type;
    uint16_t result;
    uint32_t stream_size;

    uint32_t unk_4;
    uint32_t unk_8;
    uint32_t len_u64s;

    int64_t recv_ns;
} ql_xrsp_hostinfo_pkt;

typedef struct ql_xrsp_topic_pkt
{
    bool has_alignment_padding;
    bool packet_version_is_internal;
    uint8_t packet_version_number;
    uint8_t topic;

    uint16_t num_words;
    uint16_t sequence_num;

    uint8_t* payload;
    uint32_t payload_size;
    uint32_t payload_valid;
    uint32_t remainder_offs;
    int32_t missing_bytes;

    int64_t recv_ns;
} ql_xrsp_topic_pkt;

typedef struct ql_xrsp_host ql_xrsp_host;

typedef struct ql_xrsp_host
{
    struct ql_system* sys;

    /* Packet processing threads */
    struct os_thread_helper read_thread;
    struct os_thread_helper write_thread;

    //struct ql_system* sys;

    libusb_context *ctx;
    libusb_device_handle *dev;

    int if_num;
    uint8_t ep_out;
    uint8_t ep_in;

    // Parsing state
    bool have_working_pkt;
    ql_xrsp_topic_pkt working_pkt;

    uint16_t increment;
    int pairing_state;
    int64_t start_ns;

    // Echo state
    int echo_idx;
    int64_t ns_offset;

    int64_t echo_req_sent_ns;
    int64_t echo_req_recv_ns;
    int64_t echo_resp_sent_ns;
    int64_t echo_resp_recv_ns;
    int64_t last_xmt;

    int num_slices;
    int64_t frame_sent_ns;
    int64_t paired_ns;

    struct os_mutex stream_mutex;
    struct os_mutex usb_mutex;
    bool ready_to_send_frames;
    bool needs_flush;
    int frame_idx;

    //std::vector<uint8_t> csd_stream;
    //std::vector<uint8_t> idr_stream;

    uint8_t* csd_stream;
    uint8_t* idr_stream;

    size_t csd_stream_len;
    size_t idr_stream_len;

    void (*send_csd)(struct ql_xrsp_host* host, const uint8_t* data, size_t len);
    void (*send_idr)(struct ql_xrsp_host* host, const uint8_t* data, size_t len);
    void (*flush_stream)(struct ql_xrsp_host* host);
} ql_xrsp_host;


#define MAX_TRACKED_DEVICES 2

#define HMD_HID 0
#define STATUS_HID 1
#define CONTROLLER_HID 2

/* All HMD Configuration / calibration info */
struct ql_hmd_config
{
    int proximity_threshold;
};

/* Structure to track online devices and type */
struct ql_tracked_device
{
    uint64_t device_id;
    //ql_device_type device_type;
};

struct ql_controller
{
    struct xrt_device base;

    struct xrt_pose pose;
    struct xrt_vec3 center;

    double created_ns;

    struct ql_system *sys;
};

struct ql_hmd
{
    struct xrt_device base;

    struct xrt_pose pose;
    struct xrt_vec3 center;

    double created_ns;

    struct ql_system *sys;
    /* HMD config info (belongs to the system, which we have a ref to */
    struct ql_hmd_config *config;

    /* Pose tracker provided by the system */
    struct ql_tracker *tracker;

    /* Tracking to extend 32-bit HMD time to 64-bit nanoseconds */
    uint32_t last_imu_timestamp32; /* 32-bit µS device timestamp */
    timepoint_ns last_imu_timestamp_ns;


    int32_t encode_width;
    int32_t encode_height;

    /* Temporary distortion values for mesh calc */
    struct u_panotools_values distortion_vals[2];
};

typedef struct ql_system
{
    struct xrt_tracking_origin base;
    struct xrt_reference ref;

    ql_xrsp_host xrsp_host;

    /* Packet processing thread */
    struct os_thread_helper oth;
    struct os_hid_device *handles[3];
    uint64_t last_keep_alive;

    /* state tracking for tracked devices on our radio link */
    int num_active_tracked_devices;
    struct ql_tracked_device tracked_device[MAX_TRACKED_DEVICES];

    /* Device lock protects device access */
    struct os_mutex dev_mutex;

    /* All configuration data for the HMD, stored
     * here for sharing to child objects */
    struct ql_hmd_config hmd_config;

    /* HMD device */
    struct ql_hmd *hmd;

    /* Controller devices */
    struct ql_controller *controllers[MAX_TRACKED_DEVICES];

    /* Video feed handling */
    struct xrt_frame_context xfctx;
    struct ql_camera *cam;
} ql_system;

void ql_xrsp_host_init();

#ifdef __cplusplus
}
#endif
