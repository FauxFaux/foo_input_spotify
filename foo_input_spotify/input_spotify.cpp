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


int g_notify_do;
int g_playback_done;
sp_session *g_sess;
sp_playlist *g_jukeboxlist;
sp_track *g_currenttrack;
int g_track_index;

volatile bool doNotify = false;
volatile bool playbackDone = false;
volatile bool failed = false;


volatile bool filled = false;
CONDITION_VARIABLE bufferNotEmpty;
CONDITION_VARIABLE bufferNotFull;
CRITICAL_SECTION   bufferLock;

pfc::array_t< t_int16 > sample_buffer;
volatile int sampleRate = 0;
volatile int channels = 0;

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

	if (filled) {
		//SleepConditionVariableCS (&bufferNotFull, &bufferLock, INFINITE);
		LeaveCriticalSection(&bufferLock);
		return 0;
	}

	const size_t s = num_frames * sizeof(int16_t) * format->channels;

	sample_buffer.grow_size(s);

	memcpy(sample_buffer.get_ptr(), frames, s);

	sample_buffer.set_count(num_frames);

	sampleRate = format->sample_rate;
	channels = format->channels;

	WakeConditionVariable(&bufferNotEmpty);
    LeaveCriticalSection(&bufferLock);

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


class input_kdm
{
	t_filestats m_stats;

public:

	input_kdm()
	{
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

		err = sp_session_create(&spconfig, &sp);

		if (SP_ERROR_OK != err) {
			throw new pfc::exception("Couldn't create spotify session");
		}
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
		//loop_count = cfg_loop_count;
		//if ( p_flags & input_flag_no_looping && !loop_count ) loop_count++;

		//m_player->musicoff();
		//m_player->musicon();

		//first_block = true;
		//eof = false;
	}

	bool decode_run( audio_chunk & p_chunk, abort_callback & p_abort )
	{
		if (failed)
			throw exception_io_data("failed");

		if (playbackDone)
			return false;

		{
			int next_timeout;
			do {
				sp_session_process_events(sp, &next_timeout);
			} while (next_timeout == 0);
		}

		//if ( eof ) return false;

		//unsigned samples_to_do = srate / 120;

		//sample_buffer.grow_size( samples_to_do * 2 );

		//long repeatcount = m_player->rendersound( sample_buffer.get_ptr(), samples_to_do * 4 );

		//p_chunk.set_data_fixedpoint( sample_buffer.get_ptr(), samples_to_do * 4, srate, 2, 16, audio_chunk::channel_config_stereo );

		//if ( loop_count && repeatcount >= loop_count ) eof = true;

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
