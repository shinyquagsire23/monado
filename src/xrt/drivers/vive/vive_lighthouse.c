// Copyright 2016-2019, Philipp Zabel
// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vive Lighthouse Watchman implementation
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_vive
 */

#include <asm/byteorder.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>
#include <stdio.h>

#include "math/m_api.h"
#include "util/u_debug.h"
#include "util/u_logging.h"

#include "vive_lighthouse.h"

static enum u_logging_level ll;

#define LH_TRACE(...) U_LOG_IFL_T(ll, __VA_ARGS__)
#define LH_DEBUG(...) U_LOG_IFL_D(ll, __VA_ARGS__)
#define LH_INFO(...) U_LOG_IFL_I(ll, __VA_ARGS__)
#define LH_WARN(...) U_LOG_IFL_W(ll, __VA_ARGS__)
#define LH_ERROR(...) U_LOG_IFL_E(ll, __VA_ARGS__)

DEBUG_GET_ONCE_LOG_OPTION(vive_log, "VIVE_LOG", U_LOGGING_WARN)

struct lighthouse_ootx_report
{
	__le16 version;
	__le32 serial;
	__le16 phase[2];
	__le16 tilt[2];
	__u8 reset_count;
	__u8 model_id;
	__le16 curve[2];
	__s8 gravity[3];
	__le16 gibphase[2];
	__le16 gibmag[2];
} __attribute__((packed));

static unsigned int watchman_id;

float
_f16_to_float(uint16_t f16)
{
	unsigned int sign = f16 >> 15;
	unsigned int exponent = (f16 >> 10) & 0x1f;
	unsigned int mantissa = f16 & 0x3ff;
	union {
		float f32;
		uint32_t u32;
	} u;

	if (exponent == 0) {
		if (!mantissa) {
			/* zero */
			u.u32 = sign << 31;
		} else {
			/* subnormal */
			exponent = 127 - 14;
			mantissa <<= 23 - 10;
			/*
			 * convert to normal representation:
			 * shift up mantissa and drop MSB
			 */
			while (!(mantissa & (1 << 23))) {
				mantissa <<= 1;
				exponent--;
			}
			mantissa &= 0x7fffffu;
			u.u32 = (sign << 31) | (exponent << 23) | mantissa;
		}
	} else if (exponent < 31) {
		/* normal */
		exponent += 127 - 15;
		mantissa <<= 23 - 10;
		u.u32 = (sign << 31) | (exponent << 23) | mantissa;
	} else if (mantissa == 0) {
		/* infinite */
		u.u32 = (sign << 31) | (255 << 23);
	} else {
		/* NaN */
		u.u32 = 0x7fffffffu;
	}
	return u.f32;
}

static inline float
__le16_to_float(__le16 le16)
{
	return _f16_to_float(__le16_to_cpu(le16));
}

static inline bool
pulse_in_this_sync_window(int32_t dt, uint16_t duration)
{
	return dt > -duration && (dt + duration) < (6500 + 250);
}

static inline bool
pulse_in_next_sync_window(int32_t dt, uint16_t duration)
{
	int32_t dt_end = dt + duration;

	/*
	 * Allow 2000 pulses (40 µs) deviation from the expected interval
	 * between bases, and 1000 pulses (20 µs) for a single base.
	 */
	return (dt > (20000 - 2000) && (dt_end) < (20000 + 6500 + 2000)) ||
	       (dt > (380000 - 2000) && (dt_end) < (380000 + 6500 + 2000)) ||
	       (dt > (400000 - 1000) && (dt_end) < (400000 + 6500 + 1000));
}

static inline bool
pulse_in_sweep_window(int32_t dt, uint16_t duration)
{
	/*
	 * The J axis (horizontal) sweep starts 71111 ticks after the sync
	 * pulse start (32°) and ends at 346667 ticks (156°).
	 * The K axis (vertical) sweep starts at 55555 ticks (23°) and ends
	 * at 331111 ticks (149°).
	 */
	return dt > (55555 - 1000) && (dt + duration) < (346667 + 1000);
}

