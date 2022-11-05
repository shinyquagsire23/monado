// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#include "wivrn_session.h"
#include "os/os_time.h"
#include "util/u_logging.h"

#include "wivrn_controller.h"
#include "wivrn_hmd.h"

wivrn_session::wivrn_session() {}

int
wivrn_session::wait_for_handshake(xrt_device **out_device)
{
	auto self = std::shared_ptr<wivrn_session>(new wivrn_session());
	auto control = self->session.poll_control(-1);
	if (control) {
		const auto &info = std::get<from_headset::headset_info_packet>(*control);
		self->hmd = std::make_unique<wivrn_hmd>(self, info);
		self->left_hand = std::make_unique<wivrn_controller>(0, self->hmd.get(), self);
		self->right_hand = std::make_unique<wivrn_controller>(1, self->hmd.get(), self);
	} else {
		throw std::runtime_error("no packet received from headset, failed to initialise");
	}

	int n = 0;
	if (self->hmd)
		out_device[n++] = self->hmd.get();
	if (self->left_hand)
		out_device[n++] = self->left_hand.get();
	if (self->right_hand)
		out_device[n++] = self->right_hand.get();

#if 0
	// For debugging purposes
	self->feedback_csv.open("feedback.csv");
#endif

	self->thread = std::thread(&wivrn_session::run, self);
	return n;
}

clock_offset
wivrn_session::get_offset()
{
	std::lock_guard lock(mutex);
	return offset;
}

void
wivrn_session::operator()(from_headset::headset_info_packet &&)
{
	U_LOG_W("unexpected headset info packet, ignoring");
}
void
wivrn_session::operator()(from_headset::tracking &&tracking)
{
	if (offset.epoch_offset.count() == 0)
		return;

	hmd->update_tracking(tracking, offset);
	left_hand->update_tracking(tracking, offset);
	right_hand->update_tracking(tracking, offset);
}
void
wivrn_session::operator()(from_headset::inputs &&inputs)
{
	left_hand->set_inputs(inputs);
	right_hand->set_inputs(inputs);
}

void
wivrn_session::operator()(from_headset::timesync_response &&timesync)
{
	std::lock_guard lock(mutex);
	offset.epoch_offset = std::chrono::nanoseconds(timesync.response) - timesync.query;
	offset_age = std::chrono::steady_clock::now();
}

void
wivrn_session::operator()(from_headset::feedback &&feedback)
{
	if (feedback_csv) {
		feedback_csv << feedback.frame_index << "," << feedback.received_first_packet << ","
		             << feedback.received_last_packet << "," << feedback.reconstructed << ","
		             << feedback.sent_to_decoder << "," << feedback.received_from_decoder << ","
		             << feedback.blitted << "," << feedback.displayed << ",";

		XrQuaternionf q;
		XrVector3f x;

		q = feedback.received_pose[0].orientation;
		feedback_csv << q.x << "," << q.y << "," << q.z << "," << q.w << ",";
		x = feedback.received_pose[0].position;
		feedback_csv << x.x << "," << x.y << "," << x.z << ",";

		q = feedback.received_pose[1].orientation;
		feedback_csv << q.x << "," << q.y << "," << q.z << "," << q.w << ",";
		x = feedback.received_pose[1].position;
		feedback_csv << x.x << "," << x.y << "," << x.z << ",";

		q = feedback.real_pose[0].orientation;
		feedback_csv << q.x << "," << q.y << "," << q.z << "," << q.w << ",";
		x = feedback.real_pose[0].position;
		feedback_csv << x.x << "," << x.y << "," << x.z << ",";

		q = feedback.real_pose[1].orientation;
		feedback_csv << q.x << "," << q.y << "," << q.z << "," << q.w << ",";
		x = feedback.real_pose[1].position;
		feedback_csv << x.x << "," << x.y << "," << x.z << std::endl;
	}
}

uint64_t
clock_offset::from_headset(uint64_t ts) const
{
	return ts - epoch_offset.count();
}

std::chrono::nanoseconds
clock_offset::to_headset(uint64_t timestamp_ns) const
{
	return std::chrono::nanoseconds(timestamp_ns) + epoch_offset;
}

void
wivrn_session::run(std::weak_ptr<wivrn_session> weak_self)
{
	while (true) {
		try {
			auto self = weak_self.lock();
			if (self and not self->quit) {
				{
					std::lock_guard lock(self->mutex);
					if (std::chrono::steady_clock::now() - self->offset_age >
					    std::chrono::seconds(5)) {
						to_headset::timesync_query timesync{};
						timesync.query = std::chrono::nanoseconds(os_monotonic_get_ns());
						self->session.send_stream(timesync);
					}
				}
				self->session.poll(*self, 20);
			} else
				return;
		} catch (const std::exception &e) {
			U_LOG_E("exception in network thread: %s", e.what());
			auto self = weak_self.lock();
			if (self)
				self->quit = true;
			return;
		}
	}
}
