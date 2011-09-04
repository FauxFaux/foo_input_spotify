#include "SpotifyApi.h"
#include <memory>
#include <sstream>
#include <foobar2000.h>

struct FeedbackThreadData {
	FeedbackThreadData(PipeOut to, PipeIn from)	:
		to(to), from(from) {
	}

	PipeOut to;
	PipeIn from;
};

DWORD WINAPI feedbackThread(void *data) {
	FeedbackThreadData *d = static_cast<FeedbackThreadData*>(data);
	while (true) {
		const std::string cmd = d->from.takeCommand();
		if ("stop" == cmd) {
			return 0;
		} else {
			console::formatter() << "invalid command: " << cmd.c_str();
		}
	}
}


class SpotifyApiClient : SpotifyApi {
	typedef std::auto_ptr<Pipe> pp;
	pp toChild;
	pp fromChild;
	pp feedbackToChild;
	pp feedbackFromChild;

	PipeOut to;
	PipeIn from;

	PipeOut feedTo;
	PipeIn feedFrom;

	HANDLE child;
	HANDLE thread;

	FeedbackThreadData threadData;

public:
	SpotifyApiClient() : child(NULL), 
			toChild(pp(new Pipe())),
			fromChild(pp(new Pipe())),
			feedbackToChild(pp(new Pipe())),
			feedbackFromChild(pp(new Pipe())),
			to(PipeOut(toChild->write)),
			from(PipeIn(fromChild->read)),
			feedTo(PipeOut(feedbackToChild->write)),
			feedFrom(PipeIn(feedbackFromChild->read)),
			threadData(feedTo, feedFrom) {
		std::wstringstream ss(L"spotify_depooper.exe ");
		ss << toChild->read << " " << fromChild->write << " " 
			<< feedbackToChild->read << " " << feedbackFromChild->write;
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

		threadData.to = feedTo;
		threadData.from = feedFrom;

		thread = CreateThread(NULL, 0, &feedbackThread, &threadData, 0, 0);
		if (NULL == thread) {
			throw std::exception("couldn't create feedback thread");
		}
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
