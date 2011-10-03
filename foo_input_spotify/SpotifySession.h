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
	pfc::string8 loginResult;
	__declspec(align(2)) volatile PVOID decoderOwner;
public:
	Buffer buf;

	SpotifySession();

	~SpotifySession();

	sp_session *getAnyway();

	sp_session *get(abort_callback & p_abort);

	CriticalSection &getSpotifyCS();

	pfc::string8 waitForLogin(abort_callback & p_abort);

	void loggedIn(sp_error err);

	void processEvents();

	void takeDecoder(void *owner);
	void ensureDecoder(void *owner);
	void releaseDecoder(void *owner);
	bool hasDecoder(void *owner);
};

void assertSucceeds(pfc::string8 msg, sp_error err);
