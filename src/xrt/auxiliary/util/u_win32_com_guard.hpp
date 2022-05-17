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
#pragma once

#include "xrt/xrt_config_os.h"

#if defined(XRT_OS_WINDOWS)

#include "xrt/xrt_windows.h"
#include "combaseapi.h"

namespace xrt::auxiliary::util {

/**
 * This object makes sure that Windows doesn't close out COM while we're holding on to COM objects.
 *
 * We don't know if the calling thread has initialized COM or how, so this just increments the ref count without
 * really expressing an opinion.
 */
class ComGuard
{
public:
	ComGuard();
	~ComGuard();

	ComGuard(ComGuard const &) = delete;
	ComGuard(ComGuard &&) = delete;
	ComGuard &
	operator=(ComGuard const &) = delete;
	ComGuard &
	operator=(ComGuard &&) = delete;

private:
	CO_MTA_USAGE_COOKIE m_cookie;
};

} // namespace xrt::auxiliary::util

#endif // defined(XRT_OS_WINDOWS) && defined(XRT_HAVE_WIL)
