// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Wrap float filters for C
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_math
 */

#include "m_lowpass_float.h"
#include "m_lowpass_float.hpp"

#include "util/u_logging.h"

#include <memory>

using xrt::auxiliary::math::LowPassIIRFilter;

struct m_lowpass_float
{
	m_lowpass_float(float cutoff_hz) : filter(cutoff_hz) {}

	LowPassIIRFilter<float> filter;
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

struct m_lowpass_float *
m_lowpass_float_create(float cutoff_hz)
{
	try {
		auto ret = std::make_unique<m_lowpass_float>(cutoff_hz);
		return ret.release();
	}
	DEFAULT_CATCH(nullptr)
}

void
m_lowpass_float_add_sample(struct m_lowpass_float *mlf, float sample, timepoint_ns timestamp_ns)
{
	try {
		mlf->filter.addSample(sample, timestamp_ns);
	}
	DEFAULT_CATCH()
}

float
m_lowpass_float_get_state(const struct m_lowpass_float *mlf)
{

	try {
		return mlf->filter.getState();
	}
	DEFAULT_CATCH(0)
}

timepoint_ns
m_lowpass_float_get_timestamp_ns(const struct m_lowpass_float *mlf)
{
	try {
		return mlf->filter.getTimestampNs();
	}
	DEFAULT_CATCH(0)
}

bool
m_lowpass_float_is_initialized(const struct m_lowpass_float *mlf)
{

	try {
		return mlf->filter.isInitialized();
	}
	DEFAULT_CATCH(false)
}

void
m_lowpass_float_destroy(struct m_lowpass_float **ptr_to_mlf)
{
	try {
		if (ptr_to_mlf == nullptr) {
			return;
		}
		struct m_lowpass_float *mlf = *ptr_to_mlf;
		if (mlf == nullptr) {
			return;
		}
		delete mlf;
		*ptr_to_mlf = nullptr;
	}
	DEFAULT_CATCH()
}
