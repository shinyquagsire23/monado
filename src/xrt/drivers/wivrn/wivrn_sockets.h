// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "wivrn_serialization.h"

#include <netinet/ip.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace xrt::drivers::wivrn {
class socket_shutdown : public std::exception
{
public:
	const char *
	what() const noexcept override;
};

class invalid_packet : public std::exception
{
public:
	const char *
	what() const noexcept override;
};

template <typename T, typename... Ts> struct index_of_type
{
};

template <typename T, typename... Ts> struct index_of_type<T, T, Ts...> : std::integral_constant<size_t, 0>
{
};

template <typename T, typename T2, typename... Ts>
struct index_of_type<T, T2, Ts...> : std::integral_constant<size_t, 1 + index_of_type<T, Ts...>::value>
{
};

static_assert(index_of_type<int, int, float>::value == 0);
static_assert(index_of_type<float, int, float>::value == 1);

class socket_base
{
protected:
	int fd = -1;

	socket_base() = default;
	socket_base(const socket_base &) = delete;
	socket_base(socket_base &&);
	~socket_base();

public:
	int
	get_fd() const
	{
		return fd;
	}
};

class UDP : public socket_base
{
public:
	UDP();
	UDP(const UDP &) = delete;
	UDP(UDP &&) = default;

	std::vector<uint8_t>
	receive_raw();
	std::pair<std::vector<uint8_t>, sockaddr_in6>
	receive_from_raw();
	void
	send_raw(const std::vector<uint8_t> &data);

	void
	connect(in6_addr address, int port);
	void
	bind(int port);
	void
	subscribe_multicast(in6_addr address);
	void
	unsubscribe_multicast(in6_addr address);
	void
	set_receive_buffer_size(int size);
};

class TCP : public socket_base
{
public:
	TCP(in6_addr address, int port);
	explicit TCP(int fd);
	TCP(const TCP &) = delete;
	TCP(TCP &&) = default;

	std::vector<uint8_t>
	receive_raw();
	void
	send_raw(const std::vector<uint8_t> &data);
};

class TCPListener : public socket_base
{
public:
	TCPListener(int port);
	TCPListener(const TCPListener &) = delete;
	TCPListener(TCPListener &&) = default;

	std::pair<TCP, sockaddr_in6>
	accept();
};

template <typename Socket, typename ReceivedType, typename SentType> class typed_socket : public Socket
{
public:
	template <typename... Args> typed_socket(Args &&...args) : Socket(std::forward<Args>(args)...) {}

	ReceivedType
	receive()
	{
		deserialization_packet p(this->receive_raw());
		return p.deserialize<ReceivedType>();
	}

	std::pair<ReceivedType, sockaddr_in6>
	receive_from()
	{
		auto [packet, addr] = this->receive_from_raw();
		return {deserialization_packet(packet).deserialize<ReceivedType>(), addr};
	}

	template <typename T = SentType, typename = std::enable_if_t<std::is_same_v<T, SentType>>>
	void
	send(const T &data)
	{
		serialization_packet p;
		p.serialize(data);
		this->send_raw(std::move(p));
	}
};

} // namespace xrt::drivers::wivrn
