// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

//#include "xrt/xrt_gfx_native.h"
#include "xrt/xrt_system.h"
#include "xrt/xrt_config_build.h"

#include "os/os_time.h"

#include "util/u_debug.h"
#include "util/u_trace_marker.h"
#include "util/u_system_helpers.h"

#include "main/comp_main_interface.h"

#include <assert.h>

#include "mdns_publisher.h"
#include "hostname.h"

#include "wivrn_session.h"

/*
 *
 * Internal functions.
 *
 */

std::unique_ptr<TCP> tcp;

static xrt_result_t
wivrn_instance_create_system(struct xrt_instance *xinst,
                             struct xrt_system_devices **out_xsysd,
                             struct xrt_system_compositor **out_xsysc)
{
	assert(out_xsysd != NULL);
	assert(*out_xsysd == NULL);
	assert(out_xsysc == NULL || *out_xsysc == NULL);

	struct xrt_system_compositor *xsysc = NULL;
	struct xrt_system_devices *xsysd = xrt::drivers::wivrn::wivrn_session::create_session(std::move(*tcp));
	tcp.reset();

	if (!xsysd)
		return XRT_ERROR_DEVICE_CREATION_FAILED;

	xrt_result_t xret = XRT_SUCCESS;


	struct xrt_device *head = xsysd->roles.head;

	if (xret == XRT_SUCCESS && xsysc == NULL) {
		xret = comp_main_create_system_compositor(head, NULL, &xsysc);
	}

	if (xret != XRT_SUCCESS) {
		xrt_system_devices_destroy(&xsysd);
		return xret;
	}

	*out_xsysd = xsysd;
	*out_xsysc = xsysc;

	return xret;
}

static void
wivrn_instance_destroy(struct xrt_instance *xinst)
{
	delete xinst;
}

static xrt_result_t
wivrn_instance_get_prober(struct xrt_instance *xinst, struct xrt_prober **out_xp)
{
	*out_xp = nullptr;
	return XRT_ERROR_PROBER_NOT_SUPPORTED;
}

/*
 *
 * Exported function(s).
 *
 */

static void
avahi_callback(AvahiWatch *w, int fd, AvahiWatchEvent event, void *userdata)
{
	bool *client_connected = (bool *)userdata;
	*client_connected = true;
}

extern "C" xrt_result_t
wivrn_xrt_instance_create(struct xrt_instance_info *ii, struct xrt_instance **out_xinst)
{
	u_trace_marker_init();

	avahi_publisher publisher(hostname().c_str(), "_wivrn._tcp", control_port);

	TCPListener listener(control_port);
	//bool client_connected = false;

	//AvahiWatch *watch =
	//    publisher.watch_new(listener.get_fd(), AVAHI_WATCH_IN, &avahi_callback, &client_connected);

	//while (publisher.iterate() && !client_connected)
	//	;

	//publisher.watch_free(watch);

	tcp = std::make_unique<TCP>(listener.accept().first);
	printf("Got connection!\n");

	struct xrt_instance *xinst = U_TYPED_CALLOC(struct xrt_instance);
	xinst->create_system = wivrn_instance_create_system;
	xinst->get_prober = wivrn_instance_get_prober;
	xinst->destroy = wivrn_instance_destroy;

	xinst->startup_timestamp = os_monotonic_get_ns();

	*out_xinst = xinst;

	return XRT_SUCCESS;
}

#ifdef XRT_FEATURE_SERVICE_WIVRN
xrt_result_t
xrt_instance_create(struct xrt_instance_info *ii, struct xrt_instance **out_xinst)
{
	u_trace_marker_init();

	return wivrn_xrt_instance_create(ii, out_xinst);
}
#endif
