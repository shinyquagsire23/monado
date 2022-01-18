// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Frame pacing tests.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

#include <util/u_pacing.h>

#include "catch/catch.hpp"

#include "time_utils.hpp"

#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <queue>

using namespace std::chrono_literals;
using namespace std::chrono;

static constexpr unanoseconds frame_interval_ns(16ms);

namespace {

uint64_t
getNextPresentAfterTimestampAndKnownPresent(uint64_t timestamp_ns, uint64_t known_present_ns)
{

	while (known_present_ns < timestamp_ns) {
		known_present_ns += frame_interval_ns.count();
	}
	return known_present_ns;
}
uint64_t
getPresentBefore(uint64_t timestamp_ns, uint64_t known_present_ns)
{

	while (known_present_ns >= timestamp_ns && known_present_ns > frame_interval_ns.count()) {
		known_present_ns -= frame_interval_ns.count();
	}
	return known_present_ns;
}
uint64_t
getNextPresentAfterTimestamp(uint64_t timestamp_ns, uint64_t known_present_ns)
{
	auto present_before_ns = getPresentBefore(timestamp_ns, known_present_ns);
	return getNextPresentAfterTimestampAndKnownPresent(timestamp_ns, present_before_ns);
}

struct CompositorPredictions
{
	int64_t frame_id{0};
	uint64_t wake_up_time_ns{0};
	uint64_t desired_present_time_ns{0};
	uint64_t present_slop_ns{0};
	uint64_t predicted_display_time_ns{0};
	uint64_t predicted_display_period_ns{0};
	uint64_t min_display_period_ns{0};
};
} // namespace

static void
basicPredictionConsistencyChecks(uint64_t now_ns, CompositorPredictions const &predictions)
{
	INFO(predictions.frame_id);
	INFO(now_ns);
	CHECK(predictions.wake_up_time_ns >= now_ns);
	CHECK(predictions.desired_present_time_ns > now_ns);
	CHECK(predictions.desired_present_time_ns > predictions.wake_up_time_ns);
	CHECK(predictions.predicted_display_time_ns > now_ns);
	CHECK(predictions.predicted_display_time_ns > predictions.desired_present_time_ns);
	// display period predicted to be +- 2ms (arbitrary)
	CHECK(unanoseconds(predictions.predicted_display_period_ns) < (frame_interval_ns + unanoseconds(2ms)));
	CHECK(unanoseconds(predictions.predicted_display_period_ns) > (frame_interval_ns - unanoseconds(2ms)));
}

struct SimulatedDisplayTimingData
{
	SimulatedDisplayTimingData(int64_t id, uint64_t desired_present_time, uint64_t gpu_finish, uint64_t now)
	    : frame_id(id), desired_present_time_ns(desired_present_time),
	      actual_present_time_ns(getNextPresentAfterTimestampAndKnownPresent(gpu_finish, desired_present_time)),
	      earliest_present_time_ns(getNextPresentAfterTimestamp(gpu_finish, desired_present_time)),
	      present_margin_ns(earliest_present_time_ns - gpu_finish), now_ns(now)
	{}

	int64_t frame_id;
	uint64_t desired_present_time_ns;
	uint64_t actual_present_time_ns;
	uint64_t earliest_present_time_ns;
	uint64_t present_margin_ns;
	uint64_t now_ns;
	void
	call_u_pc_info(u_pacing_compositor *upc) const
	{
		std::cout << "frame_id:                 " << frame_id << std::endl;
		std::cout << "desired_present_time_ns:  " << desired_present_time_ns << std::endl;
		std::cout << "actual_present_time_ns:   " << actual_present_time_ns << std::endl;
		std::cout << "earliest_present_time_ns: " << earliest_present_time_ns << std::endl;
		std::cout << "present_margin_ns:        " << present_margin_ns << std::endl;
		std::cout << "now_ns:                   " << now_ns << "\n" << std::endl;
		u_pc_info(upc, frame_id, desired_present_time_ns, actual_present_time_ns, earliest_present_time_ns,
		          present_margin_ns, now_ns);
	}
};
static inline bool
operator>(SimulatedDisplayTimingData const &lhs, SimulatedDisplayTimingData const &rhs)
{
	return lhs.now_ns > rhs.now_ns;
}
using SimulatedDisplayTimingQueue = std::priority_queue<SimulatedDisplayTimingData,
                                                        std::vector<SimulatedDisplayTimingData>,
                                                        std::greater<SimulatedDisplayTimingData>>;

