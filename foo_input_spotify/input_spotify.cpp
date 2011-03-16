#define MYVERSION "0.1"

#define _WIN32_WINNT 0x0600

#include <foobar2000.h>
#include <libspotify/api.h>

#include "../helpers/dropdown_helper.h"
#include <functional>
#include <shlobj.h>

#include <stdint.h>
#include <stdlib.h>

extern "C" {
	extern const uint8_t g_appkey[];
	extern const size_t g_appkey_size;
}

volatile bool failed = false;

struct Gentry {
	void *data;
    size_t size;
	int sampleRate;
	int channels;
};

struct CriticalSection {
	CRITICAL_SECTION cs;
	CriticalSection() {
		InitializeCriticalSection(&cs);
	}

	~CriticalSection() {
		DeleteCriticalSection(&cs);
	}
};

struct LockedCS {
	CRITICAL_SECTION &cs;
	LockedCS(CriticalSection &o) : cs(o.cs) {
		EnterCriticalSection(&cs);
	}

	~LockedCS() {
		LeaveCriticalSection(&cs);
	}
};

struct SpotifyThreadData {
	SpotifyThreadData(CriticalSection &cs) : cs(cs) {
	}

	HANDLE processEventsEvent;
	CriticalSection &cs;
	sp_session *sess;
};

DWORD WINAPI spotifyThread(void *data) {
	SpotifyThreadData *dat = (SpotifyThreadData*)data;

	int nextTimeout = INFINITE;
	while (true) {
		WaitForSingleObject(dat->processEventsEvent, nextTimeout);
		LockedCS lock(dat->cs);
		sp_session_process_events(dat->sess, &nextTimeout);
	}
}

struct Buffer {

	size_t entries;
	size_t ptr;

	static const size_t MAX_ENTRIES = 255;

	Gentry *entry[MAX_ENTRIES];

	CONDITION_VARIABLE bufferNotEmpty;
	CriticalSection bufferLock;

	Buffer() : entries(0), ptr(0) {
		InitializeConditionVariable(&bufferNotEmpty);
	}

	~Buffer() {
		flush();
	}

	void add(void *data, size_t size, int sampleRate, int channels) {
		Gentry *e = new Gentry;
		e->data = data;
		e->size = size;
		e->sampleRate = sampleRate;
		e->channels = channels;

		{
			LockedCS lock(bufferLock);
			entry[(ptr + entries) % MAX_ENTRIES] = e;
			++entries;
		}
		WakeConditionVariable(&bufferNotEmpty);
	}

	bool isFull() {
		return entries >= MAX_ENTRIES;
	}

	void flush() {
		while (entries > 0)
			free(take());
	}

	Gentry *take() {
		LockedCS lock(bufferLock);
		while (entries == 0) {
			SleepConditionVariableCS(&bufferNotEmpty, &bufferLock.cs, INFINITE);
		}

		Gentry *e = entry[ptr++];
		--entries;
		if (MAX_ENTRIES == ptr)
			ptr = 0;

		return e;
	}

	void free(Gentry *e) {
		delete[] e->data;
		delete e;
	}
};

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

class SpotifySession {
	INIT_ONCE initOnce;
	sp_session *sp;
	HANDLE loggedInEvent;
	SpotifyThreadData threadData;
	CriticalSection spotifyCS;
	HANDLE processEventsEvent;
public:
	Buffer buf;

	SpotifySession() :
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

	~SpotifySession() {
		CloseHandle(loggedInEvent);
		CloseHandle(processEventsEvent);
	}

	sp_session *getAnyway() {
		return sp;
	}

	sp_session *get() {
		InitOnceExecuteOnce(&initOnce,
			makeSpotifySession,
			NULL,
			NULL);
		return getAnyway();
	}

	CriticalSection &getSpotifyCS() {
		return spotifyCS;
	}

	void waitForLogin() {
		WaitForSingleObject(loggedInEvent, INFINITE);
	}

	void loggedIn(sp_error err) {
		if (SP_ERROR_OK != err) {
			LockedCS lock(spotifyCS);
			pfc::string8 s = "Logging-in went wrong.  This is not recoverable.  Please restart";
			s += sp_error_message(err);
			console::error(s);
		}
		SetEvent(loggedInEvent);
	}

	void processEvents() {
		SetEvent(processEventsEvent);
	}
} ss;

BOOL CALLBACK makeSpotifySession(PINIT_ONCE initOnce, PVOID param, PVOID *context) {
	pfc::string8 username;
	spotifyUsername.get(username);

	pfc::string8 password;
	spotifyPassword.get(password);

	{
		sp_session *sess = ss.getAnyway();
		LockedCS lock(ss.getSpotifyCS());
		sp_session_login(sess, username.get_ptr(), password.get_ptr());
	}
	ss.waitForLogin();
	return TRUE;
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
	ss.loggedIn(error);
}

