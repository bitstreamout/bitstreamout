/*
 * vob2vdr.c:	Strip MPEG VOB streams down to PES VDR streams
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 * Copyright (C) 2003,2005 Werner Fink, <werner@suse.de>
 *
 * Partly based on ftp://ftp.cadsoft.de/vdr/xlist.c
 * Copyright (C) 2001 Klaus Schmidinger, <kls@cadsoft.de>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <netinet/in.h>

#ifndef AARONS_TYPES
#define AARONS_TYPES
typedef unsigned long long uint_64;
typedef unsigned int   uint_32;
typedef unsigned short uint_16;
typedef unsigned char  uint_8;

typedef signed long long sint_64;
typedef signed int     sint_32;
typedef signed short   sint_16;
typedef signed char    sint_8;
#endif

#define dsyslog(format, args...)	fprintf(stderr, "xlist: " format "\n\r", ## args)
#define O_PIPE				(O_NONBLOCK|O_ASYNC)

// Exeption class
class cExeption {
public:
    const int reason;
    cExeption(const int Reason) : reason(Reason) {};
};

class cHandle {
private:
    uint_64 offset;
    uint_8 *const data;
    uint_8 * curr;
    const size_t size;
    const size_t over;
    volatile size_t left;
    volatile size_t head;
    volatile size_t tail;
    volatile size_t avail;
    int fd;
    cExeption ex;
    inline bool watch(unsigned long int ms)
    {
	fd_set watch;
	suseconds_t usec = ms*1000;
	struct timeval tv = { 0, usec};

	FD_ZERO(&watch);
	FD_SET(fd, &watch);

	int ret = 0;
	do {
	    errno = 0;
	    ret = select(fd+1, &watch, (fd_set*)0, (fd_set*)0, &tv);
	} while (ret < 0 && errno == EINTR);

	if (tv.tv_usec == usec)
	    ret = -1;
	return (ret > 0) ? FD_ISSET(fd, &watch) : false;
    }
    void refill(void) throw(cExeption)
    {
	size_t free;

	if (fd < 0)		// Static buffer, skip
	    goto out;

	if (!(free = (size - avail)))
	    goto out;

	do {
	    size_t  put = (tail+free > size) ? size - tail : free;
	    ssize_t real = read(fd, data+tail, put);
	    if (real < 0) {
		if (errno == EINTR || errno == EAGAIN)
		    continue;
		throw ex;
	    }
	    if (real == 0) {
		if (watch(320))
		    continue;
		fd = -1;	// Nothing new, we now have a static buffer
		break;
	    }
	    avail += real;
	    free  -= real;
	    tail   = (tail + real) % size;

	} while (free);
    out:
	return;
    };
    inline void rolling(const size_t pos)
    {
	ssize_t roll;
	//
	// Case of rolling the memory window over the end of the ring
	// buffer, just copy the data from the buffer begin to the
	// overroll area to get one continues memory window.
	//
	if ((pos < size) && ((roll = (pos+over) - size) > 0))
	    memcpy(data+size, data, roll);
    }
    inline void movwin(const size_t len)
    {
	size_t  want, pos = head;

	if (!len || (over && (len > over)))
	    goto out;

	if ((fd >= 0) && (avail < size/2))
	    (void)refill();

	if (avail == 0)
	    goto out;

	if ((want = len) > avail)
	    want = avail;

	pos += want;
	if (over)
	    rolling(pos);

	head = pos % size;
	curr = data+head;
	avail -= want;
	offset += want;
    out:
	return;
    };
    inline bool check(const size_t off) throw(cExeption)
    {
	if ((fd >= 0) && (avail < size/2))
	    (void)refill();
	left = avail;
	if (over) {
	    if (left > over)
		left = over;
	    rolling(head+off);
	}
	if (left < off)
	    throw ex;
	return true;
    }
public:
	//
	// Dynamic buffers filled from file opened with descriptor `file'
	// or static buffer aready filled.  In case of a dynamic filter
	// you may provide a minimal memory window step size to be able
	// to ensure continues buffers from operators even if a buffer
	// boundary is reached.
	//
    cHandle(uint_8 *buf, size_t len, const size_t step = 0, const int file = -1)
    : data(buf), curr(data), size(len-step), over(step), fd(file), ex(1)
    {
	avail = head = tail = 0;
	offset = 0;
	if (fd < 0) {		// For static buffers we assume a filled buffer
	    tail  += size;
	    avail += size;
	} else
	    (void)refill();
    };
	//
	// Return current offset
	//
    virtual const uint_64 Offset() const { return offset; };
	//
	// Return unsigned character value of
	// current buffer position
	//
    inline operator uint_8  () { return check(1) ? *curr : 0; };
	//
	// Return unsigned short value of current
	// buffer position (in big endian order)
	//
    inline operator uint_16 () { return check(2) ? ntohs(*((uint_16*)curr)) : 0; };
	//
	// Return unsigned int value of current
	// buffer position (in big endian order)
	//
    inline operator uint_32 () { return check(4) ? ntohl(*((uint_32*)curr)) : 0; };
	//
	// Returns true if buffer has at least one
	// left member
	//
    inline operator bool    () { return check(1); };
	//
	// A few useful relational operators
	//
    inline bool operator >= (const size_t  &o) { return          (avail >= o); };
    inline bool operator >= (const uint_32 &o) { return          (avail >= (const size_t) o); };
    inline bool operator >= (const int     &o) { return ((ssize_t)avail >= (const ssize_t)o); };
    inline bool operator >  (const size_t  &o) { return          (avail >  o); };
    inline bool operator >  (const uint_32 &o) { return          (avail >  (const size_t) o); };
    inline bool operator >  (const int     &o) { return ((ssize_t)avail >  (const ssize_t)o); };
    inline bool operator <= (const size_t  &o) { return          (avail <= o); };
    inline bool operator <= (const uint_32 &o) { return          (avail <= (const size_t) o); };
    inline bool operator <= (const int     &o) { return ((ssize_t)avail <= (const ssize_t)o); };
    inline bool operator <  (const size_t  &o) { return          (avail <  o); };
    inline bool operator <  (const uint_32 &o) { return          (avail <  (const size_t) o); };
    inline bool operator <  (const int     &o) { return ((ssize_t)avail <  (const ssize_t)o); };
    inline bool operator == (const size_t  &o) { return          (avail == o); };
    inline bool operator == (const uint_32 &o) { return          (avail == (const size_t) o); };
    inline bool operator == (const int     &o) { return ((ssize_t)avail == (const ssize_t)o); };
    inline bool operator != (const size_t  &o) { return          (avail != o); };
    inline bool operator != (const uint_32 &o) { return          (avail != (const size_t) o); };
    inline bool operator != (const int     &o) { return ((ssize_t)avail != (const ssize_t)o); };
	//
	// Skip current read position, goto next (++x).
	//
    inline cHandle& operator ++()      { movwin(1); return *this; };
	//
	// Skip current read position, goto next.
	// Return previous (x++).
	//
    inline cHandle  operator ++(int)
    {
	check(1);		// Before moving position, check and maybe rolling over
	const cHandle ret(*this); movwin(1); return ret;
    };
	//
	// Skip o positions from current read
	// position (x += o).
	//
    inline cHandle& operator +=(const size_t& o) { movwin(o); return *this; };
	//
	// Return current buffer position with at least
	// o members included. Do not use any position
	// operator during usage of the pointer to this
	// buffer position
	//
    inline uint_8*  operator ()(const size_t& o) { return check(o) ? curr : NULL; }
	//
	// Return left members of buffer. Should be less or
	// equal to step size if step size is larger than zero.
	//
    inline const size_t len(void) { check(0); return left; };
	//
	// Exeption handling: you should use this instead
	// of `while (handle)' or `if (handle)' ... this
	// avoids checks for potential buffer shortage.
	//
#   define foreach(handle)	try { while ((handle))
#   define test(handle)		try { if((handle))
#   define end(handle)		} catch (cExeption& ex) { if (ex.reason != 1) throw; }

};

// Signal handling
static struct sigaction saved_act[SIGUNUSED];
static void sighandler(int sig)
{
    sigset_t set;

    if (sig != SIGPIPE && sig != SIGIO)
	dsyslog("Catch %s", strsignal(sig));

    if (sig == SIGIO)
	return;

    usleep(100);

    if (sigaction (sig, &saved_act[sig], NULL))
	_exit(1);
    if (sigemptyset(&set) || sigaddset(&set, sig) ||
	sigprocmask(SIG_UNBLOCK, &set, NULL))
	_exit(1);
    kill(-getpid(), sig);	// Raise signal
}

static void install_sighandler(int sig)
{
    struct sigaction act;
    if (sigaction (sig, NULL, &saved_act[sig]))		// Old action handler
	return;
    if (saved_act[sig].sa_handler == sighandler)	// Compare with our
	return;

    act.sa_handler = sighandler;
    if (sig == SIGIO) {
	act.sa_flags = SA_RESTART;
    } else {
	act.sa_flags = SA_ONESHOT;
    }
    if (sigemptyset (&act.sa_mask) < 0)
	_exit(1);
    if (sigaction (sig, &act, NULL))
	_exit(1);
}

// Known options
static FILE* out = stdout;
static struct option long_option[] =
{
    {"help",      0, NULL,  'h'},
    {"block",     0, NULL,  'b'},
    {"output",    1, NULL,  'o'},
    {"ac3",       1, NULL,  'a'},
    {"dts",       1, NULL,  'd'},
    {"pcm",       1, NULL,  'p'},
    {"track",     1, NULL,  't'},
    { NULL,       0, NULL,   0 },
};

static void help(void)
{
        printf("Usage: vob2vdr <options> [file]\n");
        printf("\nAvailable options:\n");
        printf("  -h, --help         this help\n");
        printf("  -b, --block        use blocking read mode on stdin\n");
        printf("  -o, --output=file  use this file for output\n");
        printf("  -a, --ac3          include AC3 audio stream\n");
        printf("  -d, --dts          include DTS audio stream\n");
        printf("  -p, --pcm          include linear PCM audio stream\n");
        printf("  -t, --track=[1..8] set track number for audio stream\n");
}

// Forward declaration
#define AC3	1
#define DTS	2
#define PCM	3

#define TR0	(0<<3)
#define TR1	(1<<3)
#define TR2	(2<<3)
#define TR3	(3<<3)
#define TR4	(4<<3)
#define TR5	(5<<3)
#define TR6	(6<<3)
#define TR7	(7<<3)

static uint_16 flags = (AC3|TR0);

static void scan(cHandle handle);

int main(int argc, char *argv[])
{
    bool block = false;
    cHandle *handle;
    char *output;
    int c, tr;

    while ((c = getopt_long(argc, argv, "bo:hadpt:", long_option, NULL)) > 0) {
	switch (c) {
	case 'b':
	    block = true;
	    break;
	case 'o':
	    if (!optarg || *optarg == '-') {
		help();
		exit(1);
	    }
	    if (!(output = strdup(optarg))) {
		dsyslog("strdup() failed: %s", strerror(errno));
		exit(1);
	    }
	    if (!(out = fopen(output, "a+"))) {
		dsyslog("fopen(%s) failed: %s", output, strerror(errno));
		exit(1);
	    }
	    break;
	case 'h':
	    help();
	    exit(0);
	    break;
	case 'a':
	    flags = AC3 | (flags & 0x38);
	    break;
	case 'd':
	    flags = DTS | (flags & 0x38);
	    break;
	case 'p':
	    flags = PCM | (flags & 0x38);
	    break;
	case 't':
	    if (!optarg || *optarg == '-') {
		help();
		exit(1);
	    }
	    if (!(tr = atoi(optarg))) {
		dsyslog("atoi() failed: %s", strerror(errno));
		exit(1);
	    }
	    if (tr < 1 || tr > 8) {
		help();
		exit(1);
	    }
	    tr--;
	    flags = (flags & 3)|4|(tr << 3);
	    break;
	default:
	    help();
	    exit(1);
	    break;
	}
    }
    argv += optind;
    argc -= optind;
    if (*argv && **argv == '-') {
	argv++;
	argc--;
    }

    install_sighandler(SIGINT);
    install_sighandler(SIGQUIT);
    install_sighandler(SIGHUP);
    install_sighandler(SIGTERM);
    install_sighandler(SIGPIPE);
    install_sighandler(SIGIO);

#   define STEPSIZE	 4096
#   define OVERALL	(128*STEPSIZE)
    static uint_8* buf = (uint_8*)malloc(OVERALL);

    if (!buf)
	exit(1);

    bool loop = true;
    while (true) {
	int fd = STDIN_FILENO;

	if (argc <= 0) {
	    if (!loop)
		break;
	    if (!block && fd == STDIN_FILENO) {
		int flags = fcntl(fd, F_GETFL);
		if (flags >= 0) {
		    if (fcntl(fd, F_SETFL, (flags|O_PIPE)) < 0 ||
			fcntl(fd, F_SETOWN, getpid()) < 0)
			dsyslog("Couldn't set NONBLOCK|ASYNC mode on stdin: %s", strerror(errno));
		}
	    }
	} else {
	    if (!*argv)
		break;
	    if ((fd = open(*argv, O_NOCTTY|O_RDONLY)) < 0) {
		dsyslog("Couldn't open file %s: %s", *argv, strerror(errno));
		exit(1);
	    }
	    argv++;
	    argc--;
	}
	loop = false;

	handle = new cHandle(&buf[0], OVERALL, STEPSIZE, fd);

	scan(*handle);

	if (handle) {
	    delete handle;
	    handle = NULL;
	}

	if (fd != STDIN_FILENO)
	    close (fd);
    }

    free(buf);
    return 0;
}

static void scan(cHandle handle)
{
    char I_Frame = 0; 
    bool Header  = false;

    foreach(handle >= 4) {
	uint_32 ul = handle;
	uint_32 lu = htonl(ul);
	if ((ul & 0xffffff00) == 0x0000100) {
	    handle += 4;

	    uint_8  ub = (uint_8)(ul & 0x000000ff);
	    uint_16 m  = handle;
	    uint_16 M  = htons(m);
	    uint_8  l  = (uint_8)((m>>8)&0x00ff);

	    switch (ub) {
	    case 0xb9:		// Program end (terminates a program stream)
		break;
	    case 0xba:		// Pack header
		if (((l>>2) & 0x01) != 1 && (l>>6) != 1)
		    break;
		Header = true;
		I_Frame = 0;
		handle += 10;
		break;
	    case 0xbb:		// System Header
		handle += (2+m);
		break;
	    case 0xbc:		// Program Stream Map
		break;
	    case 0xbd:		// Private stream 1
		handle += 2;
		if (m > handle.len())
		    m = handle.len();
		if (handle >= (size_t)m) {
		    uint_8 *p;
		    uint_8  h;
		    if (m < 2 || !(p = handle(m)))
			break;
		    if (m < (h = p[2] + 3))
			break;
		    switch (p[h]) {
		    case 0x80 ... 0x87:		// Dolby Digital Audio
			if ((flags & 3) != AC3)
			    goto skip;
			if (!(flags & 4))
			    flags = (flags & 3)|4|((p[h] & 7)<<3);
			if ((p[h] & 7) != (flags >> 3))
			    goto skip;
			break;
		    case 0x88 ... 0x8f:		// DTS Audio
			if ((flags & 3) != DTS)
			    goto skip;
			if (!(flags & 4))
			    flags = (flags & 3)|4|((p[h] & 7)<<3);
			if ((p[h] & 7) != (flags >> 3))
			    goto skip;
			break;
		    case 0xa0 ... 0xa7:		// Linear PCM Audio
			if ((flags & 3) != PCM)
			    goto skip;
			if (!(flags & 4))
			    flags = (flags & 3)|4|((p[h] & 7)<<3);
			if ((p[h] & 7) != (flags >> 3))
			    goto skip;
			break;
		    default:
			goto skip;
		    }
		    fwrite((uint_8*)&lu, 1, 4, out);
		    fwrite((uint_8*)&M, 1, 2, out);
		    fwrite(p, sizeof(uint_8), m, out);
		}
	    skip:
		handle += m;
		break;
	    case 0xbe:		// Padding stream
		handle += 2;
		l = handle;
		if (l != 0xff)
		    break;
		handle += m;
		break;
	    case 0xbf:		// Private stream 2
		handle += (2+m);
		break;
	    case 0xc0 ... 0xdf:	// MPEG-1 or MPEG-2 audio stream
		handle += 2;
		fwrite((uint_8*)&lu, 1, 4, out);
		fwrite((uint_8*)&M, 1, 2, out);
		if (m > handle.len())
		    m = handle.len();
		if (handle >= (size_t)m)
		    fwrite((uint_8*)handle(m), sizeof(uint_8), m, out);
		handle += m;
		break;
	    case 0xe0 ... 0xef:	// MPEG-1 or MPEG-2 video stream
		handle += 2;
		fwrite((uint_8*)&lu, 1, 4, out);
		fwrite((uint_8*)&M, 1, 2, out);
		if (m > handle.len())
		    m = handle.len();
		if (handle >= (size_t)m)
		    fwrite((uint_8*)handle(m), sizeof(uint_8), m, out);
		handle += m;
		break;
	    case 0xf0:		// ECM Stream
		break;
	    case 0xf1:		// EMM Stream
		break;
	    case 0xf2:		// ITU-T Rec. H.222.0
				// ISO/IEC 13818-1 Annex A or ISO/IEC 13818-6 DSMCC stream
		break;
	    case 0xf3:		// ISO/IEC_13522_stream
		break;
	    case 0xf4:		// ITU-T Rec. H.222.1 type A
		break;
	    case 0xf5:		// ITU-T Rec. H.222.1 type B
		break;
	    case 0xf6:		// ITU-T Rec. H.222.1 type C
		break;
	    case 0xf7:		// ITU-T Rec. H.222.1 type D
		break;
	    case 0xf8:		// ITU-T Rec. H.222.1 type E
		break;
	    case 0xf9:		// ancillary_stream
		break;
	    case 0xff:		// Program Stream Directory
		break;
	    default:
		fprintf(stderr, " unkown or reserved %08x", ul);
		break;
	    }
	} else {
	    handle++;
	}
    } end (handle);
};
