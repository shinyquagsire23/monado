// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGloves device implementation.
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup drv_opengloves
 */

#include <stdio.h>

#include "xrt/xrt_device.h"
#include "xrt/xrt_defines.h"

#include "math/m_space.h"

#include "util/u_device.h"
#include "util/u_debug.h"
#include "util/u_hand_tracking.h"
#include "util/u_logging.h"
#include "util/u_misc.h"
#include "util/u_var.h"

#include "opengloves_device.h"

#include "communication/opengloves_communication.h"
#include "encoding/alpha_encoding.h"

DEBUG_GET_ONCE_LOG_OPTION(opengloves_log, "OPENGLOVES_LOG", U_LOGGING_INFO)


#include "os/os_threading.h"
#include "util/u_hand_simulation.h"

#define OPENGLOVES_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->log_level, __VA_ARGS__)
#define OPENGLOVES_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->log_level, __VA_ARGS__)
#define OPENGLOVES_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->log_level, __VA_ARGS__)
#define OPENGLOVES_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->log_level, __VA_ARGS__)
#define OPENGLOVES_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->log_level, __VA_ARGS__)

enum opengloves_input_index
{
	OPENGLOVES_INPUT_INDEX_HAND_TRACKING,

	OPENGLOVES_INPUT_INDEX_TRIGGER_CLICK,
	OPENGLOVES_INPUT_INDEX_TRIGGER_VALUE,

	OPENGLOVES_INPUT_INDEX_A_CLICK,
	OPENGLOVES_INPUT_INDEX_B_CLICK,

	OPENGLOVES_INPUT_INDEX_JOYSTICK_MAIN,
	OPENGLOVES_INPUT_INDEX_JOYSTICK_MAIN_CLICK,

	OPENGLOVES_INPUT_INDEX_COUNT
};

enum opengloves_output_index
{
	OPENGLOVES_OUPUT_INDEX_FORCE_FEEDBACK,
	OPENGLOVES_OUTPUT_INDEX_COUNT
};

/*!
 * @implements xrt_device
 */
struct opengloves_device
{
	struct xrt_device base;
	struct opengloves_communication_device *ocd;

	struct os_thread_helper oth;
	struct os_mutex lock;

	struct opengloves_input *last_input;

	enum xrt_hand hand;

	struct u_hand_tracking hand_tracking;

	enum u_logging_level log_level;
};

static inline struct opengloves_device *
opengloves_device(struct xrt_device *xdev)
{
	return (struct opengloves_device *)xdev;
}

static void
opengloves_device_get_hand_tracking(struct xrt_device *xdev,
                                    enum xrt_input_name name,
                                    uint64_t requested_timestamp_ns,
                                    struct xrt_hand_joint_set *out_joint_set,
                                    uint64_t *out_timestamp_ns)
{
	struct opengloves_device *od = opengloves_device(xdev);

	enum xrt_hand hand = od->hand;

	struct u_hand_tracking_values values = {.little =
	                                            {
	                                                .splay = od->last_input->splay[4],
	                                                .joint_count = 5,
	                                            },
	                                        .ring =
	                                            {
	                                                .splay = od->last_input->splay[3],
	                                                .joint_count = 5,
	                                            },
	                                        .middle =
	                                            {
	                                                .splay = od->last_input->splay[2],
	                                                .joint_count = 5,
	                                            },
	                                        .index =
	                                            {
	                                                .splay = od->last_input->splay[1],
	                                                .joint_count = 5,
	                                            },
	                                        .thumb = {
	                                            .splay = od->last_input->splay[0],
	                                            .joint_count = 4,
	                                        }};
	// copy in the curls
	memcpy(values.little.joint_curls, od->last_input->flexion[4], sizeof(od->last_input->flexion[4]));
	memcpy(values.ring.joint_curls, od->last_input->flexion[3], sizeof(od->last_input->flexion[3]));
	memcpy(values.middle.joint_curls, od->last_input->flexion[2], sizeof(od->last_input->flexion[2]));
	memcpy(values.index.joint_curls, od->last_input->flexion[1], sizeof(od->last_input->flexion[1]));
	memcpy(values.thumb.joint_curls, od->last_input->flexion[0], sizeof(od->last_input->flexion[0]));

	struct xrt_space_relation ident;
	m_space_relation_ident(&ident);
	u_hand_sim_simulate_generic(&values, hand, &ident, out_joint_set);

	*out_timestamp_ns = requested_timestamp_ns;
	out_joint_set->is_active = true;
}

