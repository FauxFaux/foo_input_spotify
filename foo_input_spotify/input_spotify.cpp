#define MYVERSION "0.1"

#define _WIN32_WINNT 0x0600

#include <foobar2000.h>

#include "../helpers/dropdown_helper.h"
#include <functional>
#include <shlobj.h>

#include <stdint.h>
#include <stdlib.h>

#include "SpotifySession.h"

extern "C" {
	extern const uint8_t g_appkey[];
	extern const size_t g_appkey_size;
}

SpotifySession ss;

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
					assertSucceeds("preloading track", e);
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
		assertSucceeds("load track", sp_session_player_load(sess, t));
		sp_session_player_play(sess, 1);
	}

	bool decode_run( audio_chunk & p_chunk, abort_callback & p_abort )
	{
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
		sp_session_player_seek(sess, static_cast<int>(p_seconds*1000));
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
