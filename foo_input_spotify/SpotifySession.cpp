#include "util.h"

#include <foobar2000.h>

#include <shlobj.h>

#include <stdint.h>
#include <stdlib.h>

#include "SpotifySession.h"

#ifndef LIBDESPOTIFY_H
DWORD WINAPI spotifyThread(void *data) {
	SpotifyThreadData *dat = (SpotifyThreadData*)data;

	int nextTimeout = INFINITE;
	while (true) {
		WaitForSingleObject(dat->processEventsEvent, nextTimeout);
		LockedCS lock(dat->cs);
		sp_session_process_events(dat->sess, &nextTimeout);
	}
}
#endif

pfc::string8 &doctor(pfc::string8 &msg, sp_error err) {
	msg += " failed: ";
	msg += sp_error_message(err);
	return msg;
}

void alert(pfc::string8 msg) {
	console::complain("boom", msg.toString());
}

/* @param msg "logging in" */
void assertSucceeds(pfc::string8 msg, sp_error err) {
	if (SP_ERROR_OK == err)
		return;

	throw pfc::exception(doctor(msg, err));
}

void alertIfFailure(pfc::string8 msg, sp_error err) {
	if (SP_ERROR_OK == err)
		return;
	alert(doctor(msg, err));
}

// {FDE57F91-397C-45F6-B907-A40E378DDB7A}
static const GUID spotifyUsernameGuid = 
{ 0xfde57f91, 0x397c, 0x45f6, { 0xb9, 0x7, 0xa4, 0xe, 0x37, 0x8d, 0xdb, 0x7a } };

// {543780A4-2EC2-4EFE-966E-4AC491ACADBA}
static const GUID spotifyPasswordGuid = 
{ 0x543780a4, 0x2ec2, 0x4efe, { 0x96, 0x6e, 0x4a, 0xc4, 0x91, 0xac, 0xad, 0xba } };

static advconfig_string_factory_MT spotifyUsername("Spotify Username", spotifyUsernameGuid, advconfig_entry::guid_root, 1, "", 0);
static advconfig_string_factory_MT spotifyPassword("Spotify Password (plaintext lol)", spotifyPasswordGuid, advconfig_entry::guid_root, 2, "", 0);

#ifndef LIBDESPOTIFY_H
void CALLBACK log_message(sp_session *sess, const char *error);
void CALLBACK message_to_user(sp_session *sess, const char *error);
void CALLBACK start_playback(sp_session *sess);
void CALLBACK logged_in(sp_session *sess, sp_error error);
void CALLBACK notify_main_thread(sp_session *sess);
int CALLBACK music_delivery(sp_session *sess, const sp_audioformat *format, const void *frames, int num_frames);
void CALLBACK end_of_track(sp_session *sess);
void CALLBACK play_token_lost(sp_session *sess);
#else
void CALLBACK callback(struct despotify_session* ds, int signal, void* data, void* callback_data) {
	MessageBox(0, L"LOL", L"", 0);
}
#endif

BOOL CALLBACK makeSpotifySession(PINIT_ONCE initOnce, PVOID param, PVOID *context);

SpotifySession::SpotifySession() :
		loggedInEvent(CreateEvent(NULL, TRUE, FALSE, NULL)),
		threadData(spotifyCS) {

	processEventsEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	threadData.processEventsEvent = processEventsEvent;
	threadData.sess = getAnyway();

	memset(&initOnce, 0, sizeof(INIT_ONCE));

	PWSTR path;
	if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &path))
		throw pfc::exception("couldn't get local app data path");

	size_t num;
	char lpath[MAX_PATH];
	if (wcstombs_s(&num, lpath, MAX_PATH, path, MAX_PATH)) {
		CoTaskMemFree(path);
		throw pfc::exception("couldn't convert local app data path");
	}
	CoTaskMemFree(path);

	if (strcat_s(lpath, "\\foo_input_spotify"))
		throw pfc::exception("couldn't append to path");

#ifndef LIBDESPOTIFY_H
	static sp_session_callbacks session_callbacks = {};
	static sp_session_config spconfig = {};

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

		assertSucceeds("creating session", sp_session_create(&spconfig, &sp));
	}
#else
	if (!despotify_init())
		throw pfc::exception("despotify_init failed");
	sp = despotify_init_client(NULL, NULL, true, true);
#endif
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
#ifndef LIBDESPOTIFY_H
	WaitForSingleObject(loggedInEvent, INFINITE);
#endif
}

void SpotifySession::loggedIn(sp_error err) {
	alertIfFailure("logging in", err);
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

#ifndef LIBDESPOTIFY_H
void CALLBACK message_to_user(sp_session *sess, const char *message) {
	alert(message);
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
	alert("play token lost (someone's using your account elsewhere)");
}
#endif