static void
opengloves_device_update_inputs(struct xrt_device *xdev)
{
	struct opengloves_device *od = opengloves_device(xdev);

	os_mutex_lock(&od->lock);

	od->base.inputs[OPENGLOVES_INPUT_INDEX_A_CLICK].value.boolean = od->last_input->buttons.A.pressed;
	od->base.inputs[OPENGLOVES_INPUT_INDEX_B_CLICK].value.boolean = od->last_input->buttons.B.pressed;

	od->base.inputs[OPENGLOVES_INPUT_INDEX_TRIGGER_CLICK].value.boolean = od->last_input->buttons.trigger.pressed;
	od->base.inputs[OPENGLOVES_INPUT_INDEX_TRIGGER_VALUE].value.vec1.x = od->last_input->buttons.trigger.value;

	od->base.inputs[OPENGLOVES_INPUT_INDEX_JOYSTICK_MAIN].value.vec2.x = od->last_input->joysticks.main.x;
	od->base.inputs[OPENGLOVES_INPUT_INDEX_JOYSTICK_MAIN].value.vec2.y = od->last_input->joysticks.main.y;
	od->base.inputs[OPENGLOVES_INPUT_INDEX_JOYSTICK_MAIN_CLICK].value.boolean =
	    od->last_input->joysticks.main.pressed;

	os_mutex_unlock(&od->lock);
}

static void
opengloves_ffb_location_convert(const struct xrt_output_force_feedback *xrt_ffb,
                                struct opengloves_output_force_feedback *out_ffb)
{
	switch (xrt_ffb->location) {
	case XRT_FORCE_FEEDBACK_LOCATION_LEFT_THUMB: out_ffb->thumb = xrt_ffb->value; break;
	case XRT_FORCE_FEEDBACK_LOCATION_LEFT_INDEX: out_ffb->index = xrt_ffb->value; break;
	case XRT_FORCE_FEEDBACK_LOCATION_LEFT_MIDDLE: out_ffb->middle = xrt_ffb->value; break;
	case XRT_FORCE_FEEDBACK_LOCATION_LEFT_RING: out_ffb->ring = xrt_ffb->value; break;
	case XRT_FORCE_FEEDBACK_LOCATION_LEFT_PINKY: out_ffb->little = xrt_ffb->value; break;
	}
}

static void
opengloves_device_set_output(struct xrt_device *xdev, enum xrt_output_name name, const union xrt_output_value *value)
{
	struct opengloves_device *od = opengloves_device(xdev);

	switch (name) {
	case XRT_OUTPUT_NAME_FORCE_FEEDBACK_LEFT:
	case XRT_OUTPUT_NAME_FORCE_FEEDBACK_RIGHT: {
		struct opengloves_output out;

		int location_count = value->force_feedback.force_feedback_location_count;
		const struct xrt_output_force_feedback *ffb = value->force_feedback.force_feedback;

		for (int i = 0; i < location_count; i++) {
			opengloves_ffb_location_convert(ffb + i, &out.force_feedback);
		}

		char buff[64];
		opengloves_alpha_encoding_encode(&out, buff);

		opengloves_communication_device_write(od->ocd, buff, strlen(buff));
	}
	default: break;
	}
}

static void
opengloves_device_destroy(struct xrt_device *xdev)
{
	struct opengloves_device *od = opengloves_device(xdev);

	os_thread_helper_destroy(&od->oth);

	os_mutex_destroy(&od->lock);

	opengloves_communication_device_destory(od->ocd);

	free(od->last_input);
	free(od);
}


/*!
 * Reads the next packet from the device, finishing successfully when reaching a newline
 * Returns true if finished at a newline, or false if there was an error
 */
static bool
opengloves_read_next_packet(struct opengloves_device *od, char *buffer, int buffer_len)
{
	os_thread_helper_lock(&od->oth);

	char next_char = 0;
	int i = 0;
	do {
		// try read one byte
		int ret = opengloves_communication_device_read(od->ocd, &next_char, 1);
		if (ret < 0) {
			OPENGLOVES_ERROR(od, "Failed to read from device! %s", strerror(ret));
			os_thread_helper_unlock(&od->oth);
			return false;
		}

		if (next_char == 0 || next_char == '\n')
			continue;

		buffer[i++] = next_char;
	} while (next_char != '\n' && i < buffer_len);

	// null terminate
	buffer[i] = '\0';

	OPENGLOVES_DEBUG(od, "%s -> len %i", buffer, i);

	os_thread_helper_unlock(&od->oth);

	return true;
}

