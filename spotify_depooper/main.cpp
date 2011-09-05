#include "SpotifyApiImpl.h"
#include <wininet.h>
#include <windows.h>
#include <sstream>
#include <iostream>
#include <string>

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
	std::function<void()> defaultCheck = [](){
	};

	PipeIn in(inh, defaultCheck);
	PipeOut out(outh);
	PipeIn feedIn(feedInh, defaultCheck);
	PipeOut feedOut(feedOuth);

	funcstr_t warn = [&](std::string msg) {
		feedOut.sync().operation("warn").arg(msg);
	};

	warn("Pre-construct");

	SpotifyApiImpl impl([&]() -> std::string {
		feedOut.sync().operation("user");
		return feedIn.takeString();
	}, [&]() -> std::string {
		feedOut.sync().operation("pass");
		return feedIn.takeString();
	}, warn);

	warn("Post-construct, event loop");

	while (true) {
		const std::string cmd = in.sync().takeCommand();
		if (cmd == "load") {
			out.sync().doReturn([&]() { impl.load(in.takeString()); });
		} else if (cmd == "frtr") {
			out.sync().doReturn([&]() { impl.freeTracks(); });
		} else if (cmd == "init") {
			out.sync().doReturn([&]() { impl.initialise(in.takeLen()); });
		} else if (cmd == "take") {
			Gentry *g;
			out.sync().doReturn([&]() { g = impl.take(); });
			out.arg(g->channels).arg(g->sampleRate).arg(g->size)
				.put(std::string(static_cast<char*>(g->data), static_cast<char*>(g->data) + g->size));
		} else if (cmd == "ssct") {
			out.sync().doReturnInt([&]() { return impl.currentSubsongCount(); });
		} else {
			feedOut.sync().operation("excp").arg("Unknown command: '" + cmd + "'");
			return 3;
		}
	}
}
