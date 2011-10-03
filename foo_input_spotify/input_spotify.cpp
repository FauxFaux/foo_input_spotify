#include "util.h"

#include <foobar2000.h>

#include "../helpers/dropdown_helper.h"
#include <functional>
#include <shlobj.h>

#include <stdint.h>
#include <stdlib.h>
#include <vector>

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
	std::vector<sp_track *> t;
	typedef std::vector<sp_track *>::iterator tr_iter;

	int channels;
	int sampleRate;

#define FOR_TRACKS() for (tr_iter it = t.begin(); it != t.end(); ++it)

	void freeTracks() {
		FOR_TRACKS()
			sp_track_release(*it);
		t.clear();
	}

public:

	InputSpotify() {
	}

	~InputSpotify() {
		freeTracks();
		ss.releaseDecoder(this);
	}

	void open( service_ptr_t<file> m_file, const char * p_path, t_input_open_reason p_reason, abort_callback & p_abort )
	{
		if ( p_reason == input_open_info_write ) throw exception_io_data();
		url = p_path;

		sp_session *sess = ss.get();

		{
			LockedCS lock(ss.getSpotifyCS());

			sp_link *link = sp_link_create_from_string(p_path);
			if (NULL == link)
				throw exception_io_data("couldn't parse url");

			freeTracks();

			sp_track *ptr = sp_link_as_track(link);

			// do we need to free link here, above or never?

			if (NULL == ptr) {
				sp_playlist *playlist = sp_playlist_create(sess, link);
				if (NULL == playlist)
					throw exception_io_data("Apparently not a track or a playlist");

				int count;
				// XXX I don't know what we're waiting for here; sp_playlist_num_tracks returning 0 is undocumented,
				// there's no sp_playlist_error() to return not ready..
				// foobar can't cope with an empty entry, anyway, so we have to get something or error out
				for (int retries = 0; retries < 50; ++retries) {
					count = sp_playlist_num_tracks(playlist);
					if (0 != count)
						break;

					lock.dropAndReacquire(100);
					p_abort.check();
				}

				if (0 == count)
					throw exception_io_data("empty (or failed to load?) playlist");

				for (int i = 0; i < count; ++i) {
					sp_track *track = sp_playlist_track(playlist, i);
					sp_track_add_ref(track); // or the playlist will free it
					t.push_back(track);
				}
				sp_playlist_release(playlist);
			} else
				t.push_back(ptr);
		}

		while (true) {
			{
				LockedCS lock(ss.getSpotifyCS());
				size_t done = 0;
				FOR_TRACKS() {
					const sp_error e = sp_track_error(*it);
					if (SP_ERROR_OK == e)
						++done;
					else if (SP_ERROR_IS_LOADING != e)
						assertSucceeds("preloading track", e);
				}

				if (done == t.size())
					break;
			}

			Sleep(50);
			p_abort.check();
		}
	}

	void get_info(t_int32 subsong, file_info & p_info, abort_callback & p_abort )
	{
		LockedCS lock(ss.getSpotifyCS());
		sp_track *tr = t.at(subsong);
		p_info.set_length(sp_track_duration(tr)/1000.0);
		p_info.meta_add("ARTIST", sp_artist_name(sp_track_artist(tr, 0)));
		p_info.meta_add("ALBUM", sp_album_name(sp_track_album(tr)));
		p_info.meta_add("TITLE", sp_track_name(tr));
		p_info.meta_add("URL", url.c_str());
	}

	t_filestats get_file_stats( abort_callback & p_abort )
	{
		return m_stats;
	}

	void decode_initialize(t_int32 subsong, unsigned p_flags, abort_callback & p_abort )
	{
		ss.takeDecoder(this);

		ss.buf.flush();
		sp_session *sess = ss.get();

		LockedCS lock(ss.getSpotifyCS());
		assertSucceeds("load track (including region check)", sp_session_player_load(sess, t.at(subsong)));
		sp_session_player_play(sess, 1);
	}

	bool decode_run( audio_chunk & p_chunk, abort_callback & p_abort )
	{
		ss.ensureDecoder(this);

		Gentry *e = ss.buf.take();

		if (NULL == e->data) {
			ss.buf.free(e);
			ss.releaseDecoder(this);
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
		ss.ensureDecoder(this);

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
		return t.size();
	}

	t_uint32 get_subsong(t_uint32 song) {
		return song;
	}
};

static input_factory_t< InputSpotify > inputFactorySpotify;

DECLARE_COMPONENT_VERSION("Spotify Decoder", MYVERSION, "Support for spotify: urls.");
