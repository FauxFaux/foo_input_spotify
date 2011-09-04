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

struct Pipe : boost::noncopyable {
	HANDLE read;
	HANDLE write;
	Pipe();
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
		if (FAILED(WriteFile(h, str.c_str(), str.size(), NULL, NULL)))
			throw std::exception("couldn't communicate with child");
		return *this;
	}
};

struct PipeIn {
	HANDLE h;
	PipeIn(HANDLE h) : h(h) {}
	PipeIn& sync() {
		assertEquals("sync", take(strlen("sync")));
		return *this;
	}

	PipeIn &checkReturn() {
		const std::string ret = take(2);
		if ("OK" == ret) {
			return *this;
		} else if ("EX" == ret) {
			throw std::exception(takeString().c_str());
		} else {
			throw std::exception(("Unknown return type: " + ret).c_str());
		}
	}

	uint32_t returnUint32_t() {
		checkReturn();
		return takeLen();
	}

	Gentry *returnGentry() {
		Gentry *entry = new Gentry;
		entry->channels = takeLen();
		entry->sampleRate = takeLen();
		entry->size = takeLen();
		entry->data = new char[entry->size];
		memcpy(entry->data, take(entry->size).data(), entry->size);
		return entry;
	}

	long takeLen() {
		std::stringstream ss(take(lengthLength));
		long len;
		ss >> len;
		return len;
	}

	std::string takeString() {
		return take(takeLen());
	}

	std::string take(const size_t len) {
		std::vector<char> buf(len);
		if (FAILED(ReadFile(h, buf.data(), len, NULL, NULL)))
			throw std::exception("couldn't communicate with child");
		return std::string(buf.begin(), buf.end());
	}

	void assertEquals(const std::string &expected, const std::string &actual) {
		if (expected != actual) {
			throw std::exception((expected + " != " + actual).c_str());
		}
	}
};
