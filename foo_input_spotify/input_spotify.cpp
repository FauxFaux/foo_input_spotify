#include "util.h"

#include <foobar2000.h>

#include "../helpers/dropdown_helper.h"
#include <functional>
#include <shlobj.h>

#include <stdint.h>
#include <stdlib.h>
#include <vector>

#include "SpotifyApi.h"

SpotifyApi api;

class InputSpotify
{
	int channels;
	int sampleRate;

	void freeTracks() {
		api.freeTracks();
	}

public:

	InputSpotify() {
	}

	~InputSpotify() {
		freeTracks();
	}

	void open( service_ptr_t<file> m_file, const char * p_path, t_input_open_reason p_reason, abort_callback & p_abort )
	{
		if ( p_reason == input_open_info_write ) throw exception_io_data();
		api.load(p_path);
	}

	void get_info(t_int32 subsong, file_info & p_info, abort_callback & p_abort )
	{
#ifdef DISABLED
		LockedCS lock(ss.getSpotifyCS());
		sp_track *tr = t.at(subsong);
		p_info.set_length(sp_track_duration(tr)/1000.0);
		p_info.meta_add("ARTIST", sp_artist_name(sp_track_artist(tr, 0)));
		p_info.meta_add("ALBUM", sp_album_name(sp_track_album(tr)));
		p_info.meta_add("TITLE", sp_track_name(tr));
		p_info.meta_add("URL", url.c_str());
#endif
	}

	t_filestats get_file_stats( abort_callback & p_abort )
	{
		t_filestats t = {};
		return t;
	}

	void decode_initialize(t_int32 subsong, unsigned p_flags, abort_callback & p_abort )
	{
		api.initialise(subsong);
	}

	bool decode_run( audio_chunk & p_chunk, abort_callback & p_abort )
	{
		Gentry *e = api.take();

		if (NULL == e->data) {
			api.free(e);
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

		api.free(e);

		return true;
	}

	void decode_seek( double p_seconds,abort_callback & p_abort )
	{
#ifdef DISABLED
		ss.buf.flush();
		sp_session *sess = ss.get();
		LockedCS lock(ss.getSpotifyCS());
		sp_session_player_seek(sess, static_cast<int>(p_seconds*1000));
#endif
	}

	bool decode_can_seek()
	{
		return false;
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

	void retag_set_info( t_int32 subsong, const file_info & p_info, abort_callback & p_abort )
	{
		throw exception_io_data();
	}

	void retag_commit( abort_callback & p_abort )
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

	t_uint32 get_subsong_count() {
		return api.currentSubsongCount();
	}

	t_uint32 get_subsong(t_uint32 song) {
		return song;
	}
};

static input_factory_t< InputSpotify > inputFactorySpotify;

DECLARE_COMPONENT_VERSION("Spotify Decoder", MYVERSION, "Support for spotify: urls.");
