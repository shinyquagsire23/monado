// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief A collection of strings, like a list of extensions to enable
 *
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 *
 */

#pragma once

#include "u_string_list.h"

#include <memory>
#include <vector>
#include <limits>
#include <stdexcept>
#include <string>
#include <algorithm>

namespace xrt::auxiliary::util {

/*!
 * @brief A collection of strings (const char *), like a list of extensions to enable.
 *
 * This version is only for use with strings that will outlive this object, preferably string literals.
 *
 * Size is limited to one less than the max value of uint32_t which shouldn't be a problem,
 * the size really should be much smaller (especially if you use push_back_unique()).
 */
class StringList
{
public:
	/// Construct a string list.
	StringList() = default;
	/// Construct a string list with the given capacity.
	StringList(uint32_t capacity) : vec(capacity, nullptr)
	{
		// best way I know to create with capacity
		vec.clear();
	}
	StringList(StringList &&) = default;
	StringList(StringList const &) = default;

	StringList &
	operator=(StringList &&) = default;
	StringList &
	operator=(StringList const &) = default;

	/// Construct a string list with the given items
	template <uint32_t N> StringList(const char *(&arr)[N]) : StringList(N)
	{
		for (auto &&elt : arr) {
			push_back(elt);
		}
	}

	/*!
	 * @brief Get the size of the array (the number of strings)
	 */
	uint32_t
	size() const noexcept
	{
		return static_cast<uint32_t>(vec.size());
	}

	/*!
	 * @brief Get the data pointer of the array
	 */
	const char *const *
	data() const noexcept
	{
		return vec.data();
	}

	/*!
	 * @brief Append a new string to the list.
	 *
	 * @param str a non-null, null-terminated string that must live at least as long as the list,
	 *            preferably a string literal.
	 *
	 * @throws std::out_of_range if you have a ridiculous number of strings in your list already,
	 *         std::invalid_argument if you pass a null pointer.
	 */
	void
	push_back(const char *str)
	{

		if (vec.size() > (std::numeric_limits<uint32_t>::max)() - 1) {
			throw std::out_of_range("Size limit reached");
		}
		if (str == nullptr) {
			throw std::invalid_argument("Cannot pass a null pointer");
		}
		vec.push_back(str);
	}

	/// Add all given items
	/// @throws the same as what push_back() throws
	template <uint32_t N>
	void
	push_back_all(const char *(&arr)[N])
	{
		for (auto &&elt : arr) {
			push_back(elt);
		}
	}

	/*!
	 * @brief Check if the string is in the list.
	 *
	 * (Comparing string contents, not pointers)
	 *
	 * @param str a non-null, null-terminated string.
	 *
	 * @return true if the string is in the list.
	 */
	bool
	contains(const char *str)
	{
		if (str == nullptr) {
			throw std::invalid_argument("Cannot pass a null pointer");
		}
		std::string needle{str};
		auto it = std::find_if(vec.begin(), vec.end(), [needle](const char *elt) { return needle == elt; });
		return it != vec.end();
	}

	/*!
	 * @brief Append a new string to the list if it doesn't match any existing string.
	 *
	 * (Comparing string contents, not pointers)
	 *
	 * This does a simple linear search, because it is assumed that the size of this list is fairly small.
	 *
	 * @param str a non-null, null-terminated string that must live at least as long as the list,
	 *            preferably a string literal.
	 *
	 * @return true if we added it
	 *
	 * @throws std::out_of_range if you have a ridiculous number of strings in your list already,
	 *         std::invalid_argument if you pass a null pointer.
	 */
	bool
	push_back_unique(const char *str)
	{
		if (vec.size() > (std::numeric_limits<uint32_t>::max)() - 1) {
			throw std::out_of_range("Size limit reached");
		}
		if (str == nullptr) {
			throw std::invalid_argument("Cannot pass a null pointer");
		}
		std::string needle{str};
		auto it = std::find_if(vec.begin(), vec.end(), [needle](const char *elt) { return needle == elt; });
		if (it != vec.end()) {
			// already have it
			return false;
		}
		vec.push_back(str);
		return true;
	}

private:
	std::vector<const char *> vec;
};

} // namespace xrt::auxiliary::util