static void
_handle_ootx_frame(struct lighthouse_base *base)
{
	struct lighthouse_ootx_report *report = (void *)(base->ootx + 2);
	uint16_t len = (__le16)*base->ootx;
	uint32_t crc = crc32(0L, Z_NULL, 0);
	bool serial_changed = false;
	uint32_t ootx_crc;
	uint16_t version;
	int ootx_version;
	struct xrt_vec3 gravity;
	int i;

	if (len != 33) {
		LH_WARN("Lighthouse Base %X: unexpected OOTX payload length: %d", base->serial, len);
		return;
	}

	ootx_crc = (__le32) * ((__le32 *)(base->ootx + 36)); /* (len+3)/4*4 */

	crc = crc32(crc, base->ootx + 2, 33);
	if (ootx_crc != crc) {
		LH_ERROR("Lighthouse Base %X: CRC error: %08x != %08x", base->serial, crc, ootx_crc);
		return;
	}

	version = __le16_to_cpu(report->version);
	ootx_version = version & 0x3f;
	if (ootx_version != 6) {
		LH_ERROR("Lighthouse Base %X: unexpected OOTX frame version: %d", base->serial, ootx_version);
		return;
	}

	base->firmware_version = version >> 6;

	if (base->serial != __le32_to_cpu(report->serial)) {
		base->serial = __le32_to_cpu(report->serial);
		serial_changed = true;
	}

	for (i = 0; i < 2; i++) {
		struct lighthouse_rotor_calibration *rotor;

		rotor = &base->calibration.rotor[i];
		rotor->tilt = __le16_to_float(report->tilt[i]);
		rotor->phase = __le16_to_float(report->phase[i]);
		rotor->curve = __le16_to_float(report->curve[i]);
		rotor->gibphase = __le16_to_float(report->gibphase[i]);
		rotor->gibmag = __le16_to_float(report->gibmag[i]);
	}

	base->model_id = report->model_id;

	if (serial_changed) {
		LH_INFO(
		    "Lighthouse Base %X: firmware version: %d, model id: %d, "
		    "channel: %c",
		    base->serial, base->firmware_version, base->model_id, base->channel);

		for (i = 0; i < 2; i++) {
			struct lighthouse_rotor_calibration *rotor;

			rotor = &base->calibration.rotor[i];

			LH_INFO(
			    "Lighthouse Base %X: rotor %d: [ %12.9f %12.9f "
			    "%12.9f %12.9f %12.9f ]",
			    base->serial, i, rotor->tilt, rotor->phase, rotor->curve, rotor->gibphase, rotor->gibmag);
		}
	}

	gravity.x = report->gravity[0];
	gravity.y = report->gravity[1];
	gravity.z = report->gravity[2];
	math_vec3_normalize(&gravity);
	if (gravity.x != base->gravity.x || gravity.y != base->gravity.y || gravity.z != base->gravity.z) {
		base->gravity = gravity;
		LH_INFO("Lighthouse Base %X: gravity: [ %9.6f %9.6f %9.6f ]", base->serial, gravity.x, gravity.y,
		        gravity.z);
	}

	if (base->reset_count != report->reset_count) {
		base->reset_count = report->reset_count;
		LH_INFO("Lighthouse Base %X: reset count: %d", base->serial, base->reset_count);
	}
}

static void
lighthouse_base_reset(struct lighthouse_base *base)
{
	base->data_sync = 0;
	base->data_word = -1;
	base->data_bit = 0;
	memset(base->ootx, 0, sizeof(base->ootx));
}

static void
_handle_ootx_data_word(struct lighthouse_watchman *watchman, struct lighthouse_base *base)
{
	uint16_t len = (__le16)*base->ootx;

	/* After 4 OOTX words we have received the base station serial number */
	if (base->data_word == 4) {
		struct lighthouse_ootx_report *report = (void *)(base->ootx + 2);
		uint16_t ootx_version = __le16_to_cpu(report->version) & 0x3f;
		uint32_t serial = __le32_to_cpu(report->serial);

		if (len != 33) {
			LH_WARN("%s: unexpected OOTX frame length %d", watchman->name, len);
			return;
		}

		if (ootx_version == 6 && serial != base->serial) {
			LH_DEBUG("%s: spotted Lighthouse Base %X", watchman->name, serial);
		}
	}
	if (len == 33 && base->data_word == 20) { /* (len + 3)/4 * 2 + 2 */
		_handle_ootx_frame(base);
	}
}

