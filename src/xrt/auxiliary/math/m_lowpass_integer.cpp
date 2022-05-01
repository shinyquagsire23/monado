// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Wrap integer filters for C
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_math
 */

#include "m_lowpass_integer.h"
#include "m_lowpass_integer.hpp"

#include "util/u_logging.h"

#include <memory>

using xrt::auxiliary::math::IntegerLowPassIIRFilter;
// using xrt::auxiliary::math::Rational;
using Rational64 = xrt::auxiliary::math::Rational<int64_t>;

struct m_lowpass_integer
{
	m_lowpass_integer(Rational64 alpha) : filter(alpha) {}

	IntegerLowPassIIRFilter<int64_t> filter;
};

#define DEFAULT_CATCH(...)                                                                                             \
	catch (std::exception const &e)                                                                                \
	{                                                                                                              \
		U_LOG_E("Caught exception: %s", e.what());                                                             \
		return __VA_ARGS__;                                                                                    \
	}                                                                                                              \
	catch (...)                                                                                                    \
	{                                                                                                              \
		U_LOG_E("Caught exception");                                                                           \
		return __VA_ARGS__;                                                                                    \
	}

struct m_lowpass_integer *
m_lowpass_integer_create(int64_t alpha_numerator, int64_t alpha_denominator)
{
	try {
		if (alpha_denominator <= 0) {
			return nullptr;
		}
		if (alpha_numerator <= 0 || alpha_numerator >= alpha_denominator) {
			return nullptr;
		}
		auto ret = std::make_unique<m_lowpass_integer>(Rational64{alpha_numerator, alpha_denominator});
		return ret.release();
	}
	DEFAULT_CATCH(nullptr)
}

void
m_lowpass_integer_add_sample(struct m_lowpass_integer *mli, int64_t sample)
{
	try {
		mli->filter.addSample(sample);
	}
	DEFAULT_CATCH()
}

int64_t
m_lowpass_integer_get_state(const struct m_lowpass_integer *mli)
{

	try {
		return mli->filter.getState();
	}
	DEFAULT_CATCH(0)
}

bool
m_lowpass_integer_is_initialized(const struct m_lowpass_integer *mli)
{

	try {
		return mli->filter.isInitialized();
	}
	DEFAULT_CATCH(false)
}

void
m_lowpass_integer_destroy(struct m_lowpass_integer **ptr_to_mli)
{
	try {
		if (ptr_to_mli == nullptr) {
			return;
		}
		struct m_lowpass_integer *mli = *ptr_to_mli;
		if (mli == nullptr) {
			return;
		}
		delete mli;
		*ptr_to_mli = nullptr;
	}
	DEFAULT_CATCH()
}
