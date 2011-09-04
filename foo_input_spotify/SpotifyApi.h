#include <stdint.h>
#include "util.h"

struct SpotifyApi {
	virtual void load(std::string url) = 0;
	virtual void freeTracks() = 0;
	virtual void initialise(int subsong) = 0;
	virtual uint32_t currentSubsongCount() = 0;
	virtual Gentry *take() = 0;
	virtual void free(Gentry *entry) = 0;
};
