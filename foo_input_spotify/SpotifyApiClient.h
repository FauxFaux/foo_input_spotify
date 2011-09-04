#include "SpotifyApi.h"
#include <memory>
#include <sstream>

class SpotifyApiClient : SpotifyApi {
	typedef std::auto_ptr<Pipe> pp;
	pp toChild;
	pp fromChild;
	PipeOut to;
	PipeIn from;

	Buffer buf;

	HANDLE child;

public:
	SpotifyApiClient() : child(NULL), 
			toChild(pp(new Pipe())),
			fromChild(pp(new Pipe())),
			to(PipeOut(toChild->write)),
			from(PipeIn(fromChild->read)) {
		std::wstringstream ss(L"spotify_depooper.exe ");
		ss << toChild->read << " " << fromChild->write;
		STARTUPINFO si = {};
		si.cb = sizeof(STARTUPINFO);
		PROCESS_INFORMATION pi = {};
		LPWSTR arg = _wcsdup(ss.str().c_str());
		
		if (FAILED(CreateProcess(NULL, arg, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))) {
			::free(arg);
			throw std::exception("couldn't create child process");
		}
		::free(arg);

		CloseHandle(si.hStdInput);
		CloseHandle(si.hStdOutput);
		CloseHandle(si.hStdError);
		CloseHandle(pi.hThread);
		child = pi.hProcess;
	}

	~SpotifyApiClient() {
		if (child) {
			TerminateProcess(child, 1);
		}
	}

	virtual void load(std::string url) {
		to.sync().operation("load").arg(url);
		from.sync().checkReturn();
	}

	virtual void freeTracks() {
		to.sync().operation("frtr");
		from.sync().checkReturn();
	}

	virtual void initialise(int subsong) {
		to.sync().operation("init").arg(subsong);
		from.sync().checkReturn();
	}

	virtual uint32_t currentSubsongCount() {
		to.sync().operation("ssct");
		return from.sync().returnUint32_t();
	}

	virtual Gentry *take() {
		to.sync().operation("ssct");
		return from.sync().returnGentry();
	}

	virtual void free(Gentry *entry) {
		delete[] entry->data;
		delete entry;
	}
};
