// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief An object that serves to keep the reference count of COM initialization greater than 0.
 *
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 *
 */
#include "u_win32_com_guard.hpp"
#include "xrt/xrt_config_have.h"


#if defined(XRT_OS_WINDOWS) && defined(XRT_HAVE_WIL)
#include <wil/result.h>

namespace xrt::auxiliary::util {


ComGuard::ComGuard()
{
	THROW_IF_FAILED(CoIncrementMTAUsage(&m_cookie));
}
ComGuard::~ComGuard()
{
	LOG_IF_FAILED(CoDecrementMTAUsage(m_cookie));
}

} // namespace xrt::auxiliary::util
#endif // defined(XRT_OS_WINDOWS) && defined(XRT_HAVE_WIL)
