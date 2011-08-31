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
public:
	Buffer buf;

	SpotifySession(stringfunc_t username, stringfunc_t password);

	~SpotifySession();

	sp_session *getAnyway();

	sp_session *get();

	CriticalSection &getSpotifyCS();

	void waitForLogin();

	void loggedIn(sp_error err);

	void processEvents();

	stringfunc_t getUsername;
	stringfunc_t getPassword;
};

void assertSucceeds(std::string msg, sp_error err);
