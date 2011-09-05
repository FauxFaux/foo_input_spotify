#pragma once

#define MYVERSION "0.02"

#define _WIN32_WINNT 0x0600

#include <windows.h>
#include "boost/noncopyable.hpp"
#include <functional>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <stdint.h>

typedef std::function<std::string()> stringfunc_t;
typedef std::function<void(std::string)> funcstr_t;
typedef std::function<void()> nullary_t;

struct win32exception : std::exception {
	std::string makeMsg(const std::string &cause, DWORD err) {
		std::stringstream ss;
		ss << cause << ", win32: " << err << " (" << std::hex << err << "): ???";
		return ss.str();
#if ARGH
		LPVOID lpMsgBuf;
		FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				err,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPTSTR) &lpMsgBuf,
				0, NULL );
		LocalFree(lpMsgBuf);
//		pfc::stringcvt::string_utf8_from_wide((wchar_t*)lpMsgBuf, wcslen((wchar_t*)lpMsgBuf));
//		WideCharToMultiByte(CP_UTF8, 0, lpMsgBuf, -1, buf.data(), buf.size(), NULL, NULL);
#endif
	}

	win32exception(std::string cause) : std::exception(makeMsg(cause, GetLastError()).c_str()) {
	}

	win32exception(std::string cause, DWORD err) : std::exception(makeMsg(cause, err).c_str()) {
	}
};

struct Gentry {
	void *data;
    size_t size;
	int sampleRate;
	int channels;
};

struct CriticalSection : boost::noncopyable {
	CRITICAL_SECTION cs;
	CriticalSection() {
		InitializeCriticalSection(&cs);
	}

	~CriticalSection() {
		DeleteCriticalSection(&cs);
	}
};

struct LockedCS : boost::noncopyable {
	CRITICAL_SECTION &cs;
	LockedCS(CriticalSection &o) : cs(o.cs) {
		EnterCriticalSection(&cs);
	}

	~LockedCS() {
		LeaveCriticalSection(&cs);
	}

	void dropAndReacquire(DWORD wait = 0) {
		LeaveCriticalSection(&cs);
		Sleep(wait);
		EnterCriticalSection(&cs);
	}
};

/** A fixed sized, thread-safe queue with blocking take(), but without an efficient blocking add implementation.
 * Expected use: Main producer, secondary notification producers, single consumer. 
 * Main producer is expected to back-off when queue is full.
 *
 * In hindsight, a dual-lock queue would've been simpler.  Originally there wern't multiple producers..
 */
struct Buffer : boost::noncopyable {

	size_t entries;
	size_t ptr;

	static const size_t MAX_ENTRIES = 255;
	static const size_t SPACE_FOR_UTILITY_MESSAGES = 5;

	Gentry *entry[MAX_ENTRIES];

	CONDITION_VARIABLE bufferNotEmpty;
	CriticalSection bufferLock;

	Buffer();
	~Buffer();
	void add(void *data, size_t size, int sampleRate, int channels);
	bool isFull();
	void flush();
	Gentry *take(nullary_t check = [](){});
	void free(Gentry *e);
};

struct Pipe : boost::noncopyable {
	HANDLE read;
	HANDLE write;
	Pipe() {
		SECURITY_ATTRIBUTES sec = {};
		sec.nLength = sizeof(SECURITY_ATTRIBUTES);
		sec.lpSecurityDescriptor = NULL;
		sec.bInheritHandle = TRUE;
		if (!CreatePipe(&read, &write, &sec, 0))
			throw win32exception("Couldn't create pipe");
	}

	~Pipe() {
		CloseHandle(read);
		CloseHandle(write);
	}
};

const int lengthLength = 8;

struct PipeOut {
	HANDLE h;
	PipeOut(HANDLE h) : h(h) {}

	PipeOut &sync() {
		put("sync");
		return *this;
	}

	PipeOut &operation(const std::string& token4) {
		put(token4);
		return *this;
	}

	PipeOut &arg(const std::string& str) {
		arg(str.size());
		put(str);
		return *this;
	}

	PipeOut &arg(const long i) {
		std::stringstream ss;
		ss.fill('0');
		ss.width(lengthLength);
		ss << i;
		put(ss.str());
		return *this;
	}

	PipeOut &put(const std::string &str) {
		DWORD written = 0;
		if (FAILED(WriteFile(h, str.c_str(), str.size(), &written, NULL)))
			throw win32exception("couldn't communicate with child");

		if (written != str.size())
			throw std::exception("not entirely written");

		return *this;
	}

	PipeOut &doReturnInt(std::function<int()> func) {
		int i;
		doReturn([&]() {
			i = func();
		});
		arg(i);
		return *this;
	}

	PipeOut &doReturn(std::function<void()> func) {
		try {
			func();
			put("OK");
		} catch (std::exception &e) {
			put("EX");
			arg(e.what());
		}
		return *this;
	}
};

struct PipeIn {
	HANDLE h;
	std::function<void()> check;
	PipeIn(HANDLE h, std::function<void()> check) : h(h), check(check) {
	}

	PipeIn& sync(nullary_t additionalCheck = [](){}) {
		assertEquals("sync", take(strlen("sync"), additionalCheck));
		return *this;
	}

	PipeIn &checkReturn(nullary_t additionalCheck = [](){}) {
		const std::string ret = take(2, additionalCheck);
		if ("OK" == ret) {
			return *this;
		} else if ("EX" == ret) {
			throw std::exception(takeString(additionalCheck).c_str());
		} else {
			throw std::exception(("Unknown return type: " + ret).c_str());
		}
	}

	uint32_t returnUint32_t(nullary_t additionalCheck = [](){}) {
		checkReturn(additionalCheck);
		return takeLen(additionalCheck);
	}

	Gentry *returnGentry(nullary_t additionalCheck = [](){}) {
		checkReturn(additionalCheck);
		Gentry *entry = new Gentry;
		entry->channels = takeLen(additionalCheck);
		entry->sampleRate = takeLen(additionalCheck);
		entry->size = takeLen(additionalCheck);
		entry->data = new char[entry->size];
		memcpy(entry->data, take(entry->size).data(), entry->size);
		return entry;
	}

	long takeLen(nullary_t additionalCheck = [](){}) {
		std::stringstream ss(take(lengthLength, additionalCheck));
		long len;
		ss >> len;
		return len;
	}

	std::string takeCommand(nullary_t additionalCheck = [](){}) {
		return take(4, additionalCheck);
	}

	std::string takeString(nullary_t additionalCheck = [](){}) {
		return take(takeLen(additionalCheck), additionalCheck);
	}

	std::string take(const size_t len, nullary_t additionalCheck = [](){}) {
		std::vector<char> buf(len);
		DWORD read = 0;
		DWORD avail = 0;
		DWORD sleep = 5;
		do {
			if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL))
				throw win32exception("couldn't peek");
			check();
			additionalCheck();
			//Sleep(sleep);
			if (sleep < 200)
				;//sleep += 10;
		} while (avail < len);

		if (!ReadFile(h, buf.data(), len, &read, NULL))
			throw win32exception("couldn't read pipe");

		if (read != len)
			throw std::exception("Not entirely read");

		return std::string(buf.begin(), buf.end());
	}

	void assertEquals(const std::string &expected, const std::string &actual) {
		if (expected != actual) {
			throw std::exception((expected + " != " + actual).c_str());
		}
	}
};
