#include "util.h"

#include <foobar2000.h>

#include "../helpers/dropdown_helper.h"
#include <functional>
#include <shlobj.h>

#include <stdint.h>
#include <stdlib.h>
#include <vector>

#include "SpotifySession.h"

SpotifySession ss;

sp_track *trackItemOrThrow(sp_track *first, int i) {
	while (first && i --> 0)
		first = first->next;
	if (NULL == first)
		throw pfc::exception("Requested track past end of tracks");
	return first;
}

struct DeOb : boost::noncopyable {
	virtual ~DeOb() {};
	virtual sp_track* at(int pos) = 0;
	virtual int size() = 0;
};

struct DeTrack : DeOb {
	sp_track *o;
	DeTrack(sp_track *o) : o(o) {
	}

	sp_track* at(int pos) {
		return o;
	}

	int size() {
		return 1;
	}

	virtual ~DeTrack() { 
		sp_track_release(o);
	}
};

struct DeAlbum : DeOb {
	sp_album *o;
	DeAlbum(sp_album *o) : o(o) {
	}

	sp_track* at(int pos) {
		return trackItemOrThrow(o->tracks, pos);
	}

	int size() {
		return o->num_tracks;
	}

	virtual ~DeAlbum() { 
		sp_album_release(o);
	}
};

struct DePlaylist : DeOb {
	sp_playlist *o;
	DePlaylist(sp_playlist *o) : o(o) {
	}

	sp_track* at(int pos) {
		return trackItemOrThrow(o->tracks, pos);
	}

	int size() {
		return o->num_tracks;
	}

	virtual ~DePlaylist() { 
		sp_playlist_release(o);
	}
};

class InputSpotify : boost::noncopyable
{
	t_filestats m_stats;

	std::string url;
	std::auto_ptr<DeOb> t;

	int channels;
	int sampleRate;
	bool firstPass;

public:

	InputSpotify() {
	}

	~InputSpotify() {
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

			switch (link->type) {
			case LINK_TYPE_TRACK: {
				sp_track *ptr = despotify_link_get_track(sess, link);
				if (NULL == ptr)
					throw exception_io_data("couldn't convert link to track");
				t = std::auto_ptr<DeOb>(new DeTrack(ptr));
				} break;
			case LINK_TYPE_ALBUM: {
				sp_album *ptr = despotify_link_get_album(sess, link);
				if (NULL == ptr)
					throw exception_io_data("couldn't convert link to album");
				t = std::auto_ptr<DeOb>(new DeAlbum(ptr));
				} break;
			case LINK_TYPE_PLAYLIST: {
				sp_playlist *ptr = despotify_link_get_playlist(sess, link);
				if (NULL == ptr)
					throw exception_io_data("couldn't convert link to playlist");
				t = std::auto_ptr<DeOb>(new DePlaylist(ptr));
				} break;
			default:
				throw exception_io_data("unsupported url type");
			}
		}
	}

	void get_info(t_int32 subsong, file_info & p_info, abort_callback & p_abort )
	{
		LockedCS lock(ss.getSpotifyCS());
		sp_track *tr = t->at(subsong);
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
		sp_session *sess = ss.get();

		LockedCS lock(ss.getSpotifyCS());
		DeOb *d = t.get();
		sp_track *r = d->at(subsong);
		assertSucceeds("track is playable in this region", sess, !r->playable);
		if (!despotify_play(sess, t->at(subsong), false))
			throw pfc::exception("Not playable wtf");
		firstPass = true;
	}

	bool decode_run( audio_chunk & p_chunk, abort_callback & p_abort )
	{
		pcm_data d = {};
		sp_session *sess = ss.get();
		do {
			int ret = despotify_get_pcm(sess, &d);
			if (NULL != sess->last_error)
				throw exception_io_data(sess->last_error);

			assertSucceeds("despotify_get_pcm", sess, ret);
			p_abort.check();
		} while (firstPass && 0 == d.len);

		// Minor hack.  despotify_get_pcm returns an empty e:
		// a) while it's waiting for itself to initialise
		// b) when you're at the end of a track
		// c) Randomly, at other times.  Oh good, now I have to panic.
		// i.e. if firstPass is set, retry, else end-of-track:

		if (0 == d.len)
			return false;

		firstPass = false;
		
		p_chunk.set_data_fixedpoint(
			d.buf,
			d.len,
			d.samplerate,
			d.channels,
			16,
			audio_chunk::channel_config_stereo);

		channels = d.channels;
		sampleRate = d.samplerate;
		return true;
	}

	void decode_seek( double p_seconds,abort_callback & p_abort )
	{
		throw exception_io_data("I /said/, seek isn't supported");
	}

	bool decode_can_seek()
	{
		return false;
	}

	bool decode_get_dynamic_info( file_info & p_out, double & p_timestamp_delta )
	{
		p_out.info_set_int("CHANNELS", channels);
		p_out.info_set_int("SAMPLERATE", sampleRate);
		p_out.info_set_bitrate(despotify_get_current_track(ss.get())->file_bitrate/1000);
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
		return t->size();
	}

	t_uint32 get_subsong(t_uint32 song) {
		return song;
	}
};

static input_factory_t< InputSpotify > inputFactorySpotify;

DECLARE_COMPONENT_VERSION("Spotify Decoder", MYVERSION, "Support for spotify: urls.");
