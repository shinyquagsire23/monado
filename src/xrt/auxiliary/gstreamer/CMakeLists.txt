# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_library(
	aux_gstreamer STATIC
	gst_internal.h
	gst_sink.h
	gst_sink.c
	gst_pipeline.h
	gst_pipeline.c
	)
target_link_libraries(aux_gstreamer PUBLIC aux-includes)
target_link_libraries(aux_gstreamer PRIVATE xrt-interfaces aux_math aux_os ${GST_LIBRARIES})
target_include_directories(aux_gstreamer PRIVATE ${GST_INCLUDE_DIRS})