void CALLBACK notify_main_thread(sp_session *sess)
{
    ss.processEvents();
}

int CALLBACK music_delivery(sp_session *sess, const sp_audioformat *format,
                          const void *frames, int num_frames)
{
	if (num_frames == 0) {
		ss.buf.flush();
        return 0;
	}

	if (ss.buf.isFull()) {
		return 0;
	}

	const size_t s = num_frames * sizeof(int16_t) * format->channels;

	void *data = new char[s];
	memcpy(data, frames, s);

	ss.buf.add(data, s, format->sample_rate, format->channels);

	return num_frames;
}

void CALLBACK end_of_track(sp_session *sess)
{
	ss.buf.add(NULL, 0, 0, 0);
}

void CALLBACK play_token_lost(sp_session *sess)
{
	failed = true;
}

class InputSpotify
{
	t_filestats m_stats;

	std::string url;
	sp_track *t;

	int channels;
	int sampleRate;

public:

	void open( service_ptr_t<file> m_file, const char * p_path, t_input_open_reason p_reason, abort_callback & p_abort )
	{
		if ( p_reason == input_open_info_write ) throw exception_io_data();
		url = p_path;

		ss.get();

		{
			LockedCS lock(ss.getSpotifyCS());

			sp_link *link = sp_link_create_from_string(p_path);
			if (NULL == link)
				throw exception_io_data("couldn't parse url");

			t = sp_link_as_track(link);
			if (NULL == t)
				throw exception_io_data("url not a track");
		}

		while (true) {
			{
				LockedCS lock(ss.getSpotifyCS());

				const sp_error e = sp_track_error(t);
				if (SP_ERROR_OK == e)
					break;
				if (SP_ERROR_IS_LOADING != e)
					throw exception_io_data(sp_error_message(e));
			}

			Sleep(50);
			p_abort.check();
		}
	}

	void get_info( file_info & p_info, abort_callback & p_abort )
	{
		LockedCS lock(ss.getSpotifyCS());
		p_info.set_length(sp_track_duration(t)/1000.0);
		p_info.meta_add("ARTIST", sp_artist_name(sp_track_artist(t, 0)));
		p_info.meta_add("ALBUM", sp_album_name(sp_track_album(t)));
		p_info.meta_add("TITLE", sp_track_name(t));
		p_info.meta_add("URL", url.c_str());
	}

	t_filestats get_file_stats( abort_callback & p_abort )
	{
		return m_stats;
	}

	void decode_initialize( unsigned p_flags, abort_callback & p_abort )
	{
		ss.buf.flush();
		sp_session *sess = ss.get();

		LockedCS lock(ss.getSpotifyCS());
		sp_session_player_load(sess, t);
		sp_session_player_play(sess, 1);
	}

	bool decode_run( audio_chunk & p_chunk, abort_callback & p_abort )
	{
		if (failed)
			throw exception_io_data("failed");

		Gentry *e = ss.buf.take();

		if (NULL == e->data) {
			ss.buf.free(e);
			return false;
		}

		p_chunk.set_data_fixedpoint(
			e->data,
			e->size,
			e->sampleRate,
			e->channels,
			16,
			audio_chunk::channel_config_stereo);

		channels = e->channels;
		sampleRate = e->sampleRate;

		ss.buf.free(e);

		return true;
	}

	void decode_seek( double p_seconds,abort_callback & p_abort )
	{
		ss.buf.flush();
		sp_session *sess = ss.get();
		LockedCS lock(ss.getSpotifyCS());
		sp_session_player_seek(sess, p_seconds*1000);
	}

	bool decode_can_seek()
	{
		return true;
	}

	bool decode_get_dynamic_info( file_info & p_out, double & p_timestamp_delta )
	{
		p_out.info_set_int("CHANNELS", channels);
		p_out.info_set_int("SAMPLERATE", sampleRate);
		return true;
	}

	bool decode_get_dynamic_info_track( file_info & p_out, double & p_timestamp_delta )
	{
		return false;
	}

	void decode_on_idle( abort_callback & p_abort ) { }

	void retag( const file_info & p_info, abort_callback & p_abort )
	{
		throw exception_io_data();
	}

	static bool g_is_our_content_type( const char * p_content_type )
	{
		return false;
	}

	static bool g_is_our_path( const char * p_full_path, const char * p_extension )
	{
		return !strncmp( p_full_path, "spotify:", strlen("spotify:") );
	}
};

static input_singletrack_factory_t< InputSpotify > inputFactorySpotify;

DECLARE_COMPONENT_VERSION("Spotify Decoder", MYVERSION, "Support for spotify: urls.");
