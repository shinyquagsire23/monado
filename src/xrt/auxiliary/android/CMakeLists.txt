# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_library(
	android_app_glue STATIC
	${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
	)
target_include_directories(android_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)
# disable these warnings in external code
target_compile_options(android_app_glue PRIVATE -Wno-format-pedantic)

add_library(
	aux_android STATIC
	android_ahardwarebuffer_allocator.c
	android_ahardwarebuffer_allocator.h
	android_custom_surface.cpp
	android_custom_surface.h
	android_globals.cpp
	android_globals.h
	android_load_class.cpp
	android_load_class.hpp
	android_looper.cpp
	android_looper.h
	org.freedesktop.monado.auxiliary.cpp
	org.freedesktop.monado.auxiliary.hpp
	org.freedesktop.monado.auxiliary.impl.hpp
	)
target_link_libraries(
	aux_android
	PUBLIC aux_util
	PRIVATE
		${ANDROID_LIBRARY}
		${ANDROID_LOG_LIBRARY}
		xrt-external-jni-wrap
		xrt-external-jnipp
		android_app_glue
	)