static void
lighthouse_base_handle_ootx_data_bit(struct lighthouse_watchman *watchman, struct lighthouse_base *base, bool data)
{
	if (base->data_word >= (int)sizeof(base->ootx) / 2) {
		base->data_word = -1;
	} else if (base->data_word >= 0) {
		if (base->data_bit == 16) {
			/* Sync bit */
			base->data_bit = 0;
			if (data) {
				base->data_word++;
				_handle_ootx_data_word(watchman, base);
			} else {
				LH_WARN("%s: Missed a sync bit, restarting", watchman->name);
				/* Missing sync bit, restart */
				base->data_word = -1;
			}
		} else if (base->data_bit < 16) {
			/*
			 * Each 16-bit payload word contains two bytes,
			 * transmitted MSB-first.
			 */
			if (data) {
				int idx = 2 * base->data_word + (base->data_bit >> 3);

				base->ootx[idx] |= 0x80 >> (base->data_bit % 8);
			}
			base->data_bit++;
		}
	}

	/* Preamble detection */
	if (data) {
		if (base->data_sync > 16) {
			/* Preamble detected, restart bit capture */
			memset(base->ootx, 0, sizeof(base->ootx));
			base->data_word = 0;
			base->data_bit = 0;
		}
		base->data_sync = 0;
	} else {
		base->data_sync++;
	}
}

static void
lighthouse_base_handle_frame(struct lighthouse_watchman *watchman,
                             struct lighthouse_base *base,
                             uint32_t sync_timestamp)
{
	struct lighthouse_frame *frame = &base->frame[base->active_rotor];

	(void)watchman;

	if (!frame->sweep_ids)
		return;

	frame->frame_duration = sync_timestamp - frame->sync_timestamp;

	/*
	 * If a single base station is running in 'B' mode, skipped frames
	 * will still contain old data.
	 */
	if (frame->frame_duration > 1000000)
		return;

	// telemetry_send_lighthouse_frame(watchman->id, frame);
}

/*
 * The pulse length encodes three bits. The skip bit indicates whether the
 * emitting base will enable the sweeping laser in the next sweep window.
 * The data bit is collected to eventually assemble the OOTX frame. The rotor
 * bit indicates whether the next sweep will be horizontal (0) or vertical (1):
 *
 * duration  3000 3500 4000 4500 5000 5500 6000 6500 (in 48 MHz ticks)
 * skip         0    0    0    0    1    1    1    1
 * data         0    0    1    1    0    0    1    1
 * rotor        0    1    0    1    0    1    0    1
 */
#define SKIP_BIT 4
#define DATA_BIT 2
#define ROTOR_BIT 1

static void
_handle_sync_pulse(struct lighthouse_watchman *watchman, struct lighthouse_pulse *sync)
{
	struct lighthouse_base *base;
	unsigned char channel;
	int32_t dt;
	unsigned int code;

	if (!sync->duration)
		return;

	if (sync->duration < 2750 || sync->duration > 6750) {
		LH_WARN("%s: Unknown pulse length: %d", watchman->name, sync->duration);
		return;
	}
	code = (sync->duration - 2750) / 500;

	dt = sync->timestamp - watchman->last_timestamp;

	/* 48 MHz / 120 Hz = 400000 cycles per sync pulse */
	if (dt > (400000 - 1000) && dt < (400000 + 1000)) {
		/* Observing a single base station, channel A (or B, actually)
		 */
		channel = 'A';
	} else if (dt > (380000 - 1000) && dt < (380000 + 1000)) {
		/* Observing two base stations, this is channel B */
		channel = 'B';
	} else if (dt > (20000 - 1000) && dt < (20000 + 1000)) {
		/* Observing two base stations, this is channel C */
		channel = 'C';
	} else {
		if (dt > -1000 && dt < 1000) {
			/*
			 * Ignore, this means we prematurely finished
			 * assembling the last sync pulse.
			 */
		} else {
			/* Irregular sync pulse */
			if (watchman->last_timestamp)
				LH_WARN(
				    "%s: Irregular sync pulse: %08x -> %08x "
				    "(%+d)",
				    watchman->name, watchman->last_timestamp, sync->timestamp, dt);
			lighthouse_base_reset(&watchman->base[0]);
			lighthouse_base_reset(&watchman->base[1]);
		}

		watchman->last_timestamp = sync->timestamp;
		return;
	}

	base = &watchman->base[channel == 'C'];
	base->channel = channel;
	base->last_sync_timestamp = sync->timestamp;
	lighthouse_base_handle_ootx_data_bit(watchman, base, (code & DATA_BIT));
	lighthouse_base_handle_frame(watchman, base, sync->timestamp);

	base->active_rotor = (code & ROTOR_BIT);
	if (!(code & SKIP_BIT)) {
		struct lighthouse_frame *frame = &base->frame[code & ROTOR_BIT];

		watchman->active_base = base;
		frame->sync_timestamp = sync->timestamp;
		frame->sync_duration = sync->duration;
		frame->sweep_ids = 0;
	}

	watchman->last_timestamp = sync->timestamp;
}

