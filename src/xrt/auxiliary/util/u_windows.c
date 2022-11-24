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
#include "util/u_logging.h"

#include "assert.h"


/*
 *
 * Helper functions.
 *
 */

#define LOG_D(...) U_LOG_IFL_D(log_level, __VA_ARGS__)
#define LOG_I(...) U_LOG_IFL_I(log_level, __VA_ARGS__)
#define LOG_W(...) U_LOG_IFL_W(log_level, __VA_ARGS__)
#define LOG_E(...) U_LOG_IFL_E(log_level, __VA_ARGS__)

#define GET_LAST_ERROR_STR(BUF) (u_winerror(BUF, ARRAY_SIZE(BUF), GetLastError(), true))


static bool
check_privilege_on_process(HANDLE hProcess, LPCTSTR lpszPrivilege, LPBOOL pfResult)
{
	PRIVILEGE_SET ps;
	LUID luid;
	HANDLE hToken;
	BOOL bRet, bHas;
	char buf[512];


	bRet = LookupPrivilegeValue( //
	    NULL,                    //
	    lpszPrivilege,           //
	    &luid);                  //
	if (!bRet) {
		U_LOG_E("LookupPrivilegeValue: '%s'", GET_LAST_ERROR_STR(buf));
		return false;
	}

	bRet = OpenProcessToken( //
	    hProcess,            //
	    TOKEN_QUERY,         //
	    &hToken);            //
	if (!bRet) {
		U_LOG_E("OpenProcessToken: '%s'", GET_LAST_ERROR_STR(buf));
		return false;
	}

	ps.PrivilegeCount = 1;
	ps.Control = PRIVILEGE_SET_ALL_NECESSARY;
	ps.Privilege[0].Luid = luid;
	ps.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

	bRet = PrivilegeCheck( //
	    hToken,            //
	    &ps,               //
	    &bHas);            //

	CloseHandle(hToken); // Done with token now.

	if (!bRet) {
		U_LOG_E("PrivilegeCheck: '%s'", GET_LAST_ERROR_STR(buf));
		return false;
	}

	*pfResult = bHas;

	return true;
}

static bool
enable_privilege_on_process(HANDLE hProcess, LPCTSTR lpszPrivilege)
{
	TOKEN_PRIVILEGES tp;
	LUID luid;
	HANDLE hToken;
	BOOL bRet;
	char buf[512];

	bRet = LookupPrivilegeValue( //
	    NULL,                    //
	    lpszPrivilege,           //
	    &luid);                  //
	if (!bRet) {
		U_LOG_E("LookupPrivilegeValue: '%s'", GET_LAST_ERROR_STR(buf));
		return false;
	}

	bRet = OpenProcessToken(     //
	    hProcess,                //
	    TOKEN_ADJUST_PRIVILEGES, //
	    &hToken);                //
	if (!bRet) {
		U_LOG_E("OpenProcessToken: '%s'", GET_LAST_ERROR_STR(buf));
		return false;
	}

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	bRet = AdjustTokenPrivileges( //
	    hToken,                   //
	    FALSE,                    //
	    &tp,                      //
	    sizeof(TOKEN_PRIVILEGES), //
	    (PTOKEN_PRIVILEGES)NULL,  //
	    (PDWORD)NULL);            //

	CloseHandle(hToken); // Done with token now.

	if (!bRet) {
		U_LOG_E("AdjustTokenPrivileges: '%s'", GET_LAST_ERROR_STR(buf));
		return false;
	}

	if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
		U_LOG_D("AdjustTokenPrivileges return ok but we got:\n\t'%s'", GET_LAST_ERROR_STR(buf));
		return false;
	}

	return true;
}

bool
try_to_grant_privilege(enum u_logging_level log_level, HANDLE hProcess, LPCTSTR lpszPrivilege)
{
	BOOL bRet, bHas;

	if (check_privilege_on_process(hProcess, lpszPrivilege, &bHas)) {
		LOG_D("%s: %s", lpszPrivilege, bHas ? "true" : "false");
		if (bHas) {
			LOG_I("Already had privilege '%s'.");
			return true;
		}
	}

	LOG_D("Trying to grant privilege '%s'.", lpszPrivilege);

	bRet = enable_privilege_on_process(hProcess, lpszPrivilege);

	if (check_privilege_on_process(hProcess, lpszPrivilege, &bHas)) {
		LOG_D("%s: %s", lpszPrivilege, bHas ? "true" : "false");
		if (bHas == TRUE) {
			LOG_I("Granted privilege '%s'.", lpszPrivilege);
			return true;
		}
	}

	LOG_I("Failed to grant privilege '%s'.", lpszPrivilege);

	return false;
}


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

bool
u_win_grant_inc_base_priorty_base_privileges(enum u_logging_level log_level)
{
	// Always succeeds
	HANDLE hProcess = GetCurrentProcess();

	// Do not need to free hProcess.
	return try_to_grant_privilege(log_level, hProcess, SE_INC_BASE_PRIORITY_NAME);
}
