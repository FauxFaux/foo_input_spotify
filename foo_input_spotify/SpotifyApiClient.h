#include "SpotifyApi.h"
#include <memory>
#include <sstream>
#include <foobar2000.h>

struct FeedbackThreadData {
	FeedbackThreadData(PipeOut to, PipeIn from, stringfunc_t username, stringfunc_t password) :
		to(to), from(from), username(username), password(password) {
	}

	PipeOut to;
	PipeIn from;

	stringfunc_t username, password;
};

DWORD WINAPI feedbackThread(void *data) {
	FeedbackThreadData *d = static_cast<FeedbackThreadData*>(data);
	while (true) {
		const std::string cmd = d->from.takeCommand();
		if ("stop" == cmd) {
			return 0;
		} else if ("sync" == cmd) {
		} else if ("user" == cmd) {
			d->to.arg(d->username());
		} else if ("pass" == cmd) {
			d->to.arg(d->password());
		} else if ("excp" == cmd) {
			console::formatter() << "bang! " << d->from.takeString().c_str();
		} else if ("warn" == cmd) {
			console::formatter() << "log " << d->from.takeString().c_str();
		} else {
			console::formatter() << "invalid command: " << cmd.c_str();
		}
	}
}


class SpotifyApiClient : SpotifyApi {
	typedef std::auto_ptr<Pipe> pp;

	std::function<void()> defaultCheck;

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
	SpotifyApiClient(stringfunc_t username, stringfunc_t password) :
			child(NULL), thread(NULL),
			defaultCheck([&](){
				if (WAIT_OBJECT_0 != WaitForSingleObject(child, 0))
					throw std::exception("child went away");
				if (WAIT_OBJECT_0 != WaitForSingleObject(thread, 0))
					throw std::exception("thread went away");
			}),
			toChild(pp(new Pipe())),
			fromChild(pp(new Pipe())),
			feedbackToChild(pp(new Pipe())),
			feedbackFromChild(pp(new Pipe())),
			to(PipeOut(toChild->write)),
			from(PipeIn(fromChild->read, defaultCheck)),
			feedTo(PipeOut(feedbackToChild->write)),
			feedFrom(PipeIn(feedbackFromChild->read, defaultCheck)),
			threadData(feedTo, feedFrom, username, password) {
		std::wstringstream ss; ss << L"user-components\\foo_input_spotify\\spotify_depooper.exe ";
		ss << toChild->read << " " << fromChild->write << " " 
			<< feedbackToChild->read << " " << feedbackFromChild->write;
		STARTUPINFO si = {};
		si.cb = sizeof(STARTUPINFO);
		PROCESS_INFORMATION pi = {};
		LPWSTR arg = _wcsdup(ss.str().c_str());
		
		if (0 == CreateProcess(NULL, arg, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
			::free(arg);
			throw win32exception("couldn't create child process");
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

		CloseHandle(child);
	}

	virtual void load(std::string url, nullary_t check = [](){}) {
		to.sync().operation("load").arg(url);
		from.sync().checkReturn();
	}

	virtual void freeTracks(nullary_t check = [](){}) {
		to.sync().operation("frtr");
		from.sync().checkReturn();
	}

	virtual void initialise(int subsong, nullary_t check = [](){}) {
		to.sync().operation("init").arg(subsong);
		from.sync().checkReturn();
	}

	virtual uint32_t currentSubsongCount(nullary_t check = [](){}) {
		to.sync().operation("ssct");
		return from.sync().returnUint32_t();
	}

	virtual Gentry *take(nullary_t check = [](){}) {
		to.sync().operation("take");
		return from.sync().returnGentry();
	}

	virtual void free(Gentry *entry) {
		delete[] entry->data;
		delete entry;
	}
};
