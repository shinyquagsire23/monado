// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#include "util/u_trace_marker.h"

#include "wivrn_sockets.h"
#include "wivrn_packets.h"
#include <memory>
#include <unistd.h>
#include <sys/wait.h>
#include <iostream>

#include "mdns_publisher.h"
#include "hostname.h"

// Insert the on load constructor to init trace marker.
U_TRACE_TARGET_SETUP(U_TRACE_WHICH_SERVICE)

extern "C" {
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

int
oxr_sdl2_hack_stop(void **hack_ptr)
{
	return 0;
}
}

using namespace xrt::drivers::wivrn;

extern std::unique_ptr<TCP> tcp;

static void
avahi_callback(AvahiWatch *w, int fd, AvahiWatchEvent event, void *userdata)
{
	bool *client_connected = (bool *)userdata;
	*client_connected = true;
}

int
main(int argc, char *argv[])
{
	u_trace_marker_init();

	while (true) {
		{
			avahi_publisher publisher(hostname().c_str(), "_wivrn._tcp", control_port);

			TCPListener listener(control_port);
			bool client_connected = false;

			AvahiWatch *watch =
			    publisher.watch_new(listener.get_fd(), AVAHI_WATCH_IN, &avahi_callback, &client_connected);

			while (publisher.iterate() && !client_connected)
				;

			publisher.watch_free(watch);

			tcp = std::make_unique<TCP>(listener.accept().first);
		}

#ifdef XRT_FEATURE_SERVICE
		pid_t child = fork();

		if (child < 0) {
			perror("fork");
			return 1;
		}
		if (child == 0) {
			return ipc_server_main(argc, argv);
		} else {
			std::cerr << "Server started, PID " << child << std::endl;

			int wstatus = 0;
			waitpid(child, &wstatus, 0);

			std::cerr << "Server exited, exit status " << WEXITSTATUS(wstatus) << std::endl;
			if (WIFSIGNALED(wstatus)) {
#ifndef XRT_OS_APPLE
#if 0
				std::cerr << "Received signal " << sigabbrev_np(WTERMSIG(wstatus)) << " ("
				          << strsignal(WTERMSIG(wstatus)) << ")"
				          << (WCOREDUMP(wstatus) ? ", core dumped" : "") << std::endl;
#endif
#endif
			}
		}
#endif
	}
}
