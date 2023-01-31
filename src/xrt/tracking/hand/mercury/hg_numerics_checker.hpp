// Copyright 2021-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Simple util for setting floating-point exceptions and checking for NaNs.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */

#undef PEDANTIC_NAN_CHECKS
#undef NAN_EXCEPTIONS

#ifdef NAN_EXCEPTIONS
#include <cfenv>
#endif

namespace xrt::tracking::hand::mercury::numerics_checker {

#ifdef PEDANTIC_NAN_CHECKS
#define CHECK_NOT_NAN(val)                                                                                             \
	do {                                                                                                           \
		if (val != val) {                                                                                      \
			U_LOG_E(" was NAN at %d", __LINE__);                                                           \
			assert(false);                                                                                 \
		}                                                                                                      \
                                                                                                                       \
	} while (0)

#else
#define CHECK_NOT_NAN(val) (void)val;
#endif

#ifdef NAN_EXCEPTIONS
static int ex = FE_DIVBYZERO | FE_OVERFLOW | FE_INVALID;


static inline void
set_floating_exceptions()
{
	// NO: FE_UNDERFLOW, FE_INEXACT
	// https://stackoverflow.com/questions/60731382/c-setting-floating-point-exception-environment
	feenableexcept(ex); // Uncomment this for version 2
}

static inline void
remove_floating_exceptions()
{
	fedisableexcept(ex);
}

#else
static inline void
set_floating_exceptions()
{}

static inline void
remove_floating_exceptions()
{}
#endif


} // namespace xrt::tracking::hand::mercury::numerics_checker