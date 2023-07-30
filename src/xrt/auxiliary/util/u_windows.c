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

static const char *
get_priority_string(DWORD dwPriorityClass)
{
	switch (dwPriorityClass) {
	case ABOVE_NORMAL_PRIORITY_CLASS: return "ABOVE_NORMAL_PRIORITY_CLASS";
	case BELOW_NORMAL_PRIORITY_CLASS: return "BELOW_NORMAL_PRIORITY_CLASS";
	case HIGH_PRIORITY_CLASS: return "HIGH_PRIORITY_CLASS";
	case IDLE_PRIORITY_CLASS: return "IDLE_PRIORITY_CLASS";
	case NORMAL_PRIORITY_CLASS: return "NORMAL_PRIORITY_CLASS";
	case PROCESS_MODE_BACKGROUND_BEGIN: return "PROCESS_MODE_BACKGROUND_BEGIN";
	case PROCESS_MODE_BACKGROUND_END: return "PROCESS_MODE_BACKGROUND_END";
	case REALTIME_PRIORITY_CLASS: return "REALTIME_PRIORITY_CLASS";
	default: return "Unknown";
	}
}

static bool
try_to_raise_priority(enum u_logging_level log_level, HANDLE hProcess)
{
	BOOL bRet;
	char buf[512];

	// Doesn't fail
	DWORD dwPriClassAtStart = GetPriorityClass(hProcess);

	if (dwPriClassAtStart == REALTIME_PRIORITY_CLASS) {
		LOG_I("Already have priority 'REALTIME_PRIORITY_CLASS'.");
		return true;
	}

	LOG_D("Trying to raise priority to 'REALTIME_PRIORITY_CLASS'.");

	bRet = SetPriorityClass(hProcess, REALTIME_PRIORITY_CLASS);
	if (bRet == FALSE) {
		LOG_E("SetPriorityClass: %s", GET_LAST_ERROR_STR(buf));
		return false;
	}

	// Doesn't fail
	DWORD dwPriClassNow = GetPriorityClass(hProcess);

	if (dwPriClassNow != dwPriClassAtStart) {
		LOG_I("Raised priority class to '%s'", get_priority_string(dwPriClassNow));
		return true;
	} else {
		LOG_W("Could not raise priority at all, is/was '%s'.", get_priority_string(dwPriClassNow));
		return false;
	}
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

bool
u_win_raise_cpu_priority(enum u_logging_level log_level)
{
	// Always succeeds
	HANDLE hProcess = GetCurrentProcess();

	// Do not need to free hProcess.
	return try_to_raise_priority(log_level, hProcess);
}

void
u_win_try_privilege_or_priority_from_args(enum u_logging_level log_level, int argc, char *argv[])
{
	if (argc > 1 && strcmp(argv[1], "nothing") == 0) {
		LOG_I("Not trying privileges or priority");
	} else if (argc > 1 && strcmp(argv[1], "priv") == 0) {
		LOG_I("Setting privileges");
		u_win_grant_inc_base_priorty_base_privileges(log_level);
	} else if (argc > 1 && strcmp(argv[1], "prio") == 0) {
		LOG_I("Setting priority");
		u_win_raise_cpu_priority(log_level);
	} else {
		LOG_I("Setting both privilege and priority");
		u_win_grant_inc_base_priorty_base_privileges(log_level);
		u_win_raise_cpu_priority(log_level);
	}
}
