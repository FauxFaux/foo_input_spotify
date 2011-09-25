#include "util.h"
#include "cred_prompt.h"

#include "../../pfc/pfc.h"

#include <windows.h>
#include <WinCred.h>
#include <Ntsecapi.h>

#include <vector>

CredPromptResult credPrompt() {
	ULONG authPackage = 0;
	void *outAuth = NULL;
	DWORD outAuthCnt = 0;
	BOOL save = FALSE;

	CREDUI_INFO info = {};
	info.cbSize = sizeof(CREDUI_INFO);
	info.pszCaptionText = L"Enter Spotify Password";
	info.pszMessageText = L"Enter your username and password to connect to Spotify";

	DWORD res = CredUIPromptForWindowsCredentials(&info, 0, &authPackage,
		NULL, 0,
		&outAuth, &outAuthCnt, &save, 
		CREDUIWIN_GENERIC | CREDUIWIN_CHECKBOX
	);

	if (ERROR_SUCCESS != res) {
		throw win32exception("couldn't prompt for credentials", res);
	}

	WCHAR username[CRED_BUF_SIZE] = {};
	WCHAR password[CRED_BUF_SIZE] = {};
	WCHAR domain[1] = {};

	DWORD usernameCnt = CRED_BUF_SIZE;
	DWORD passwordCnt = CRED_BUF_SIZE;
	DWORD domainCnt = 0;

	if (!CredUnPackAuthenticationBuffer(0, outAuth, outAuthCnt,
			username, &usernameCnt,
			domain, &domainCnt,
			password, &passwordCnt)) {
		throw win32exception("couldn't unpack credentials");
	}

	CredPromptResult cpr;
	pfc::stringcvt::convert_wide_to_utf8(cpr.un.data(), CRED_BUF_SIZE, username, usernameCnt);
	pfc::stringcvt::convert_wide_to_utf8(cpr.pw.data(), CRED_BUF_SIZE, password, passwordCnt);
	cpr.save = save ? true : false;
	return cpr;
}
