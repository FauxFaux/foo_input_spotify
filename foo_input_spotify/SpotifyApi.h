#include <stdint.h>
#include "util.h"

struct SpotifyApi {
	virtual void load(std::string url, nullary_t check = [](){}) = 0;
	virtual void freeTracks(nullary_t check = [](){}) = 0;
	virtual void initialise(int subsong, nullary_t check = [](){}) = 0;
	virtual uint32_t currentSubsongCount(nullary_t check = [](){}) = 0;
	virtual Gentry *take(nullary_t check = [](){}) = 0;
	virtual void free(Gentry *entry) = 0;
};
