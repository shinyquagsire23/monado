// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Various helpers for doing Windows specific things.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @ingroup aux_os
 */

#include "xrt/xrt_windows.h"
#include "util/u_windows.h"

#include "assert.h"


/*
 *
 * 'Exported' functions.
 *
 */

const char *
u_winerror(char *s, size_t size, DWORD err, bool remove_end)
{
	DWORD dSize = (DWORD)size;
	assert(dSize == size);
	BOOL bRet;

	bRet = FormatMessageA(          //
	    FORMAT_MESSAGE_FROM_SYSTEM, //
	    NULL,                       //
	    err,                        //
	    LANG_SYSTEM_DEFAULT,        //
	    s,                          //
	    dSize,                      //
	    NULL);                      //
	if (!bRet) {
		s[0] = 0;
	}

	if (!remove_end) {
		return s;
	}

	// Remove newline and period from message.
	size = strnlen_s(s, size);
	for (size_t i = size; i-- > 0;) {
		switch (s[i]) {
		case '.':
		case '\n':
		case '\r': //
			s[i] = '\0';
			continue;
		default: break;
		}
		break;
	}

	return s;
}
