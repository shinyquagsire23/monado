// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  C++ wrappers for workers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @ingroup aux_util
 */

#include "util/u_worker.hpp"


void
xrt::auxiliary::util::TaskCollection::cCallback(void *data_ptr)
{
	auto &f = *static_cast<Functor *>(data_ptr);
	f();
	f = nullptr;
}