//! Process all simulated timing data in the queue that should be processed by now.
static void
processDisplayTimingQueue(SimulatedDisplayTimingQueue &display_timing_queue, uint64_t now_ns, u_pacing_compositor *upc)
{
	while (!display_timing_queue.empty() && display_timing_queue.top().now_ns <= now_ns) {
		display_timing_queue.top().call_u_pc_info(upc);
		display_timing_queue.pop();
	}
}
//! Process all remaining simulated timing data in the queue and return the timestamp of the last one.
static uint64_t
drainDisplayTimingQueue(SimulatedDisplayTimingQueue &display_timing_queue, uint64_t now_ns, u_pacing_compositor *upc)
{
	while (!display_timing_queue.empty()) {
		now_ns = display_timing_queue.top().now_ns;
		display_timing_queue.top().call_u_pc_info(upc);
		display_timing_queue.pop();
	}
	return now_ns;
}

static void
doFrame(SimulatedDisplayTimingQueue &display_timing_queue,
        u_pacing_compositor *upc,
        MockClock &clock,
        uint64_t wake_time_ns,
        uint64_t desired_present_time_ns,
        int64_t frame_id,
        unanoseconds wake_delay,
        unanoseconds begin_delay,
        unanoseconds submit_delay,
        unanoseconds gpu_time_after_submit)
{
	REQUIRE(clock.now() <= wake_time_ns);
	// wake up (after delay)
	clock.advance_to(wake_time_ns);
	clock.advance(wake_delay);
	processDisplayTimingQueue(display_timing_queue, clock.now(), upc);
	u_pc_mark_point(upc, U_TIMING_POINT_WAKE_UP, frame_id, clock.now());

	// begin (after delay)
	clock.advance(begin_delay);
	processDisplayTimingQueue(display_timing_queue, clock.now(), upc);
	u_pc_mark_point(upc, U_TIMING_POINT_BEGIN, frame_id, clock.now());

	// spend cpu time before submit
	clock.advance(submit_delay);
	processDisplayTimingQueue(display_timing_queue, clock.now(), upc);
	u_pc_mark_point(upc, U_TIMING_POINT_SUBMIT, frame_id, clock.now());

	// spend gpu time before present
	clock.advance(gpu_time_after_submit);
	auto gpu_finish = clock.now();
	auto next_scanout_timepoint = getNextPresentAfterTimestampAndKnownPresent(gpu_finish, desired_present_time_ns);

	REQUIRE(next_scanout_timepoint >= gpu_finish);

	// our wisdom arrives after scanout
	MockClock infoClock;
	infoClock.advance_to(next_scanout_timepoint);
	infoClock.advance(1ms);
	display_timing_queue.push({frame_id, desired_present_time_ns, gpu_finish, infoClock.now()});
}

// u_pc is for the compositor, we should take way less than a frame to do our job.
static constexpr auto wakeDelay = microseconds(20);

static constexpr auto shortBeginDelay = microseconds(20);
static constexpr auto shortSubmitDelay = 200us;
static constexpr auto shortGpuTime = 1ms;


static constexpr auto longBeginDelay = 1ms;
static constexpr auto longSubmitDelay = 2ms;
static constexpr auto longGpuTime = 2ms;

