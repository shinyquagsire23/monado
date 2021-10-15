#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2020-2022, Collabora, Ltd. and the Monado contributors
# SPDX-License-Identifier: CC0-1.0

tmpdir=$(mktemp -d)
trap "rm -rf $tmpdir" EXIT
echo "Creating python3 venv in a temporary directory"
python3 -m venv "${tmpdir}/venv"
. "${tmpdir}/venv"/bin/activate

echo "Installing glad2 from git"
python3 -m pip install git+https://github.com/Dav1dde/glad.git@glad2

# command line (for the glad2 branch!)

echo "GLAD2 generation"

glad --merge \
	--api='gl:core=4.5,gles2=3.2,egl=1.4,glx=1.3,wgl=1.0' \
	--extensions=\
EGL_ANDROID_front_buffer_auto_refresh,\
EGL_ANDROID_get_native_client_buffer,\
EGL_ANDROID_image_native_buffer,\
EGL_ANDROID_native_fence_sync,\
EGL_EXT_image_dma_buf_import,\
EGL_EXT_image_dma_buf_import_modifiers,\
EGL_EXT_image_gl_colorspace,\
EGL_IMG_context_priority,\
EGL_KHR_create_context,\
EGL_KHR_fence_sync,\
EGL_KHR_gl_colorspace,\
EGL_KHR_image,\
EGL_KHR_image_base,\
EGL_KHR_no_config_context,\
EGL_KHR_platform_android,\
EGL_KHR_reusable_sync,\
EGL_KHR_wait_sync,\
GL_EXT_EGL_image_storage,\
GL_EXT_YUV_target,\
GL_EXT_external_buffer,\
GL_EXT_memory_object,\
GL_EXT_memory_object_fd,\
GL_EXT_memory_object_win32,\
GL_EXT_sRGB,\
GL_EXT_semaphore,\
GL_OES_EGL_image,\
GL_OES_EGL_image_external,\
GL_OES_EGL_image_external_essl3,\
GL_OES_depth_texture,\
GL_OES_packed_depth_stencil,\
GL_OES_rgb8_rgba8,\
	--out-path . c
