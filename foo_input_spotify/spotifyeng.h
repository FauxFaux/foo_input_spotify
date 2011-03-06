#ifndef _KDMENG_H_
#define _KDMENG_H_

#include <foobar2000.h>

class kdmeng
{
	enum
	{
		NUMCHANNELS = 16,
		MAXWAVES = 256,
		MAXTRACKS = 256,
		MAXNOTES = 8192,
		MAXEFFECTS = 16,

		MAXSAMPLESTOPROCESS = 32768
	};

	unsigned kdmsamplerate, kdmnumspeakers, kdmbytespersample;

	unsigned numwaves;
	char instname[MAXWAVES][16];
	unsigned wavleng[MAXWAVES];
	unsigned repstart[MAXWAVES], repleng[MAXWAVES];
	int finetune[MAXWAVES];

	unsigned totsndbytes;
	unsigned char * wavoffs[MAXWAVES];

	unsigned eff[MAXEFFECTS][256];

	t_uint32 kdmversionum, numnotes, numtracks;
	char trinst[MAXTRACKS], trquant[MAXTRACKS];
	char trvol1[MAXTRACKS], trvol2[MAXTRACKS];
	t_uint32 nttime[MAXNOTES];
	char nttrack[MAXNOTES], ntfreq[MAXNOTES];
	char ntvol1[MAXNOTES], ntvol2[MAXNOTES];
	char ntfrqeff[MAXNOTES], ntvoleff[MAXNOTES], ntpaneff[MAXNOTES];

	long timecount, notecnt, musicstatus, musicrepeat, loopcnt;

	long kdmasm1, kdmasm3;
	unsigned char * kdmasm2, * kdmasm4;

	pfc::array_t<unsigned char> snd;

	long stemp[MAXSAMPLESTOPROCESS];

	long splc[NUMCHANNELS], sinc[NUMCHANNELS];
	unsigned char * soff[NUMCHANNELS];
	long svol1[NUMCHANNELS], svol2[NUMCHANNELS];
	long volookup[NUMCHANNELS<<9];
	long swavenum[NUMCHANNELS];
	long frqeff[NUMCHANNELS], frqoff[NUMCHANNELS];
	long voleff[NUMCHANNELS], voloff[NUMCHANNELS];
	long paneff[NUMCHANNELS], panoff[NUMCHANNELS];

	long frqtable[256];
	long ramplookup[64];

	long monohicomb( long * b, long c, long d, long si, long * di );
	long stereohicomb( long * b, long c, long d, long si, long * di );

	long loadwaves( const char * refdir, abort_callback & p_abort );

	void startwave ( long wavnum, long dafreq, long davolume1, long davolume2, long dafrqeff, long davoleff, long dapaneff );

public:
	kdmeng( unsigned samplerate, unsigned numspeakers, unsigned bytespersample );

	void musicon();
	void musicoff();

	long load ( service_ptr_t<file> & fil, const char * filename, abort_callback & p_abort );

	long rendersound ( void * dasnd, long numbytes );

	void seek ( long seektoms );
};

#endif