/*!
 * Main thread for reading data from the device
 */
static void *
opengloves_run_thread(void *ptr)
{
	struct opengloves_device *od = (struct opengloves_device *)ptr;

	char buffer[OPENGLOVES_ENCODING_MAX_PACKET_SIZE];

	while (opengloves_read_next_packet(od, buffer, OPENGLOVES_ENCODING_MAX_PACKET_SIZE) &&
	       os_thread_helper_is_running(&od->oth)) {
		os_mutex_lock(&od->lock);
		opengloves_alpha_encoding_decode(buffer, od->last_input);
		os_mutex_unlock(&od->lock);
	}

	return 0;
}

struct xrt_device *
opengloves_device_create(struct opengloves_communication_device *ocd, enum xrt_hand hand)
{
	enum u_device_alloc_flags flags = (enum u_device_alloc_flags)(U_DEVICE_ALLOC_TRACKING_NONE);

	struct opengloves_device *od = U_DEVICE_ALLOCATE(struct opengloves_device, flags, 8, 1);

	od->base.name = XRT_DEVICE_HAND_TRACKER;
	od->base.device_type = XRT_DEVICE_TYPE_HAND_TRACKER;
	od->hand = hand;

	od->ocd = ocd;
	od->base.destroy = opengloves_device_destroy;
	os_mutex_init(&od->lock);

	// hand tracking
	od->base.get_hand_tracking = opengloves_device_get_hand_tracking;
	od->base.inputs[OPENGLOVES_INPUT_INDEX_HAND_TRACKING].name =
	    od->hand == XRT_HAND_LEFT ? XRT_INPUT_GENERIC_HAND_TRACKING_LEFT : XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT;

	od->base.hand_tracking_supported = true;
	od->base.force_feedback_supported = true;

	// inputs
	od->base.update_inputs = opengloves_device_update_inputs;
	od->last_input = U_TYPED_CALLOC(struct opengloves_input);

	od->base.inputs[OPENGLOVES_INPUT_INDEX_A_CLICK].name = XRT_INPUT_INDEX_A_CLICK;
	od->base.inputs[OPENGLOVES_INPUT_INDEX_B_CLICK].name = XRT_INPUT_INDEX_B_CLICK;

	od->base.inputs[OPENGLOVES_INPUT_INDEX_TRIGGER_VALUE].name = XRT_INPUT_INDEX_TRIGGER_VALUE;
	od->base.inputs[OPENGLOVES_INPUT_INDEX_TRIGGER_CLICK].name = XRT_INPUT_INDEX_TRIGGER_CLICK;

	od->base.inputs[OPENGLOVES_INPUT_INDEX_JOYSTICK_MAIN].name = XRT_INPUT_INDEX_THUMBSTICK;
	od->base.inputs[OPENGLOVES_INPUT_INDEX_JOYSTICK_MAIN_CLICK].name = XRT_INPUT_INDEX_THUMBSTICK_CLICK;

	// outputs
	od->base.outputs[0].name =
	    od->hand == XRT_HAND_LEFT ? XRT_OUTPUT_NAME_FORCE_FEEDBACK_LEFT : XRT_OUTPUT_NAME_FORCE_FEEDBACK_RIGHT;
	od->base.set_output = opengloves_device_set_output;

	// startup thread
	int ret = os_thread_helper_init(&od->oth);
	if (ret != 0) {
		OPENGLOVES_ERROR(od, "Failed to initialise threading!");
		opengloves_device_destroy(&od->base);
		return NULL;
	}

	ret = os_thread_helper_start(&od->oth, opengloves_run_thread, od);
	if (ret != 0) {
		OPENGLOVES_ERROR(od, "Failed to start thread!");
		opengloves_device_destroy(&od->base);

		return 0;
	}

	u_var_add_root(od, "OpenGloves VR glove device", true);
	snprintf(od->base.serial, XRT_DEVICE_NAME_LEN, "OpenGloves %s", hand == XRT_HAND_LEFT ? "Left" : "Right");

	od->log_level = debug_get_log_option_opengloves_log();


	return &od->base;
}