TEST_CASE("u_pacing_compositor_display_timing")
{
	u_pacing_compositor *upc = nullptr;
	MockClock clock;
	REQUIRE(XRT_SUCCESS ==
	        u_pc_display_timing_create(frame_interval_ns.count(), &U_PC_DISPLAY_TIMING_CONFIG_DEFAULT, &upc));
	REQUIRE(upc != nullptr);

	clock.advance(1ms);

	CompositorPredictions predictions;
	u_pc_predict(upc, clock.now(), &predictions.frame_id, &predictions.wake_up_time_ns,
	             &predictions.desired_present_time_ns, &predictions.present_slop_ns,
	             &predictions.predicted_display_time_ns, &predictions.predicted_display_period_ns,
	             &predictions.min_display_period_ns);
	basicPredictionConsistencyChecks(clock.now(), predictions);

	auto frame_id = predictions.frame_id;
	SimulatedDisplayTimingQueue queue;


	SECTION("faster than expected")
	{
		// We have a 16ms period
		// wake promptly
		clock.advance(wakeDelay);
		u_pc_mark_point(upc, U_TIMING_POINT_WAKE_UP, frame_id, clock.now());

		// start promptly
		clock.advance(shortBeginDelay);
		u_pc_mark_point(upc, U_TIMING_POINT_BEGIN, frame_id, clock.now());

		// spend cpu time before submit
		clock.advance(shortSubmitDelay);
		u_pc_mark_point(upc, U_TIMING_POINT_SUBMIT, frame_id, clock.now());

		// spend time in gpu rendering until present
		clock.advance(shortGpuTime);
		auto gpu_finish = clock.now();

		auto next_scanout_timepoint =
		    getNextPresentAfterTimestampAndKnownPresent(gpu_finish, predictions.desired_present_time_ns);

		// our wisdom arrives after scanout
		MockClock infoClock;
		infoClock.advance_to(next_scanout_timepoint);
		infoClock.advance(1ms);
		queue.push({frame_id, predictions.desired_present_time_ns, gpu_finish, infoClock.now()});

		// Do basically the same thing a few more frames.
		for (int i = 0; i < 20; ++i) {
			CompositorPredictions loopPred;
			u_pc_predict(upc, clock.now(), &loopPred.frame_id, &loopPred.wake_up_time_ns,
			             &loopPred.desired_present_time_ns, &loopPred.present_slop_ns,
			             &loopPred.predicted_display_time_ns, &loopPred.predicted_display_period_ns,
			             &loopPred.min_display_period_ns);
			CHECK(loopPred.frame_id > i);
			INFO("frame id" << loopPred.frame_id);
			INFO(clock.now());
			basicPredictionConsistencyChecks(clock.now(), loopPred);
			doFrame(queue, upc, clock, loopPred.wake_up_time_ns, loopPred.desired_present_time_ns,
			        loopPred.frame_id, wakeDelay, shortBeginDelay, shortSubmitDelay, shortGpuTime);
		}
		// we should now get a shorter time before present to wake up.
		CompositorPredictions newPred;
		u_pc_predict(upc, clock.now(), &newPred.frame_id, &newPred.wake_up_time_ns,
		             &newPred.desired_present_time_ns, &newPred.present_slop_ns,
		             &newPred.predicted_display_time_ns, &newPred.predicted_display_period_ns,
		             &newPred.min_display_period_ns);
		basicPredictionConsistencyChecks(clock.now(), newPred);
		CHECK(unanoseconds(newPred.desired_present_time_ns - newPred.wake_up_time_ns) <
		      unanoseconds(predictions.desired_present_time_ns - predictions.wake_up_time_ns));
		CHECK(unanoseconds(newPred.desired_present_time_ns - newPred.wake_up_time_ns) >
		      unanoseconds(shortSubmitDelay + shortGpuTime));
	}

	SECTION("slower than desired")
	{
		// We have a 16ms period
		// wake promptly
		clock.advance(wakeDelay);
		u_pc_mark_point(upc, U_TIMING_POINT_WAKE_UP, frame_id, clock.now());

		// waste time before beginframe
		clock.advance(longBeginDelay);
		u_pc_mark_point(upc, U_TIMING_POINT_BEGIN, frame_id, clock.now());

		// spend cpu time before submit
		clock.advance(longSubmitDelay);
		u_pc_mark_point(upc, U_TIMING_POINT_SUBMIT, frame_id, clock.now());

		// spend time in gpu rendering until present
		clock.advance(longGpuTime);
		auto gpu_finish = clock.now();

		auto next_scanout_timepoint =
		    getNextPresentAfterTimestampAndKnownPresent(gpu_finish, predictions.desired_present_time_ns);

		REQUIRE(next_scanout_timepoint > gpu_finish);


		// our wisdom arrives after scanout
		MockClock infoClock;
		infoClock.advance_to(next_scanout_timepoint);
		infoClock.advance(1ms);
		queue.push({frame_id, predictions.desired_present_time_ns, gpu_finish, infoClock.now()});

		// Do basically the same thing a few more frames.
		for (int i = 0; i < 50; ++i) {
			CompositorPredictions loopPred;
			u_pc_predict(upc, clock.now(), &loopPred.frame_id, &loopPred.wake_up_time_ns,
			             &loopPred.desired_present_time_ns, &loopPred.present_slop_ns,
			             &loopPred.predicted_display_time_ns, &loopPred.predicted_display_period_ns,
			             &loopPred.min_display_period_ns);
			INFO(loopPred.frame_id);
			INFO(clock.now());
			basicPredictionConsistencyChecks(clock.now(), loopPred);
			doFrame(queue, upc, clock, loopPred.wake_up_time_ns, loopPred.desired_present_time_ns,
			        loopPred.frame_id, wakeDelay, longBeginDelay, longSubmitDelay, longGpuTime);
		}

		// we should now get a bigger time before present to wake up.
		CompositorPredictions newPred;
		u_pc_predict(upc, clock.now(), &newPred.frame_id, &newPred.wake_up_time_ns,
		             &newPred.desired_present_time_ns, &newPred.present_slop_ns,
		             &newPred.predicted_display_time_ns, &newPred.predicted_display_period_ns,
		             &newPred.min_display_period_ns);
		basicPredictionConsistencyChecks(clock.now(), newPred);
		CHECK(unanoseconds(newPred.desired_present_time_ns - newPred.wake_up_time_ns) >
		      unanoseconds(longBeginDelay + longSubmitDelay + longGpuTime));
	}

	u_pc_destroy(&upc);
}

