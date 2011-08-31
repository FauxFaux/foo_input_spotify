#include "SpotifyApiImpl.h"

#include "SpotifySession.h"

#include <vector>

extern "C" {
	extern const uint8_t g_appkey[];
	extern const size_t g_appkey_size;
}

SpotifySession ss;

#define FOR_TRACKS() for (tr_iter it = t.begin(); it != t.end(); ++it)

std::vector<sp_track *> t;
typedef std::vector<sp_track *>::iterator tr_iter;

std::string url;

int channels;
int sampleRate;

Buffer buf;

void checkAborted() {
	// TODO
}

struct io_data_exception : std::exception {
	io_data_exception(const char *msg) : std::exception(msg) {
	}
};

void SpotifyApiImpl::freeTracks() {
	FOR_TRACKS()
		sp_track_release(*it);
	t.clear();
}


void SpotifyApiImpl::load(const char *p_path) {
	sp_session *sess = ss.get();

	{
		LockedCS lock(ss.getSpotifyCS());

		sp_link *link = sp_link_create_from_string(p_path);
		if (NULL == link)
			throw io_data_exception("couldn't parse url");

		freeTracks();

		sp_track *ptr = sp_link_as_track(link);

		// do we need to free link here, above or never?

		if (NULL == ptr) {
			sp_playlist *playlist = sp_playlist_create(sess, link);
			if (NULL == playlist)
				throw io_data_exception("Apparently not a track or a playlist");

			int count;
			// XXX I don't know what we're waiting for here; sp_playlist_num_tracks returning 0 is undocumented,
			// there's no sp_playlist_error() to return not ready..
			// foobar can't cope with an empty entry, anyway, so we have to get something or error out
			for (int retries = 0; retries < 50; ++retries) {
				count = sp_playlist_num_tracks(playlist);
				if (0 != count)
					break;

				lock.dropAndReacquire(100);
				checkAborted();
			}

			if (0 == count)
				throw io_data_exception("empty (or failed to load?) playlist");

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
		checkAborted();
	}
}

void SpotifyApiImpl::initialise(int subsong) {
	ss.buf.flush();
	sp_session *sess = ss.get();

	LockedCS lock(ss.getSpotifyCS());
	assertSucceeds("load track (including region check)", sp_session_player_load(sess, t.at(subsong)));
	sp_session_player_play(sess, 1);
}

uint32_t SpotifyApiImpl::currentSubsongCount() {
	return t.size();
}

Gentry *SpotifyApiImpl::take() {
	return NULL;
}

void SpotifyApiImpl::free(Gentry *entry) {

}
