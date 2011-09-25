#pragma once

#define MYVERSION "0.02"

#define _WIN32_WINNT 0x0600

#include <windows.h>
#include "boost/noncopyable.hpp"
#include <string>
#include <sstream>

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
	Gentry *take();
	void free(Gentry *e);
};
