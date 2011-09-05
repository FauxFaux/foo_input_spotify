#include "../foo_input_spotify/util.h"

Buffer::Buffer() : entries(0), ptr(0) {
	InitializeConditionVariable(&bufferNotEmpty);
}

Buffer::~Buffer() {
	flush();
}

void Buffer::add(void *data, size_t size, int sampleRate, int channels) {
	Gentry *e = new Gentry;
	e->data = data;
	e->size = size;
	e->sampleRate = sampleRate;
	e->channels = channels;

	{
		LockedCS lock(bufferLock);

		// Yes, this is spinlock.  See the class comment.
		while (entries >= MAX_ENTRIES)
			lock.dropAndReacquire();

		entry[(ptr + entries) % MAX_ENTRIES] = e;
		++entries;
	}
	WakeConditionVariable(&bufferNotEmpty);
}

bool Buffer::isFull() {
	return entries >= MAX_ENTRIES - SPACE_FOR_UTILITY_MESSAGES;
}

void Buffer::flush() {
	while (entries > 0)
		free(take());
}

Gentry *Buffer::take(nullary_t check) {
	LockedCS lock(bufferLock);
	while (entries == 0) {
		SleepConditionVariableCS(&bufferNotEmpty, &bufferLock.cs, 1000);
		check();
	}

	Gentry *e = entry[ptr++];
	--entries;
	if (MAX_ENTRIES == ptr)
		ptr = 0;

	return e;
}

void Buffer::free(Gentry *e) {
	delete[] e->data;
	delete e;
}
