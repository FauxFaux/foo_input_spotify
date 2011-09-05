#include "../foo_input_spotify/SpotifyApi.h"
#include "../foo_input_spotify/util.h"

#include <vector>
#include <string>
#include "SpotifySession.h"


struct SpotifyApiImpl : SpotifyApi {
	virtual void load(std::string url, nullary_t check = [](){}) ;
	virtual void freeTracks(nullary_t check = [](){});
	virtual void initialise(int subsong, nullary_t check = [](){});
	virtual uint32_t currentSubsongCount(nullary_t check = [](){});
	virtual Gentry *take(nullary_t check = [](){});
	virtual void free(Gentry *entry);

	SpotifyApiImpl(stringfunc_t username, stringfunc_t password, funcstr_t warn) :
		ss(username, password, warn) {
	}

private:
	SpotifySession ss;

	std::vector<sp_track *> t;
	typedef std::vector<sp_track *>::iterator tr_iter;

	std::string url;

	int channels;
	int sampleRate;
};
