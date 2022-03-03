// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Generic unique_ptr deleters for Monado types
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include <memory>

namespace xrt {

/*!
 * Generic deleter functors for the variety of interface/object types in Monado.
 *
 * Use these with std::unique_ptr to make per-interface type aliases for unique ownership.
 * These are stateless deleters whose function pointer is statically specified as a template argument.
 */
namespace deleters {
	/*!
	 * Deleter type for interfaces with destroy functions that take pointers to interface pointers (so they may be
	 * zeroed).
	 */
	template <typename T, void (*DeleterFn)(T **)> struct ptr_ptr_deleter
	{
		void
		operator()(T *obj) const noexcept
		{
			if (obj == nullptr) {
				return;
			}
			DeleterFn(&obj);
		}
	};

	/*!
	 * Deleter type for interfaces with destroy functions that take just pointers.
	 */
	template <typename T, void (*DeleterFn)(T *)> struct ptr_deleter
	{
		void
		operator()(T *obj) const noexcept
		{
			if (obj == nullptr) {
				return;
			}
			DeleterFn(obj);
		}
	};

	/*!
	 * Deleter type for ref-counted interfaces with two-parameter `reference(dest, src)` functions.
	 */
	template <typename T, void (*ReferenceFn)(T **, T *)> struct reference_deleter
	{
		void
		operator()(T *obj) const noexcept
		{
			if (obj == nullptr) {
				return;
			}
			ReferenceFn(&obj, nullptr);
		}
	};
} // namespace deleters

} // namespace xrt
