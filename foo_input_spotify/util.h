#pragma once

#define MYVERSION "0.1"

#define _WIN32_WINNT 0x0600

#include <windows.h>

struct Gentry {
	void *data;
    size_t size;
	int sampleRate;
	int channels;
};

struct CriticalSection {
	CRITICAL_SECTION cs;
	CriticalSection() {
		InitializeCriticalSection(&cs);
	}

	~CriticalSection() {
		DeleteCriticalSection(&cs);
	}
};

struct LockedCS {
	CRITICAL_SECTION &cs;
	LockedCS(CriticalSection &o) : cs(o.cs) {
		EnterCriticalSection(&cs);
	}

	~LockedCS() {
		LeaveCriticalSection(&cs);
	}
};

struct Buffer {

	size_t entries;
	size_t ptr;

	static const size_t MAX_ENTRIES = 255;

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
