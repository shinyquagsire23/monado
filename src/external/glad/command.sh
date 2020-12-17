# SPDX-FileCopyrightText: 2020, Collabora, Ltd. and the Monado contributors
# SPDX-License-Identifier: CC0-1.0

#
# To install glad2 as a user on your system.
#
# git clone <glad-repo>
# git checkout glad2
# pip3 install --user .
#

# command line (for the glad2 branch!)

glad --merge \
	--api='gl:core=4.5,gles2=3.2,egl=1.4' \
	--extensions=\
GL_EXT_external_buffer,\
GL_EXT_memory_object,\
GL_EXT_memory_object_fd,\
GL_EXT_memory_object_win32,\
GL_EXT_YUV_target,\
GL_EXT_sRGB,\
GL_EXT_EGL_image_storage,\
GL_OES_depth_texture,\
GL_OES_rgb8_rgba8,\
GL_OES_packed_depth_stencil,\
GL_OES_EGL_image,\
GL_OES_EGL_image_external,\
GL_OES_EGL_image_external_essl3,\
EGL_KHR_create_context,\
EGL_KHR_gl_colorspace,\
EGL_KHR_fence_sync,\
EGL_KHR_reusable_sync,\
EGL_KHR_wait_sync,\
EGL_KHR_image,\
EGL_KHR_image_base,\
EGL_KHR_platform_android,\
EGL_EXT_image_gl_colorspace,\
EGL_EXT_image_dma_buf_import,\
EGL_EXT_image_dma_buf_import_modifiers,\
EGL_ANDROID_get_native_client_buffer,\
EGL_ANDROID_image_native_buffer,\
EGL_ANDROID_native_fence_sync,\
EGL_ANDROID_front_buffer_auto_refresh,\
EGL_IMG_context_priority,\
	--out-path . c
