// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGL functions to drive the gui.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#include "xrt/xrt_frame.h"
#include "util/u_misc.h"
#include "ogl/ogl_api.h"

#include "gui_common.h"

#include <pthread.h>


/*!
 * An @ref xrt_frame_sink that shows sunk frames in the GUI.
 * @implements xrt_frame_sink
 * @implements xrt_frame_node
 */
struct gui_ogl_sink
{
	struct gui_ogl_texture tex;

	struct xrt_frame_sink sink;
	struct xrt_frame_node node;

	struct xrt_frame *frame;

	pthread_mutex_t mutex;

	bool running;
};

static void
push_frame(struct xrt_frame_sink *xs, struct xrt_frame *xf)
{
	struct gui_ogl_sink *s = container_of(xs, struct gui_ogl_sink, sink);

	// The fields are protected.
	pthread_mutex_lock(&s->mutex);

	// If we are in the process of shutting down, don't take the reference.
	if (s->running) {
		xrt_frame_reference(&s->frame, xf);
	}

	// Done
	pthread_mutex_unlock(&s->mutex);
}

static void
break_apart(struct xrt_frame_node *node)
{
	struct gui_ogl_sink *s = container_of(node, struct gui_ogl_sink, node);

	// Stop receiving any more reference.
	pthread_mutex_lock(&s->mutex);
	s->running = false;
	pthread_mutex_unlock(&s->mutex);
}

static void
destroy(struct xrt_frame_node *node)
{
	struct gui_ogl_sink *s = container_of(node, struct gui_ogl_sink, node);

	glDeleteTextures(1, &s->tex.id);

	pthread_mutex_destroy(&s->mutex);

	free(s);
}

static void
update_r8g8b8(struct gui_ogl_sink *s, GLint w, GLint h, uint8_t *data)
{
	glBindTexture(GL_TEXTURE_2D, s->tex.id);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
	glBindTexture(GL_TEXTURE_2D, 0);
}

static void
update_l8(struct gui_ogl_sink *s, GLint w, GLint h, uint8_t *data)
{
	glBindTexture(GL_TEXTURE_2D, s->tex.id);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data);
	GLint swizzleMask[] = {GL_RED, GL_RED, GL_RED, GL_ONE};
	glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void
gui_ogl_sink_update(struct gui_ogl_texture *tex)
{
	struct gui_ogl_sink *s = container_of(tex, struct gui_ogl_sink, tex);
	(void)s;

	// Take the frame no need to adjust reference.
	pthread_mutex_lock(&s->mutex);
	struct xrt_frame *frame = s->frame;
	s->frame = NULL;
	pthread_mutex_unlock(&s->mutex);

	if (frame == NULL) {
		return;
	}

	GLint w, h;
	uint8_t *data;

	w = frame->width;
	h = frame->height;

	if (tex->w != (uint32_t)w || tex->h != (uint32_t)h) {
		tex->w = w;
		tex->h = h;

		// Automatically set the half scaling.
		if (tex->w >= 1024 || tex->h >= 1024) {
			tex->half = true;
		}
	}

	tex->seq = frame->source_sequence;
	data = frame->data;

	switch (frame->format) {
	case XRT_FORMAT_R8G8B8: update_r8g8b8(s, w, h, data); break;
	case XRT_FORMAT_L8: update_l8(s, w, h, data); break;
	default: break;
	}

	xrt_frame_reference(&frame, NULL);
}

struct gui_ogl_texture *
gui_ogl_sink_create(const char *name, struct xrt_frame_context *xfctx, struct xrt_frame_sink **out_sink)
{
	struct gui_ogl_sink *s = U_TYPED_CALLOC(struct gui_ogl_sink);
	int ret = 0;

	s->sink.push_frame = push_frame;
	s->node.break_apart = break_apart;
	s->node.destroy = destroy;
	s->tex.name = name;
	s->tex.w = 256;
	s->tex.h = 256;
	s->running = true;

	ret = pthread_mutex_init(&s->mutex, NULL);
	if (ret != 0) {
		free(s);
		return NULL;
	}

	// Temporary texture
	glGenTextures(1, &s->tex.id);
	glBindTexture(GL_TEXTURE_2D, s->tex.id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	GLint w = 1;
	GLint h = 1;
	struct xrt_colour_rgb_u8 pink = {255, 0, 255};

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, &pink.r);

	glBindTexture(GL_TEXTURE_2D, 0);

	*out_sink = &s->sink;

	return &s->tex;
}
