// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Metrics saving functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "os/os_threading.h"

#include "util/u_metrics.h"
#include "util/u_debug.h"

#include "monado_metrics.pb.h"
#include "pb_encode.h"

#include <stdio.h>

#define VERSION_MAJOR 1
#define VERSION_MINOR 1

static FILE *g_file = NULL;
static struct os_mutex g_file_mutex;
static bool g_metrics_initialized = false;
static bool g_metrics_early_flush = false;

DEBUG_GET_ONCE_OPTION(metrics_file, "XRT_METRICS_FILE", NULL)
DEBUG_GET_ONCE_BOOL_OPTION(metrics_early_flush, "XRT_METRICS_EARLY_FLUSH", false)



/*
 *
 * Helper functions.
 *
 */

static void
write_record(monado_metrics_Record *r)
{
	uint8_t buffer[monado_metrics_Record_size + 10]; // Including submessage


	pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
	bool ret = pb_encode_submessage(&stream, &monado_metrics_Record_msg, r);
	if (!ret) {
		U_LOG_E("Failed to encode metrics message!");
		return;
	}

	os_mutex_lock(&g_file_mutex);

	fwrite(buffer, stream.bytes_written, 1, g_file);

	if (g_metrics_early_flush) {
		fflush(g_file);
	}

	os_mutex_unlock(&g_file_mutex);
}

static void
write_version(uint32_t major, uint32_t minor)
{
	if (!g_metrics_initialized) {
		return;
	}

	monado_metrics_Record record = monado_metrics_Record_init_default;

	// Select which filed is used.
	record.which_record = monado_metrics_Record_version_tag;
	record.record.version.major = major;
	record.record.version.minor = minor;

	write_record(&record);
}


/*
 *
 * 'Exported' functions.
 *
 */

void
u_metrics_init(void)
{
	const char *str = debug_get_option_metrics_file();
	if (str == NULL) {
		U_LOG_D("No metrics file!");
		return;
	}

	g_file = fopen(str, "wb");
	if (g_file == NULL) {
		U_LOG_E("Could not open '%s'!", str);
		return;
	}

	os_mutex_init(&g_file_mutex);

	g_metrics_initialized = true;
	g_metrics_early_flush = debug_get_bool_option_metrics_early_flush();

	write_version(VERSION_MAJOR, VERSION_MINOR);

	U_LOG_I("Opened metrics file: '%s'", str);
}

void
u_metrics_close(void)
{
	if (!g_metrics_initialized) {
		return;
	}

	U_LOG_I("Closing metrics file: '%s'", debug_get_option_metrics_file());

	// At least try to avoid races.
	os_mutex_lock(&g_file_mutex);
	fflush(g_file);
	fclose(g_file);
	g_file = NULL;
	os_mutex_unlock(&g_file_mutex);

	os_mutex_destroy(&g_file_mutex);

	g_metrics_initialized = false;
}

bool
u_metrics_is_active(void)
{
	return g_metrics_initialized;
}

void
u_metrics_write_session_frame(struct u_metrics_session_frame *umsf)
{
	if (!g_metrics_initialized) {
		return;
	}

	monado_metrics_Record record = monado_metrics_Record_init_default;

	// Select which filed is used.
	record.which_record = monado_metrics_Record_session_frame_tag;

#define COPY(_0, _1, _2, _3, FIELD, _4) (record.record.session_frame.FIELD = umsf->FIELD);
	monado_metrics_SessionFrame_FIELDLIST(COPY, 0);
#undef COPY


	write_record(&record);
}

void
u_metrics_write_used(struct u_metrics_used *umu)
{
	if (!g_metrics_initialized) {
		return;
	}

	monado_metrics_Record record = monado_metrics_Record_init_default;

	// Select which filed is used.
	record.which_record = monado_metrics_Record_used_tag;

#define COPY(_0, _1, _2, _3, FIELD, _4) (record.record.used.FIELD = umu->FIELD);
	monado_metrics_Used_FIELDLIST(COPY, 0);
#undef COPY


	write_record(&record);
}

void
u_metrics_write_system_frame(struct u_metrics_system_frame *umsf)
{
	if (!g_metrics_initialized) {
		return;
	}

	monado_metrics_Record record = monado_metrics_Record_init_default;

	// Select which filed is used.
	record.which_record = monado_metrics_Record_system_frame_tag;

#define COPY(_0, _1, _2, _3, FIELD, _4) (record.record.system_frame.FIELD = umsf->FIELD);
	monado_metrics_SystemFrame_FIELDLIST(COPY, 0);
#undef COPY


	write_record(&record);
}

void
u_metrics_write_system_gpu_info(struct u_metrics_system_gpu_info *umgi)
{
	if (!g_metrics_initialized) {
		return;
	}

	monado_metrics_Record record = monado_metrics_Record_init_default;

	// Select which filed is used.
	record.which_record = monado_metrics_Record_system_gpu_info_tag;

#define COPY(_0, _1, _2, _3, FIELD, _4) (record.record.system_gpu_info.FIELD = umgi->FIELD);
	monado_metrics_SystemGpuInfo_FIELDLIST(COPY, 0);
#undef COPY


	write_record(&record);
}

void
u_metrics_write_system_present_info(struct u_metrics_system_present_info *umpi)
{
	if (!g_metrics_initialized) {
		return;
	}

	monado_metrics_Record record = monado_metrics_Record_init_default;

	// Select which filed is used.
	record.which_record = monado_metrics_Record_system_present_info_tag;

#define COPY(_0, _1, _2, _3, FIELD, _4) (record.record.system_present_info.FIELD = umpi->FIELD);
	monado_metrics_SystemPresentInfo_FIELDLIST(COPY, 0);
#undef COPY


	write_record(&record);
}
