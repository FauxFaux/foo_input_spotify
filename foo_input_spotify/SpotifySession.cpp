#include "util.h"

#include <foobar2000.h>

#include <shlobj.h>

#include <stdint.h>
#include <stdlib.h>

#include "SpotifySession.h"

pfc::string8 doctor(const pfc::string8 &prefix, sp_session *sess) {
	pfc::string8 msg = prefix;
	msg += " failed: ";
	if (NULL != sess->last_error)
		msg += sp_error_message(sess->last_error);
	else
		msg += "[no last error]";
	return msg;
}

void alert(pfc::string8 msg) {
	console::complain("boom", msg.toString());
}

/* @param msg "logging in" */
void assertSucceeds(pfc::string8 msg, sp_session *sess, sp_error err) {
	if (SP_ERROR_OK == err)
		return;

	throw pfc::exception(doctor(msg, sess));
}

void alertIfFailure(pfc::string8 msg, sp_session *sess, sp_error err) {
	if (SP_ERROR_OK == err)
		return;
	alert(doctor(msg, sess));
}

// {FDE57F91-397C-45F6-B907-A40E378DDB7A}
static const GUID spotifyUsernameGuid = 
{ 0xfde57f91, 0x397c, 0x45f6, { 0xb9, 0x7, 0xa4, 0xe, 0x37, 0x8d, 0xdb, 0x7a } };

// {543780A4-2EC2-4EFE-966E-4AC491ACADBA}
static const GUID spotifyPasswordGuid = 
{ 0x543780a4, 0x2ec2, 0x4efe, { 0x96, 0x6e, 0x4a, 0xc4, 0x91, 0xac, 0xad, 0xba } };

static advconfig_string_factory_MT spotifyUsername("Spotify Username", spotifyUsernameGuid, advconfig_entry::guid_root, 1, "", 0);
static advconfig_string_factory_MT spotifyPassword("Spotify Password (plaintext lol)", spotifyPasswordGuid, advconfig_entry::guid_root, 2, "", 0);

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

	if (!despotify_init())
		throw pfc::exception("despotify_init failed");
	sp = despotify_init_client(NULL, NULL, true, true);
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

BOOL CALLBACK makeSpotifySession(PINIT_ONCE initOnce, PVOID param, PVOID *context) {
	pfc::string8 username;
	spotifyUsername.get(username);

	pfc::string8 password;
	spotifyPassword.get(password);

	SpotifySession *ss = static_cast<SpotifySession *>(param);
	{
		
		sp_session *sess = ss->getAnyway();
		LockedCS lock(ss->getSpotifyCS());
		if (!sp_session_login(sess, username.get_ptr(), password.get_ptr()))
			alert(doctor("logging-in", sess));
	}
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