#include "SpotifyApiImpl.h"
#include <wininet.h>
#include <windows.h>
#include <sstream>
#include <iostream>

int CALLBACK WinMain(
  __in  HINSTANCE hInstance,
  __in  HINSTANCE hPrevInstance,
  __in  LPSTR lpCmdLine,
  __in  int nCmdShow
) {
	// Spotify apparently requires us to have used WinInet. \o/
	InternetCloseHandle(InternetOpen(L"lol", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0));

	std::stringstream cmdline(lpCmdLine);
	HANDLE inh, outh, feedInh, feedOuth;
	cmdline >> inh >> outh >> feedInh >> feedOuth;
	PipeIn in(inh);
	PipeOut out(outh);
	PipeIn feedIn(feedInh);
	PipeOut feedOut(feedOuth);

	SpotifyApiImpl impl([&]() -> std::string {
		feedOut.sync().operation("user");
		return feedIn.takeString();
	}, [&]() -> std::string {
		feedOut.sync().operation("pass");
		return feedIn.takeString();
	});

	while (true) {
		const std::string cmd = in.sync().takeCommand();
		if (cmd == "load") {
			out.doReturn([&]() { impl.load(in.takeString()); });
		} else {
			feedOut.sync().operation("excp").arg("Unknown command: " + cmd);
			std::cout << "Unknown cmd: " << cmd << std::endl;
		}
	}
}
