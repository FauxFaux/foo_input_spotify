#include <windows.h>
#include <WinCred.h>
#include <Ntsecapi.h>

int CALLBACK WinMain(
  __in  HINSTANCE hInstance,
  __in  HINSTANCE hPrevInstance,
  __in  LPSTR lpCmdLine,
  __in  int nCmdShow
) {
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

	if (NO_ERROR != res) {
		return res;
	}

	const size_t BUF_SIZE = 0xff;
	WCHAR username[BUF_SIZE] = {};
	WCHAR password[BUF_SIZE] = {};
	WCHAR domain[1] = {};

	DWORD usernameCnt = BUF_SIZE;
	DWORD passwordCnt = BUF_SIZE;
	DWORD domainCnt = 0;

	if (!CredUnPackAuthenticationBuffer(0, outAuth, outAuthCnt,
			username, &usernameCnt,
			domain, &domainCnt,
			password, &passwordCnt)) {
		return -1;
	}

	return 0;
}
