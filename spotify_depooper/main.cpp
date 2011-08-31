#include "SpotifyApiImpl.h"
#include <wininet.h>

int main() {
	InternetOpen(L"lol", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);

	SpotifyApiImpl impl([](){return "a";}, [](){return "a";});
	impl.load("lol");
}
