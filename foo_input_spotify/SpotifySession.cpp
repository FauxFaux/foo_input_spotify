#include "util.h"

#include <foobar2000.h>
#include <libspotify/api.h>

#include <shlobj.h>

#include <stdint.h>
#include <stdlib.h>

#include "SpotifySession.h"

#include "cred_prompt.h"

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
		loggedInEvent(CreateEvent(NULL, FALSE, FALSE, NULL)),
		threadData(spotifyCS), decoderOwner(NULL) {

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
	if (wcstombs_s(&num, lpath, MAX_PATH, path, MAX_PATH)) {
		CoTaskMemFree(path);
		throw pfc::exception("couldn't convert local app data path");
	}
	CoTaskMemFree(path);

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

		assertSucceeds("creating session", sp_session_create(&spconfig, &sp));
	}
}

SpotifySession::~SpotifySession() {
	CloseHandle(loggedInEvent);
	CloseHandle(processEventsEvent);
}

sp_session *SpotifySession::getAnyway() {
	return sp;
}

struct SpotifySessionData {
	SpotifySessionData(abort_callback & p_abort, SpotifySession *ss) : p_abort(p_abort), ss(ss) {}
	abort_callback & p_abort;
	SpotifySession *ss;
};

sp_session *SpotifySession::get(abort_callback & p_abort) {
	SpotifySessionData ssd(p_abort, this);
	InitOnceExecuteOnce(&initOnce,
		makeSpotifySession,
		&ssd,
		NULL);
	return getAnyway();
}

CriticalSection &SpotifySession::getSpotifyCS() {
	return spotifyCS;
}

/** Does /not/ throw exception_io_data, unlike normal */
pfc::string8 SpotifySession::waitForLogin(abort_callback & p_abort) {
	while (WAIT_OBJECT_0 != WaitForSingleObject(loggedInEvent, 200))
		if (p_abort.is_aborting())
			return "user aborted";
	return loginResult;
}

void SpotifySession::loggedIn(sp_error err) {
	if (SP_ERROR_OK == err)
		loginResult = "";
	else {
		pfc::string8 msg = "logging in";
		loginResult = doctor(msg, err);
	}
	SetEvent(loggedInEvent);
}

void SpotifySession::processEvents() {
	SetEvent(processEventsEvent);
}

bool SpotifySession::hasDecoder(void *owner) {
	return decoderOwner == owner;
}

void SpotifySession::takeDecoder(void *owner) {
	if (!hasDecoder(NULL))
		throw exception_io_data("Someone else is already decoding");
 
	InterlockedCompareExchangePointer(&decoderOwner, owner, NULL);

	if (!hasDecoder(owner))
		throw exception_io_data("Someone else beat us to the decoder");
}

void SpotifySession::ensureDecoder(void *owner) {
	if (!hasDecoder(owner))
		throw exception_io_data("bugcheck: we should own the decoder...");
}

void SpotifySession::releaseDecoder(void *owner) {
	InterlockedCompareExchangePointer(&decoderOwner, NULL, owner);
}

BOOL CALLBACK makeSpotifySession(PINIT_ONCE initOnce, PVOID param, PVOID *context) {
	SpotifySessionData *ssd = static_cast<SpotifySessionData *>(param);
	SpotifySession *ss = ssd->ss;
	sp_session *sess = ss->getAnyway();
	pfc::string8 msg = "Enter your username and password to connect to Spotify";

	while (true) {
		{
			LockedCS lock(ss->getSpotifyCS());
			if (SP_ERROR_NO_CREDENTIALS == sp_session_relogin(sess)) {
				try {
					std::auto_ptr<CredPromptResult> cpr = credPrompt(msg);
					sp_session_login(sess, cpr->un.data(), cpr->pw.data(), cpr->save);
				} catch (std::exception &e) {
					alert(e.what());
					return FALSE;
				}
			}
		}
		msg = ss->waitForLogin(ssd->p_abort);
		if (msg.is_empty())
			return TRUE;
	}
}

/** sp_session_userdata is assumed to be thread safe. */
SpotifySession *from(sp_session *sess) {
	return static_cast<SpotifySession *>(sp_session_userdata(sess));
}

void CALLBACK log_message(sp_session *sess, const char *error) {
	console::formatter() << "spotify log: " << error;
}

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
