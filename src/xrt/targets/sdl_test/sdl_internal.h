// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Internal header for SDL XR system.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup sdl_test
 */

#include "xrt/xrt_system.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_instance.h"
#include "xrt/xrt_tracking.h"
#include "xrt/xrt_compositor.h"

#include "util/u_pacing.h"
#include "util/u_logging.h"
#include "util/comp_base.h"
#include "util/comp_swapchain.h"

#include "SDL2/SDL.h"

#include "ogl/ogl_api.h"


#ifdef __cplusplus
extern "C" {
#endif

struct sdl_program;

/*!
 * Sub-class of @ref comp_swapchain, used to do OpenGL rendering.
 *
 * @ingroup sdl_test
 */
struct sdl_swapchain
{
	struct comp_swapchain base;

	//! Pointer back to main program.
	struct sdl_program *sp;

	//! Cached width and height.
	int w, h;

	//! Number of textures in base.base.base.image_count.
	GLuint textures[XRT_MAX_SWAPCHAIN_IMAGES];

	//! Same number of images as textures.
	GLuint memory[XRT_MAX_SWAPCHAIN_IMAGES];
};

/*!
 * State to emulate state transitions correctly.
 *
 * @ingroup sdl_test
 */
enum sdl_comp_state
{
	SDL_COMP_STATE_UNINITIALIZED = 0,
	SDL_COMP_STATE_READY = 1,
	SDL_COMP_STATE_PREPARED = 2,
	SDL_COMP_STATE_VISIBLE = 3,
	SDL_COMP_STATE_FOCUSED = 4,
};

/*!
 * Tracking frame state.
 *
 * @ingroup sdl_test
 */
struct sdl_comp_frame
{
	int64_t id;
	uint64_t predicted_display_time_ns;
	uint64_t desired_present_time_ns;
	uint64_t present_slop_ns;
};

/*!
 * Split out for convinecne.
 *
 * @ingroup sdl_test
 */
struct sdl_compositor
{
	//! Base native compositor.
	struct comp_base base;

	//! Pacing helper to drive us forward.
	struct u_pacing_compositor *upc;

	struct
	{
		//! Frame interval that we are using.
		uint64_t frame_interval_ns;
	} settings;

	// Kept here for convenience.
	struct xrt_system_compositor_info sys_info;

	//! State for generating the correct set of events.
	enum sdl_comp_state state;

	//! @todo Insert your own required members here

	struct
	{
		struct sdl_comp_frame waited;
		struct sdl_comp_frame rendering;
	} frame;
};

struct sdl_program_plus;

/*!
 * C base class for the SDL program.
 *
 * @ingroup sdl_test
 */
struct sdl_program
{
	//! Base class for devices.
	struct xrt_device xdev_base;

	//! Instance base.
	struct xrt_instance xinst_base;

	//! System devices base.
	struct xrt_system_devices xsysd_base;

	//! SDL compositor struct.
	struct sdl_compositor c;

	//! Created system compositor.
	struct xrt_system_compositor *xsysc;

	//! Inputs exposed by the SDL device.
	struct xrt_input inputs[1];

	//! HMD parts exposed by the SDL device to become a HMD.
	struct xrt_hmd_parts hmd;

	//! Tracking origin that the device is located in.
	struct xrt_tracking_origin origin;

	//! The current log level.
	enum u_logging_level log_level;

	struct
	{
		struct
		{
			//! The pose of the head, only used for view space.
			struct xrt_pose pose;
		} head;

		struct
		{
			//! Pose of each individual eye.
			struct xrt_pose pose;

			//! Fov of each individual eye.
			struct xrt_fov fov;
		} left, right;
	} state;

	//! The main window.
	SDL_Window *win;

	//! Main OpenGL context.
	SDL_GLContext ctx;

	//! Protects the OpenGL context.
	struct os_mutex current_mutex;

	//! Pointer back to the C++ part of the program.
	struct sdl_program_plus *spp;
};


static inline struct sdl_program *
from_xinst(struct xrt_instance *xinst)
{
	return container_of(xinst, struct sdl_program, xinst_base);
}

static inline struct sdl_program *
from_xsysd(struct xrt_system_devices *xsysd)
{
	return container_of(xsysd, struct sdl_program, xsysd_base);
}

static inline struct sdl_program *
from_xdev(struct xrt_device *xdev)
{
	return container_of(xdev, struct sdl_program, xdev_base);
}

static inline struct sdl_program *
from_comp(struct xrt_compositor *xc)
{
	return container_of(xc, struct sdl_program, c.base.base);
}


/*!
 * Spew level logging.
 *
 * @relates sdl_program
 * @ingroup sdl_test
 */
#define SP_TRACE(sp, ...) U_LOG_IFL_T(sp->log_level, __VA_ARGS__);

/*!
 * Debug level logging.
 *
 * @relates sdl_program
 */
#define SP_DEBUG(sp, ...) U_LOG_IFL_D(sp->log_level, __VA_ARGS__);

/*!
 * Info level logging.
 *
 * @relates sdl_program
 * @ingroup sdl_test
 */
#define SP_INFO(sp, ...) U_LOG_IFL_I(sp->log_level, __VA_ARGS__);

/*!
 * Warn level logging.
 *
 * @relates sdl_program
 * @ingroup sdl_test
 */
#define SP_WARN(sp, ...) U_LOG_IFL_W(sp->log_level, __VA_ARGS__);

/*!
 * Error level logging.
 *
 * @relates sdl_program
 * @ingroup sdl_test
 */
#define SP_ERROR(sp, ...) U_LOG_IFL_E(sp->log_level, __VA_ARGS__);

/*!
 * Check for OpenGL errors, context needs to be current.
 *
 * @ingroup sdl_test
 */
#define CHECK_GL()                                                                                                     \
	do {                                                                                                           \
		GLint err = glGetError();                                                                              \
		if (err != 0) {                                                                                        \
			U_LOG_RAW("%s:%u: error: 0x%04x", __func__, __LINE__, err);                                    \
		}                                                                                                      \
	} while (false)

/*!
 * Makes the OpenGL context current in this thread, takes lock.
 *
 * @ingroup sdl_test
 */
static inline void
sdl_make_current(struct sdl_program *sp)
{
	os_mutex_lock(&sp->current_mutex);
	SDL_GL_MakeCurrent(sp->win, sp->ctx);
}

/*!
 * Unmakes the any OpenGL context current in this thread, releases the lock.
 *
 * @ingroup sdl_test
 */
static inline void
sdl_make_uncurrent(struct sdl_program *sp)
{
	SDL_GL_MakeCurrent(NULL, NULL);
	os_mutex_unlock(&sp->current_mutex);
}


/*
 *
 * sdl_device.c
 *
 */

/*!
 * Init the @ref xrt_device sub struct.
 *
 * In sdl_device.c
 *
 * @ingroup sdl_test
 */
void
sdl_device_init(struct sdl_program *sp);


/*
 *
 * sdl_swapchain.c
 *
 */

/*!
 * Implementation of @ref xrt_compositor::create_swapchain.
 *
 * @ingroup sdl_test
 */
xrt_result_t
sdl_swapchain_create(struct xrt_compositor *xc,
                     const struct xrt_swapchain_create_info *info,
                     struct xrt_swapchain **out_xsc);

/*!
 * Implementation of @ref xrt_compositor::import_swapchain.
 *
 * @ingroup sdl_test
 */
xrt_result_t
sdl_swapchain_import(struct xrt_compositor *xc,
                     const struct xrt_swapchain_create_info *info,
                     struct xrt_image_native *native_images,
                     uint32_t native_image_count,
                     struct xrt_swapchain **out_xsc);


/*
 *
 * sdl_compositor.c
 *
 */

/*!
 * Initializes the compositor part of the SDL program.
 *
 * @ingroup sdl_test
 */
void
sdl_compositor_init(struct sdl_program *sp);

/*!
 * Creates the system compositor that wraps the native compositor.
 *
 * @ingroup sdl_test
 */
xrt_result_t
sdl_compositor_create_system(struct sdl_program *sp, struct xrt_system_compositor **out_xsysc);


/*
 *
 * sdl_instance.c
 *
 */

/*!
 * Init the @ref xrt_system_devices sub struct.
 *
 * @ingroup sdl_test
 */
void
sdl_system_devices_init(struct sdl_program *sp);

/*!
 * Init the @ref xrt_instance sub struct.
 *
 * @ingroup sdl_test
 */
void
sdl_instance_init(struct sdl_program *sp);


/*
 *
 * sdl_program.cpp
 *
 */

/*!
 * Create the SDL program.
 *
 * @ingroup sdl_test
 */
struct sdl_program *
sdl_program_plus_create();

/*!
 * Render a frame, called by the compositor when layers have been committed.
 *
 * @ingroup sdl_test
 */
void
sdl_program_plus_render(struct sdl_program_plus *spp);

/*!
 * Destroy the SDL program.
 *
 * @ingroup sdl_test
 */
void
sdl_program_plus_destroy(struct sdl_program_plus *spp);


#ifdef __cplusplus
}
#endif
