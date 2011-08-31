#include <stdint.h>
#include "util.h"

struct SpotifyApi {
	void load(const char *url);
	void freeTracks();
	void initialise(int subsong);
	uint32_t currentSubsongCount();
	Gentry *take();
	void free(Gentry *entry);
};
