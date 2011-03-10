#define MYVERSION "0.1"

#define _WIN32_WINNT 0x0600

#include <foobar2000.h>
#include <libspotify/api.h>

#include "../helpers/dropdown_helper.h"
#include <functional>
#include <libspotify/api.h>

#include <stdint.h>
#include <stdlib.h>

extern "C" {
	extern const uint8_t g_appkey[];
	extern const size_t g_appkey_size;
}

static sp_session_callbacks session_callbacks = {};
static sp_session_config spconfig = {};
sp_error err;

sp_playlist *g_jukeboxlist = NULL;
sp_track *g_currenttrack = NULL;

volatile bool doNotify = false;
volatile bool playbackDone = false;
volatile bool failed = false;

struct Gentry {
	void *data;
    size_t size;
	int sampleRate;
	int channels;
};

size_t entries = 0;
size_t ptr = 0;

const size_t MAX_ENTRIES = 255;

Gentry *entry[MAX_ENTRIES];

CONDITION_VARIABLE bufferNotEmpty;
CRITICAL_SECTION   bufferLock;

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
public:

	SpotifySession() : loggedInEvent(CreateEvent(NULL, TRUE, FALSE, NULL)) {
		memset(&initOnce, 0, sizeof(INIT_ONCE));

		spconfig.api_version = SPOTIFY_API_VERSION,
		spconfig.cache_location = "tmp";
		spconfig.settings_location = "tmp";
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

		sp_error err = sp_session_create(&spconfig, &sp);

		if (SP_ERROR_OK != err) {
			throw new pfc::exception("Couldn't create spotify session");
		}
	}

	~SpotifySession() {
		CloseHandle(loggedInEvent);
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

	void waitForLogin() {
		while (WAIT_OBJECT_0 != WaitForSingleObject(loggedInEvent, 100))
			notifyStuff();
	}

	void notifyStuff() {
		if (doNotify) {
			doNotify = false;
			int next_timeout;
			do {
				sp_session_process_events(getAnyway(), &next_timeout);
			} while (next_timeout == 0);
		}
	}

	void loggedIn(sp_error err) {
		// XXX err
		SetEvent(loggedInEvent);
	}
} ss;

BOOL CALLBACK makeSpotifySession(PINIT_ONCE initOnce, PVOID param, PVOID *context) {
	pfc::string8 username;
	spotifyUsername.get(username);

	pfc::string8 password;
	spotifyPassword.get(password);

	sp_session_login(ss.getAnyway(), username.get_ptr(), password.get_ptr());
	ss.waitForLogin();
	return TRUE;
}

SpotifySession *getSession(void *param) {
	return static_cast<SpotifySession *>(sp_session_userdata(static_cast<sp_session*>(param)));
}

void CALLBACK log_message(sp_session *sess, const char *error) {
	printf("%s\n", error);
}

void CALLBACK message_to_user(sp_session *sess, const char *error) {
	printf("%s\n", error);
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
    doNotify = true;
}

int CALLBACK music_delivery(sp_session *sess, const sp_audioformat *format,
                          const void *frames, int num_frames)
{
    if (num_frames == 0)
        return 0; // Audio discontinuity, do nothing

	EnterCriticalSection(&bufferLock);

	if (entries >= MAX_ENTRIES) {
		LeaveCriticalSection(&bufferLock);
		return 0;
	}

	const size_t s = num_frames * sizeof(int16_t) * format->channels;

	Gentry *e = new Gentry;
	e->data = new char[s];
	e->size = s;
	e->sampleRate = format->sample_rate;
	e->channels = format->channels;

	memcpy(e->data, frames, s);

	entry[(ptr + entries) % MAX_ENTRIES] = e;
	++entries;

	LeaveCriticalSection(&bufferLock);
	WakeConditionVariable(&bufferNotEmpty);

	return num_frames;
}


void CALLBACK end_of_track(sp_session *sess)
{
	playbackDone = true;
}

void CALLBACK play_token_lost(sp_session *sess)
{
	failed = true;
}

class input_kdm
{
	t_filestats m_stats;

	std::string name;
	std::string artist;
	std::string album;
	std::string url;
	double length;
	sp_track *t;

public:
	input_kdm()
	{
		InitializeConditionVariable (&bufferNotEmpty);
		InitializeCriticalSection (&bufferLock);
	}

	~input_kdm()
	{
	}

	void open( service_ptr_t<file> m_file, const char * p_path, t_input_open_reason p_reason, abort_callback & p_abort )
	{
		if ( p_reason == input_open_info_write ) throw exception_io_data();
		url = p_path;

		ss.get();

		sp_link *link = sp_link_create_from_string(p_path);
		if (NULL == link)
			throw exception_io_data("couldn't parse url");

		t = sp_link_as_track(link);
		if (NULL == t)
			throw exception_io_data("url not a track");

		while (true) {
			const sp_error e = sp_track_error(t);
			if (SP_ERROR_OK == e)
				break;
			if (SP_ERROR_IS_LOADING != e)
				throw exception_io_data(sp_error_message(e));

			Sleep(50);
			ss.notifyStuff();
			p_abort.check();
		}
	}

	void get_info( file_info & p_info, abort_callback & p_abort )
	{
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
		sp_session_player_load(ss.get(), t);
		sp_session_player_play(ss.get(), 1);
	}

	bool decode_run( audio_chunk & p_chunk, abort_callback & p_abort )
	{
		if (failed)
			throw exception_io_data("failed");

		if (playbackDone)
			return false;

		ss.notifyStuff();

		EnterCriticalSection(&bufferLock);

		while (entries == 0) {
			SleepConditionVariableCS(&bufferNotEmpty, &bufferLock, 100);
			LeaveCriticalSection(&bufferLock);
			ss.notifyStuff();
			EnterCriticalSection(&bufferLock);
		}


		Gentry *e = entry[ptr++];

		--entries;

		if (MAX_ENTRIES == ptr)
			ptr = 0;

		p_chunk.set_data_fixedpoint(
			e->data,
			e->size,
			e->sampleRate,
			e->channels,
			16,
			audio_chunk::channel_config_stereo);

		delete[] e->data;
		delete e;

		LeaveCriticalSection(&bufferLock);

		return true;
	}

	void decode_seek( double p_seconds,abort_callback & p_abort )
	{
		long seek_ms = audio_math::time_to_samples( p_seconds, 1000 );

		//m_player->seek( seek_ms );

		//first_block = true;
	}

	bool decode_can_seek()
	{
		return false;
	}

	bool decode_get_dynamic_info( file_info & p_out, double & p_timestamp_delta )
	{
		//if ( first_block )
		//{
		//	first_block = false;
		//	p_out.info_set_int( "samplerate", srate );
		//	return true;
		//}
		return false;
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

static input_singletrack_factory_t< input_kdm >             g_input_factory_kdm;

DECLARE_COMPONENT_VERSION("Spotify Decoder", MYVERSION, "Support for spotify: urls.");
