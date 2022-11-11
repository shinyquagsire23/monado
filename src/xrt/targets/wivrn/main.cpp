// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main file for WiVRn Monado service.
 * @author
 * @author
 * @ingroup ipc
 */

#include "util/u_trace_marker.h"

#include "wivrn_sockets.h"
#include "wivrn_packets.h"
#include <memory>
#include <unistd.h>
#include <sys/wait.h>

// Insert the on load constructor to init trace marker.
U_TRACE_TARGET_SETUP(U_TRACE_WHICH_SERVICE)

extern "C"
{
	int
ipc_server_main(int argc, char *argv[]);

int
oxr_sdl2_hack_create(void **out_hack)
{
	return 0;
}

int
oxr_sdl2_hack_start(void *hack, struct xrt_instance *xinst, struct xrt_system_devices *xsysd)
{
	return 0;
}

int oxr_sdl2_hack_stop(void **hack_ptr)
{
	return 0;
}
}

using namespace xrt::drivers::wivrn;

std::unique_ptr<TCP> tcp;

int
main(int argc, char *argv[])
{
	u_trace_marker_init();


	while(true)
	{
		{
			TCPListener listener(control_port);
			tcp = std::make_unique<TCP>(listener.accept().first);
		}

		pid_t child = fork();

		if (child < 0)
		{
			perror("fork");
			return 1;
		}
		if (child == 0)
		{
			return ipc_server_main(argc, argv);
		}
		else
		{
			waitpid(child, nullptr, 0);
		}
	}
}