static void
_handle_sweep_pulse(struct lighthouse_watchman *watchman, uint8_t id, uint32_t timestamp, uint16_t duration)
{
	struct lighthouse_base *base = watchman->active_base;
	struct lighthouse_frame *frame;
	int32_t offset;

	(void)id;

	if (!base) {
		LH_WARN("%s: sweep without sync", watchman->name);
		return;
	}

	frame = &base->frame[base->active_rotor];

	offset = timestamp - base->last_sync_timestamp;

	/* Ignore short sync pulses or sweeps without a corresponding sync */
	if (offset > 379000)
		return;

	if (!pulse_in_sweep_window(offset, duration)) {
		LH_WARN(
		    "%s: sweep offset out of range: rotor %u offset %u "
		    "duration %u",
		    watchman->name, base->active_rotor, offset, duration);
		return;
	}

	if (frame->sweep_ids & (1 << id)) {
		LH_WARN("%s: sensor %u hit twice per frame, assuming reflection", watchman->name, id);
		return;
	}

	frame->sweep_duration[id] = duration;
	frame->sweep_offset[id] = offset;
	frame->sweep_ids |= (1 << id);
}

static void
accumulate_sync_pulse(struct lighthouse_watchman *watchman, uint8_t id, uint32_t timestamp, uint16_t duration)
{
	int32_t dt = timestamp - watchman->last_sync.timestamp;

	if (dt > watchman->last_sync.duration || watchman->last_sync.duration == 0) {
		watchman->seen_by = 1 << id;
		watchman->last_sync.timestamp = timestamp;
		watchman->last_sync.duration = duration;
		watchman->last_sync.id = id;
	} else {
		watchman->seen_by |= 1 << id;
		if (timestamp < watchman->last_sync.timestamp) {
			watchman->last_sync.duration += watchman->last_sync.timestamp - timestamp;
			watchman->last_sync.timestamp = timestamp;
		}
		if (duration > watchman->last_sync.duration)
			watchman->last_sync.duration = duration;
		watchman->last_sync.duration = duration;
	}
}

void
lighthouse_watchman_handle_pulse(struct lighthouse_watchman *watchman,
                                 uint8_t id,
                                 uint16_t duration,
                                 uint32_t timestamp)
{
	int32_t dt;

	dt = timestamp - watchman->last_sync.timestamp;

	if (watchman->sync_lock) {
		if (watchman->seen_by && dt > watchman->last_sync.duration) {
			_handle_sync_pulse(watchman, &watchman->last_sync);
			watchman->seen_by = 0;
		}

		if (pulse_in_this_sync_window(dt, duration) || pulse_in_next_sync_window(dt, duration)) {
			accumulate_sync_pulse(watchman, id, timestamp, duration);
		} else if (pulse_in_sweep_window(dt, duration)) {
			_handle_sweep_pulse(watchman, id, timestamp, duration);
		} else {
			/*
			 * Spurious pulse - this could be due to a reflection or
			 * misdetected sync. If dt > period, drop the sync lock.
			 * Maybe we should ignore a single missed sync.
			 */
			if (dt > 407500) {
				watchman->sync_lock = false;
				LH_WARN("%s: late pulse, lost sync", watchman->name);
			} else {
				LH_WARN("%s: spurious pulse: %08x (%02x %d %u)", watchman->name, timestamp, id, dt,
				        duration);
			}
			watchman->seen_by = 0;
		}
	} else {
		/*
		 * If we've not locked onto the periodic sync signals, try to
		 * treat all pulses within the right duration range as potential
		 * sync pulses.
		 * This is still a bit naive. If the sensors are moved too
		 * close to the lighthouse base station, sweep pulse durations
		 * may fall into this range and sweeps may be misdetected as
		 * sync floods.
		 */
		if (duration >= 2750 && duration <= 6750) {
			/*
			 * Decide we've locked on if the pulse falls into any
			 * of the expected time windows from the last
			 * accumulated sync pulse.
			 */
			if (pulse_in_next_sync_window(dt, duration)) {
				LH_WARN("%s: sync locked", watchman->name);
				watchman->sync_lock = true;
			}

			accumulate_sync_pulse(watchman, id, timestamp, duration);
		} else {
			/* Assume this is a sweep, ignore it until we lock */
		}
	}
}

void
lighthouse_watchman_init(struct lighthouse_watchman *watchman, const char *name)
{
	watchman->id = watchman_id++;
	watchman->name = name;
	watchman->seen_by = 0;
	watchman->last_timestamp = 0;
	watchman->last_sync.timestamp = 0;
	watchman->last_sync.duration = 0;
	ll = debug_get_log_option_vive_log();
}
