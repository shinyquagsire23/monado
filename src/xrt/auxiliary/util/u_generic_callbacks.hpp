// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Implementation of a generic callback collection, intended to be wrapped for a specific event type.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include <vector>
#include <algorithm>

namespace xrt::auxiliary::util {
template <typename CallbackType, typename EventType> struct GenericCallbacks;

namespace detail {

	/*!
	 * @brief Element type stored in @ref GenericCallbacks, for internal use only.
	 */
	template <typename CallbackType, typename MaskType = uint32_t> struct GenericCallbackEntry
	{
		CallbackType callback;
		MaskType event_mask;
		void *userdata;
		bool should_remove = false;

		GenericCallbackEntry(CallbackType callback_, MaskType event_mask_, void *userdata_) noexcept
		    : callback(callback_), event_mask(event_mask_), userdata(userdata_)
		{}

		/*!
		 * Do the two entries match? Used for removal "by value"
		 */
		bool
		matches(GenericCallbackEntry const &other) const noexcept
		{
			return callback == other.callback && event_mask == other.event_mask &&
			       userdata == other.userdata;
		}

		bool
		operator==(GenericCallbackEntry const &other) const noexcept
		{
			return matches(other);
		}

		bool
		shouldInvoke(MaskType event) const noexcept
		{
			return (event_mask & event) != 0;
		}
	};

	template <typename T> struct identity
	{
		using type = T;
	};

	// This lets us handle being passed an enum (which we can call underlying_type on) as well as an integer (which
	// we cannot)
	template <typename T>
	using mask_from_enum_t =
	    typename std::conditional_t<std::is_enum<T>::value, std::underlying_type<T>, identity<T>>::type;

} // namespace detail

/*!
 * @brief A generic collection of callbacks for event types represented as a bitmask, intended to be wrapped for each
 * usage.
 *
 * A registered callback may identify one or more event types (bits in the bitmask) that it wants to be invoked for. A
 * userdata void pointer is also stored for each callback. Bitmasks are tested at invocation time, and the general
 * callback format allows for callbacks to indicate they should be removed from the collection. Actually calling each
 * callback is left to a consumer-provided "invoker" to allow adding context and event data to the call. The "invoker"
 * also allows the option of whether or how to expose the self-removal capability: yours might simply always return
 * "false".
 *
 * This generic structure supports callbacks that are included multiple times in the collection, if the consuming code
 * needs it. GenericCallbacks::contains may be used by consuming code before conditionally calling addCallback, to
 * limit to a single instance in a collection.
 *
 * @tparam CallbackType the function pointer type to store for each callback.
 * @tparam EventType the event enum type.
 */
template <typename CallbackType, typename EventType> struct GenericCallbacks
{

public:
	static_assert(std::is_integral<EventType>::value || std::is_enum<EventType>::value,
	              "Your event type must either be an integer or an enum");
	using callback_t = CallbackType;
	using event_t = EventType;
	using mask_t = detail::mask_from_enum_t<EventType>;

private:
	static_assert(std::is_integral<mask_t>::value, "Our enum to mask conversion should have produced an integer");

	//! The type stored for each added callback.
	using callback_entry_t = detail::GenericCallbackEntry<CallbackType, mask_t>;

public:
	/*!
	 * @brief Add a new callback entry with the given callback function pointer, event mask, and user data.
	 *
	 * New callback entries are always added at the end of the collection.
	 */
	void
	addCallback(CallbackType callback, mask_t event_mask, void *userdata)
	{
		callbacks.emplace_back(callback, event_mask, userdata);
	}

	/*!
	 * @brief Remove some number of callback entries matching the given callback function pointer, event mask, and
	 * user data.
	 *
	 * @param callback The callback function pointer. Tested for equality with each callback entry.
	 * @param event_mask The callback event mask. Tested for equality with each callback entry.
	 * @param userdata The opaque user data pointer. Tested for equality with each callback entry.
	 * @param num_skip The number of matches to skip before starting to remove callbacks. Defaults to 0.
	 * @param max_remove The number of matches to remove, or negative if no limit. Defaults to -1.
	 *
	 * @returns the number of callbacks removed.
	 */
	int
	removeCallback(
	    CallbackType callback, mask_t event_mask, void *userdata, unsigned int num_skip = 0, int max_remove = -1)
	{
		if (max_remove == 0) {
			// We were told to remove none. We can do this very quickly.
			// Avoids a corner case in the loop where we assume max_remove is non-zero.
			return 0;
		}
		bool found = false;

		const callback_entry_t needle{callback, event_mask, userdata};
		for (auto &entry : callbacks) {
			if (entry.matches(needle)) {
				if (num_skip > 0) {
					// We are still in our skipping phase.
					num_skip--;
					continue;
				}
				entry.should_remove = true;
				found = true;
				// Negatives (no max) get more negative, which is OK.
				max_remove--;
				if (max_remove == 0) {
					// not looking for more
					break;
				}
			}
		}
		if (found) {
			return purgeMarkedCallbacks();
		}
		// if we didn't find any, we removed zero.
		return 0;
	}

	/*!
	 * @brief See if the collection contains at least one matching callback.
	 *
	 * @param callback The callback function pointer. Tested for equality with each callback entry.
	 * @param event_mask The callback event mask. Tested for equality with each callback entry.
	 * @param userdata The opaque user data pointer. Tested for equality with each callback entry.
	 *
	 * @returns true if a matching callback is found.
	 */
	bool
	contains(CallbackType callback, mask_t event_mask, void *userdata)
	{
		const callback_entry_t needle{callback, event_mask, userdata};
		auto it = std::find(callbacks.begin(), callbacks.end(), needle);
		return it != callbacks.end();
	}

	/*!
	 * @brief Invokes the callbacks, by passing the ones we should run to your "invoker" to add any desired
	 * context/event data and forward the call.
	 *
	 * Callbacks are called in order, filtering out those whose event mask does not include the given event.
	 *
	 * @param event The event type to invoke callbacks for.
	 * @param invoker A function/functor accepting the event, a callback function pointer, and the callback entry's
	 * userdata as parameters, and returning true if the callback should be removed from the collection. It is
	 * assumed that the invoker will add any additional context or event data and call the provided callback.
	 *
	 * Typically, a lambda with some captures and a single return statement will be sufficient for an invoker.
	 *
	 * @returns the number of callbacks run
	 */
	template <typename F>
	int
	invokeCallbacks(EventType event, F &&invoker)
	{
		bool needPurge = false;

		int ran = 0;
		for (auto &entry : callbacks) {
			if (entry.shouldInvoke(static_cast<mask_t>(event))) {
				bool willRemove = invoker(event, entry.callback, entry.userdata);
				if (willRemove) {
					entry.should_remove = true;
					needPurge = true;
				}
				ran++;
			}
		}
		if (needPurge) {
			purgeMarkedCallbacks();
		}
		return ran;
	}

private:
	std::vector<callback_entry_t> callbacks;

	int
	purgeMarkedCallbacks()
	{
		auto b = callbacks.begin();
		auto e = callbacks.end();
		auto new_end = std::remove_if(b, e, [](callback_entry_t const &entry) { return entry.should_remove; });
		auto num_removed = std::distance(new_end, e);
		callbacks.erase(new_end, e);
		return static_cast<int>(num_removed);
	}
};
} // namespace xrt::auxiliary::util
