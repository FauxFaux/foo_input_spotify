#include "../foo_input_spotify/SpotifyApi.h"
#include "../foo_input_spotify/util.h"

#include <vector>
#include <string>
#include "SpotifySession.h"


struct SpotifyApiImpl : SpotifyApi {
	virtual void load(std::string url) ;
	virtual void freeTracks();
	virtual void initialise(int subsong);
	virtual uint32_t currentSubsongCount();
	virtual Gentry *take();
	virtual void free(Gentry *entry);

	SpotifyApiImpl(stringfunc_t username, stringfunc_t password) :
		ss(username, password) {
	}

private:
	SpotifySession ss;

	std::vector<sp_track *> t;
	typedef std::vector<sp_track *>::iterator tr_iter;

	std::string url;

	int channels;
	int sampleRate;

	Buffer buf;
};
