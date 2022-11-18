// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "mdnssvc/mdnssvc.h"
#include <string>

typedef void AvahiWatch;
typedef int AvahiWatchEvent;
#define AVAHI_WATCH_IN 0

class avahi_publisher
{
	std::string name;
	std::string type;
	int port;

	struct mdnsd *svr;
	struct mdns_service* svc;

public:
	avahi_publisher(const char *name, std::string type, int port);

	~avahi_publisher();

	AvahiWatch *
	watch_new(int fd,
	          AvahiWatchEvent event,
	          void (*callback)(AvahiWatch *w, int fd, AvahiWatchEvent event, void *userdata),
	          void *userdata);

	void
	watch_free(AvahiWatch *watch);

	bool
	iterate(int sleep_time = -1);
};