TEST_CASE("u_pacing_compositor_fake")
{
	MockClock clock;
	u_pacing_compositor *upc = nullptr;
	REQUIRE(XRT_SUCCESS == u_pc_fake_create(frame_interval_ns.count(), clock.now(), &upc));
	REQUIRE(upc != nullptr);

	clock.advance(1ms);

	SECTION("Standalone predictions")
	{
		CompositorPredictions predictions;
		u_pc_predict(upc, clock.now(), &predictions.frame_id, &predictions.wake_up_time_ns,
		             &predictions.desired_present_time_ns, &predictions.present_slop_ns,
		             &predictions.predicted_display_time_ns, &predictions.predicted_display_period_ns,
		             &predictions.min_display_period_ns);
		basicPredictionConsistencyChecks(clock.now(), predictions);
	}
	SECTION("Consistency in loop")
	{
		SimulatedDisplayTimingQueue queue;
		SECTION("Fast")
		{

			for (int i = 0; i < 10; ++i) {
				CompositorPredictions predictions;
				u_pc_predict(upc, clock.now(), &predictions.frame_id, &predictions.wake_up_time_ns,
				             &predictions.desired_present_time_ns, &predictions.present_slop_ns,
				             &predictions.predicted_display_time_ns,
				             &predictions.predicted_display_period_ns,
				             &predictions.min_display_period_ns);
				INFO(predictions.frame_id);
				INFO(clock.now());
				basicPredictionConsistencyChecks(clock.now(), predictions);
				doFrame(queue, upc, clock, predictions.wake_up_time_ns,
				        predictions.desired_present_time_ns, predictions.frame_id, wakeDelay,
				        shortBeginDelay, shortSubmitDelay, shortGpuTime);
			}
			drainDisplayTimingQueue(queue, clock.now(), upc);
		}
		SECTION("Slow")
		{
			for (int i = 0; i < 10; ++i) {
				CompositorPredictions predictions;
				u_pc_predict(upc, clock.now(), &predictions.frame_id, &predictions.wake_up_time_ns,
				             &predictions.desired_present_time_ns, &predictions.present_slop_ns,
				             &predictions.predicted_display_time_ns,
				             &predictions.predicted_display_period_ns,
				             &predictions.min_display_period_ns);
				INFO(predictions.frame_id);
				INFO(clock.now());
				basicPredictionConsistencyChecks(clock.now(), predictions);
				doFrame(queue, upc, clock, predictions.wake_up_time_ns,
				        predictions.desired_present_time_ns, predictions.frame_id, wakeDelay,
				        longBeginDelay, longSubmitDelay, longGpuTime);
			}
			drainDisplayTimingQueue(queue, clock.now(), upc);
		}
	}
	u_pc_destroy(&upc);
}
