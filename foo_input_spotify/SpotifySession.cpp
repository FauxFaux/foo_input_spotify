#define MYVERSION "0.1"

#define _WIN32_WINNT 0x0600

#include <foobar2000.h>
#include <libspotify/api.h>

#include <shlobj.h>

#include <stdint.h>
#include <stdlib.h>

#include "SpotifySession.h"

extern "C" {
	extern const uint8_t g_appkey[];
	extern const size_t g_appkey_size;
}

DWORD WINAPI spotifyThread(void *data) {
	SpotifyThreadData *dat = (SpotifyThreadData*)data;

	int nextTimeout = INFINITE;
	while (true) {
		WaitForSingleObject(dat->processEventsEvent, nextTimeout);
		LockedCS lock(dat->cs);
		sp_session_process_events(dat->sess, &nextTimeout);
	}
}

// {FDE57F91-397C-45F6-B907-A40E378DDB7A}
static const GUID spotifyUsernameGuid = 
{ 0xfde57f91, 0x397c, 0x45f6, { 0xb9, 0x7, 0xa4, 0xe, 0x37, 0x8d, 0xdb, 0x7a } };

// {543780A4-2EC2-4EFE-966E-4AC491ACADBA}
static const GUID spotifyPasswordGuid = 
{ 0x543780a4, 0x2ec2, 0x4efe, { 0x96, 0x6e, 0x4a, 0xc4, 0x91, 0xac, 0xad, 0xba } };

static advconfig_string_factory_MT spotifyUsername("Spotify Username", spotifyUsernameGuid, advconfig_entry::guid_root, 1, "", 0);
static advconfig_string_factory_MT spotifyPassword("Spotify Password (plaintext lol)", spotifyPasswordGuid, advconfig_entry::guid_root, 2, "", 0);


void CALLBACK log_message(sp_session *sess, const char *error);
void CALLBACK message_to_user(sp_session *sess, const char *error);
void CALLBACK start_playback(sp_session *sess);
void CALLBACK logged_in(sp_session *sess, sp_error error);
void CALLBACK notify_main_thread(sp_session *sess);
int CALLBACK music_delivery(sp_session *sess, const sp_audioformat *format, const void *frames, int num_frames);
void CALLBACK end_of_track(sp_session *sess);
void CALLBACK play_token_lost(sp_session *sess);

BOOL CALLBACK makeSpotifySession(PINIT_ONCE initOnce, PVOID param, PVOID *context);

SpotifySession::SpotifySession() :
		loggedInEvent(CreateEvent(NULL, TRUE, FALSE, NULL)),
		threadData(spotifyCS) {

	processEventsEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	threadData.processEventsEvent = processEventsEvent;
	threadData.sess = getAnyway();

	memset(&initOnce, 0, sizeof(INIT_ONCE));

	static sp_session_callbacks session_callbacks = {};
	static sp_session_config spconfig = {};

	PWSTR path;
	if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &path))
		throw pfc::exception("couldn't get local app data path");
	size_t num;
	char lpath[MAX_PATH];
	if (wcstombs_s(&num, lpath, MAX_PATH, path, MAX_PATH))
		throw pfc::exception("couldn't convert local app data path");
	if (strcat_s(lpath, "\\foo_input_spotify"))
		throw pfc::exception("couldn't append to path");

	spconfig.api_version = SPOTIFY_API_VERSION,
	spconfig.cache_location = lpath;
	spconfig.settings_location = lpath;
	spconfig.application_key = g_appkey;
	spconfig.application_key_size = g_appkey_size;
	spconfig.user_agent = "spotify-foobar2000-faux-" MYVERSION;
	spconfig.userdata = this;
	spconfig.callbacks = &session_callbacks;

	session_callbacks.logged_in = &logged_in;
	session_callbacks.notify_main_thread = &notify_main_thread;
	session_callbacks.music_delivery = &music_delivery;
	session_callbacks.play_token_lost = &play_token_lost;
	session_callbacks.end_of_track = &end_of_track;
	session_callbacks.log_message = &log_message;
	session_callbacks.message_to_user = &message_to_user;
	session_callbacks.start_playback = &start_playback;

	{
		LockedCS lock(spotifyCS);

		if (NULL == CreateThread(NULL, 0, &spotifyThread, &threadData, 0, NULL)) {
			throw pfc::exception("Couldn't create thread");
		}

		sp_error err = sp_session_create(&spconfig, &sp);

		if (SP_ERROR_OK != err) {
			throw pfc::exception("Couldn't create spotify session");
		}
	}
}

SpotifySession::~SpotifySession() {
	CloseHandle(loggedInEvent);
	CloseHandle(processEventsEvent);
}

sp_session *SpotifySession::getAnyway() {
	return sp;
}

sp_session *SpotifySession::get() {
	InitOnceExecuteOnce(&initOnce,
		makeSpotifySession,
		this,
		NULL);
	return getAnyway();
}

CriticalSection &SpotifySession::getSpotifyCS() {
	return spotifyCS;
}

void SpotifySession::waitForLogin() {
	WaitForSingleObject(loggedInEvent, INFINITE);
}

void SpotifySession::loggedIn(sp_error err) {
	if (SP_ERROR_OK != err) {
		LockedCS lock(spotifyCS);
		pfc::string8 s = "Logging-in went wrong.  This is not recoverable.  Please restart";
		s += sp_error_message(err);
		console::error(s);
	}
	SetEvent(loggedInEvent);
}

void SpotifySession::processEvents() {
	SetEvent(processEventsEvent);
}


BOOL CALLBACK makeSpotifySession(PINIT_ONCE initOnce, PVOID param, PVOID *context) {
	pfc::string8 username;
	spotifyUsername.get(username);

	pfc::string8 password;
	spotifyPassword.get(password);

	SpotifySession *ss = static_cast<SpotifySession *>(param);
	{
		
		sp_session *sess = ss->getAnyway();
		LockedCS lock(ss->getSpotifyCS());
		sp_session_login(sess, username.get_ptr(), password.get_ptr());
	}
	ss->waitForLogin();
	return TRUE;
}

/** sp_session_userdata is assumed to be thread safe. */
SpotifySession *from(sp_session *sess) {
	return static_cast<SpotifySession *>(sp_session_userdata(sess));
}

void CALLBACK log_message(sp_session *sess, const char *error) {
	console::formatter() << "spotify log: " << error;
}

void CALLBACK message_to_user(sp_session *sess, const char *error) {
	console::complain("Message from Spotify", error);
}

void CALLBACK start_playback(sp_session *sess) {
	return;
}

void CALLBACK logged_in(sp_session *sess, sp_error error)
{
	from(sess)->loggedIn(error);
}

void CALLBACK notify_main_thread(sp_session *sess)
{
    from(sess)->processEvents();
}

int CALLBACK music_delivery(sp_session *sess, const sp_audioformat *format,
                          const void *frames, int num_frames)
{
	if (num_frames == 0) {
		from(sess)->buf.flush();
        return 0;
	}

	if (from(sess)->buf.isFull()) {
		return 0;
	}

	const size_t s = num_frames * sizeof(int16_t) * format->channels;

	void *data = new char[s];
	memcpy(data, frames, s);

	from(sess)->buf.add(data, s, format->sample_rate, format->channels);

	return num_frames;
}

void CALLBACK end_of_track(sp_session *sess)
{
	from(sess)->buf.add(NULL, 0, 0, 0);
}

void CALLBACK play_token_lost(sp_session *sess)
{
	console::complain("Message from Spotify", "play token lost");
}
