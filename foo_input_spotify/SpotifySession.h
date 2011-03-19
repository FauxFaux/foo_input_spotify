#pragma once

#include "util.h"
#include <libspotify/api.h>

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

	SpotifySession();

	~SpotifySession();

	sp_session *getAnyway();

	sp_session *get();

	CriticalSection &getSpotifyCS();

	void waitForLogin();

	void loggedIn(sp_error err);

	void processEvents();
};

void assertSucceeds(pfc::string8 msg, sp_error err);
