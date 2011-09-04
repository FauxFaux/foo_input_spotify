#pragma once

#include "../foo_input_spotify/util.h"

#include <libspotify/api.h>
#include <string>
#include <functional>

struct SpotifyThreadData {
	SpotifyThreadData(CriticalSection &cs) : cs(cs) {
	}

	HANDLE processEventsEvent;
	CriticalSection &cs;
	sp_session *sess;
};

class SpotifySession {
	INIT_ONCE initOnce;
	sp_session *sp;
	HANDLE loggedInEvent;
	SpotifyThreadData threadData;
	CriticalSection spotifyCS;
	HANDLE processEventsEvent;
	funcstr_t warn;

public:
	Buffer buf;

	SpotifySession(stringfunc_t username, stringfunc_t password, funcstr_t warn);

	~SpotifySession();

	sp_session *getAnyway();

	sp_session *get();

	CriticalSection &getSpotifyCS();

	void waitForLogin();

	void loggedIn(sp_error err);

	void processEvents();

	stringfunc_t getUsername;
	stringfunc_t getPassword;

	void complain(const char *msg, std::string msg2);
	void alert(std::string msg2);
	void alertIfFailure(std::string msg, sp_error err);
};

void assertSucceeds(std::string msg, sp_error err);
