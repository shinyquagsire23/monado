// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#include "mdns_publisher.h"

#include "mdnssvc/mdns.h"
#include "mdnssvc/mdnssvc.h"
#include "hostname.h"
#include <iostream>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

//(eth.src == 80:f3:ef:fa:cc:8f && eth.dst == f0:2f:4b:09:2a:bd) || (eth.dst == 80:f3:ef:fa:cc:8f && eth.src == f0:2f:4b:09:2a:bd)
avahi_publisher::avahi_publisher(const char *name, std::string type, int port)
    : name(name), type(std::move(type)), port(port), svr(NULL), svc(NULL)
{
	//this->name = "Maxs-MacBook-Pro";
	in_addr addr_v4 = {inet_addr("192.168.50.82")};
	//in6_addr addr = {inet_addr("fe80::806:16b2:7700:70e")};
	in6_addr addr;
	int ret = inet_pton(AF_INET6, "fe80::806:16b2:7700:70e%en0", &addr); // ff02::fb // fe80::9333:21d4:c60a:2647
	printf("Got ret %x\n", ret);
	this->svr = mdnsd_start(addr_v4, true);
	if (this->svr == NULL) {
		printf("mdnsd_start() error\n");
		return;
	}

	//this->name += ".local";
	this->type += ".local";

	//std::string name_local = this->name + ".local";

	mdnsd_set_hostname(this->svr, this->name.c_str(), addr_v4);
	//mdnsd_set_hostname_v6(this->svr, this->name.c_str(), &addr);

	/*struct rr_entry *a2_e = NULL;
	a2_e = rr_create_a(create_nlabel(this->name.c_str()), addr);
	mdnsd_add_rr(svr, a2_e);*/

	char* tmp = (char*)malloc(0x100);
	snprintf(tmp, 0x100, "[fe80::806:16b2:7700:70e]:%u", port); // [fe80::806:16b2:7700:70e]:%u
	//snprintf(tmp, 0x100, "192.168.50.82:%u", port);

	const char *txt[] = { tmp, NULL };
	this->svc = mdnsd_register_svc(this->svr, /*this->name.c_str()*/  "WiVRn" , this->type.c_str(), port, NULL, txt);

	//
}

avahi_publisher::~avahi_publisher()
{
	//mdns_service_remove(svr, svc);
	//mdnsd_stop(svr);
}

AvahiWatch *
avahi_publisher::watch_new(int fd,
	      AvahiWatchEvent event,
	      void (*callback)(AvahiWatch *w, int fd, AvahiWatchEvent event, void *userdata),
	      void *userdata)
{
	return NULL;
}

void
avahi_publisher::watch_free(AvahiWatch *watch)
{
	return;
}

bool
avahi_publisher::iterate(int sleep_time)
{
	return true;
}
