#define MYVERSION "0.1"

/*
	changelog

2010-06-20 07:05 UTC - kode54
- Initial release
- Version is now 1.0

*/

#define _WIN32_WINNT 0x0501
#include <functional>

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
sp_session *sp;
sp_error err;


sp_session *g_sess = NULL;
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

void __stdcall log_message(sp_session *sess, const char *error) {
	printf("%s\n", error);
}

void __stdcall message_to_user(sp_session *sess, const char *error) {
	printf("%s\n", error);
}

void __stdcall start_playback(sp_session *sess) {
	return;
}

void __stdcall logged_in(sp_session *sess, sp_error error)
{
	if (SP_ERROR_OK != error) {
		failed = true;
    }

	g_sess = sess;
}

void __stdcall notify_main_thread(sp_session *sess)
{
    doNotify = true;
}

int __stdcall music_delivery(sp_session *sess, const sp_audioformat *format,
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


void __stdcall end_of_track(sp_session *sess)
{
	playbackDone = true;
}

void __stdcall play_token_lost(sp_session *sess)
{
	failed = true;
}

void notifyStuff() {
	if (doNotify) {
		int next_timeout;
		do {
			sp_session_process_events(sp, &next_timeout);
		} while (next_timeout == 0);
		doNotify = false;
	}
}

class input_kdm
{
	t_filestats m_stats;

public:

	input_kdm()
	{
		InitializeConditionVariable (&bufferNotEmpty);
		InitializeCriticalSection (&bufferLock);

		spconfig.api_version = SPOTIFY_API_VERSION,
		spconfig.cache_location = "tmp";
		spconfig.settings_location = "tmp";
		spconfig.application_key = g_appkey;
		spconfig.application_key_size = g_appkey_size;
		spconfig.user_agent = "spotify-foobar2000-faux-" MYVERSION;
		spconfig.callbacks = &session_callbacks;

		session_callbacks.logged_in = &logged_in;
		session_callbacks.notify_main_thread = &notify_main_thread;
		session_callbacks.music_delivery = &music_delivery;
		session_callbacks.play_token_lost = &play_token_lost;
		session_callbacks.end_of_track = &end_of_track;
		session_callbacks.log_message = &log_message;
		session_callbacks.message_to_user = &message_to_user;
		session_callbacks.start_playback = &start_playback;
		
		err = sp_session_create(&spconfig, &sp);

		if (SP_ERROR_OK != err) {
			throw new pfc::exception("Couldn't create spotify session");
		}
		
		sp_session_login(sp, "fauxfaux", "penises");
	}

	~input_kdm()
	{
	}

	void open( service_ptr_t<file> m_file, const char * p_path, t_input_open_reason p_reason, abort_callback & p_abort )
	{
		if ( p_reason == input_open_info_write ) throw exception_io_data();
	}

	void get_info( file_info & p_info, abort_callback & p_abort )
	{
		p_info.set_length(1337);
	}

	t_filestats get_file_stats( abort_callback & p_abort )
	{
		return m_stats;
	}

	void decode_initialize( unsigned p_flags, abort_callback & p_abort )
	{
		while (!g_sess) {
			notifyStuff();
			Sleep(500);
		}
		sp_link *link = sp_link_create_from_string("spotify:track:3ZDWOVWbiFBXR175n2x7xU");
		sp_track *t = sp_link_as_track(link);
		while (SP_ERROR_OK != sp_track_error(t)) {
			Sleep(500);
			notifyStuff();
		}
		const char *name = sp_track_name(t);
		printf("jukebox: Now playing \"%s\"...\n", name);
		sp_session_player_load(g_sess, t);
		sp_session_player_play(g_sess, 1);
	}

	bool decode_run( audio_chunk & p_chunk, abort_callback & p_abort )
	{
		if (failed)
			throw exception_io_data("failed");

		if (playbackDone)
			return false;

		notifyStuff();

		EnterCriticalSection(&bufferLock);

		while (entries == 0) {
			SleepConditionVariableCS(&bufferNotEmpty, &bufferLock, 100);
			LeaveCriticalSection(&bufferLock);
			notifyStuff();
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
