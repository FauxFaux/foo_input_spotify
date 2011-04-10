#pragma once

#include "util.h"
extern "C" {
#	include <despotify.h>
}

#define sp_track track
#define sp_session despotify_session
#define sp_track_release despotify_free_track
#define sp_link link
#define sp_link_create_from_string(x) despotify_link_from_uri(const_cast<char *>(x))
#define sp_link_as_track(x) despotify_link_get_track(sess, x)
#define sp_playlist playlist
#define sp_error int
#define sp_playlist_create despotify_link_get_playlist
#define sp_playlist_num_tracks(x) (x->num_tracks)
#define sp_playlist_track(playlist, i) (playlist->tracks[i])
#define sp_playlist_release despotify_free_playlist
#define sp_track_duration(x) (x->length)
#define sp_track_artist(x, zero) (x->artist)
#define sp_artist_name(x) (x->name)
#define sp_track_name(x) (x->title)
#define SP_ERROR_OK 0
#define sp_error_message(x) "UNKNOWN"
#define sp_audioformat void
#define sp_session_login despotify_authenticate
#define sp_session_userdata(x) (x->client_callback_data)
#define sp_session_player_load(sess, track) (track->playable)
#define sp_session_player_play(track, go) despotify_play(sess, track, false)

// definitions only work for direct chaining:
#define sp_album_name(x) (x->album)
#define sp_track_album(x) (x)




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

	void loggedIn(int err);

	void processEvents();
};

void assertSucceeds(pfc::string8 msg, sp_error err);
