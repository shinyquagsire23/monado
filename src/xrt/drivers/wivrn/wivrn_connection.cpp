// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#include "wivrn_connection.h"
#include <poll.h>

using namespace std::chrono_literals;

static sockaddr_in6
wait_announce()
{
	typed_socket<UDP, from_headset::client_announce_packet, void> receiver;

	receiver.subscribe_multicast(announce_address);
	receiver.bind(announce_port);

	details::hash_context h;
	serialization_traits<from_headset::control_packets>::type_hash(h);
	serialization_traits<to_headset::control_packets>::type_hash(h);
	serialization_traits<from_headset::stream_packets>::type_hash(h);
	serialization_traits<to_headset::stream_packets>::type_hash(h);

	while (true) {
		auto [packet, sender] = receiver.receive_from();

		if (packet.magic != packet.magic_value)
			continue;

		if (packet.protocol_hash != h.hash)
			continue;

		receiver.unsubscribe_multicast(announce_address);
		return sender;
	}
}


wivrn_connection::wivrn_connection(in6_addr address) : control(address, control_port)
{
	stream.bind(stream_port);
	stream.connect(address, stream_port);
}

wivrn_connection::wivrn_connection() : wivrn_connection(wait_announce().sin6_addr) {}

void
wivrn_connection::send_control(const to_headset::control_packets &packet)
{
	control.send(packet);
}

void
wivrn_connection::send_stream(const to_headset::stream_packets &packet)
{
	stream.send(packet);
}

std::optional<from_headset::stream_packets>
wivrn_connection::poll_stream(int timeout)
{
	pollfd fds{};
	fds.events = POLLIN;
	fds.fd = stream.get_fd();

	int r = ::poll(&fds, 1, timeout);
	if (r < 0)
		throw std::system_error(errno, std::system_category());

	if (r > 0 && (fds.revents & POLLIN)) {
		return stream.receive();
	}

	return {};
}

std::optional<from_headset::control_packets>
wivrn_connection::poll_control(int timeout)
{
	pollfd fds{};
	fds.events = POLLIN;
	fds.fd = control.get_fd();

	int r = ::poll(&fds, 1, timeout);
	if (r < 0)
		throw std::system_error(errno, std::system_category());

	if (r > 0 && (fds.revents & POLLIN)) {
		return control.receive();
	}

	return {};
}
