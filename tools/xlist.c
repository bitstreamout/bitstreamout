/*
 * xlist.c:	List the content of MPEG files
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
 * Copyright (C) 2003-2005 Werner Fink, <werner@suse.de>
 *
 * Partly based on ftp://ftp.cadsoft.de/vdr/xlist.c
 * Copyright (C) 2001 Klaus Schmidinger, <kls@cadsoft.de>
 *
 * Also used:
 * libmad   source as documentation, http://sourceforge.net/projects/mad/
 * libmpeg2 source as documentation, http://sourceforge.net/projects/libmpeg2
 * also the documentation found at http://mpucoder.kewlhair.com/DVD/
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
    inline bool operator >= (const uint_32 &o) { return		 (avail >= (const size_t) o); };
    inline bool operator >= (const int     &o) { return ((ssize_t)avail >= (const ssize_t)o); };
    inline bool operator >  (const uint_32 &o) { return		 (avail >  (const size_t) o); };
    inline bool operator >  (const int     &o) { return ((ssize_t)avail >  (const ssize_t)o); };
    inline bool operator <= (const uint_32 &o) { return		 (avail <= (const size_t) o); };
    inline bool operator <= (const int     &o) { return ((ssize_t)avail <= (const ssize_t)o); };
    inline bool operator <  (const uint_32 &o) { return		 (avail <  (const size_t) o); };
    inline bool operator <  (const int     &o) { return ((ssize_t)avail <  (const ssize_t)o); };
    inline bool operator == (const uint_32 &o) { return		 (avail == (const size_t) o); };
    inline bool operator == (const int     &o) { return ((ssize_t)avail == (const ssize_t)o); };
    inline bool operator != (const uint_32 &o) { return		 (avail != (const size_t) o); };
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
#define D(format, args...)		fprintf(out, format "\n", ## args)
#define N(format, args...)		fprintf(out, format,      ## args)
#define nl				putc('\n', out);
static struct option long_option[] =
{
    {"help",      0, NULL,  'h'},
    {"block",     0, NULL,  'b'},
    {"output",    1, NULL,  'o'},
    {"noslices",  1, NULL,  's'},
    {"noaudpay",  1, NULL,  'a'},
    {"nobdpay",   1, NULL,  'd'},
    { NULL,       0, NULL,   0 },
};

static void help(void)
{
        printf("Usage: xlist <options> [file]\n");
        printf("\nAvailable options:\n");
        printf("  -h, --help         this help\n");
        printf("  -b, --block        use blocking read mode on stdin\n");
        printf("  -o, --output=file  use this file for output\n");
        printf("  -s, --noslices     do not show video slice nor sequences\n");
        printf("  -a, --noaudpay     do not show payload of audio streams\n");
        printf("  -d, --nobdpay      do not show payload of private stream 1\n");
}

// Forward declaration
static bool skip_audpay = false;
static bool skip_slices = false;
static bool skip_bd_pay = false;
static bool mpeg_v1 = false;
static void scan(cHandle handle);

int main(int argc, char *argv[])
{
    bool block = false;
    cHandle *handle;
    char *output;
    int c;

    while ((c = getopt_long(argc, argv, "bo:hsad", long_option, NULL)) > 0) {
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
	case 's':
	    skip_slices = true;
	    break;
	case 'a':
	    skip_audpay = true;
	    break;
	case 'd':
	    skip_bd_pay = true;
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

// PTS (Present Time Stamps)
typedef struct _pts {
    uint_64 vid;
    uint_64 aud[8];
    uint_64 ps1;
} pts_t;
static pts_t pts;

// Used in scan for private stream data
#define BD_IS_ALIGNED	0x01
#define BD_HAS_PTSDTS	0x02
static off_t scan_bd(uint_8* const buf, const off_t rest, const uint_16 flags, const uint_64 deep);

// Used in scan for audio data
static off_t scan_c0(uint_8* const buf, const off_t rest, const uint_16 flags, const uint_64 deep, const int id);

// Used in scan for video data
static off_t scan_e0(uint_8* const buf, const off_t rest, const uint_16 flags, const uint_64 deep);

static void scan(cHandle handle)
{
    char I_Frame = 0; 
    bool Header  = false;

    foreach(handle >= 4) {
	uint_32 ul = handle;
	if ((ul & 0xffffff00) == 0x0000100) {
	    uint_64 offset = handle.Offset();
	    handle += 4;

	    uint_8  ub = (uint_8)(ul & 0x000000ff);
	    uint_16 m  = handle;
	    uint_8  l  = (uint_8)((m>>8)&0x00ff);

	    N("%8llu %02X", offset, ub);
	    switch (ub) {
	    case 0xb9:		// Program end (terminates a program stream)
		D(" program_end");
		break;
	    case 0xba:		// Pack header
		if ((l & 0xc0) != 0x40) {
		    mpeg_v1 = true;
		    N(" pack_header");
		    for (int i = 4; i < 12; i++)
			N(" %02x", (uint_8)handle++);
		    nl;
		    break;
		} else {
		    uint_64 scr90 = 0;
		    uint_64 muxrt = 0;

		    N(" pack_header");
		    N(", ext:");

		    scr90  = ((uint_64)(l&0x38))<<27;
		    if ((l & 4) != 4)
			break;
		    scr90 |= ((uint_64)(l&0x03))<<28;
		    N(" %02x", (uint_8)handle++);
		    l = handle;
		    scr90 |= ((uint_64)(l&0xff))<<20;
		    N(" %02x", (uint_8)handle++);
		    l = handle;
		    scr90 |= ((uint_64)(l&0xf8))<<11;
		    if ((l & 4) != 4)
			break;
		    scr90 |= ((uint_64)(l&0x03))<<13;
		    N(" %02x", (uint_8)handle++);
		    l = handle;
		    scr90 |= ((uint_64)(l&0xff))<<4;
		    N(" %02x", (uint_8)handle++);
		    l = handle;
		    scr90 |= ((uint_64)(l&0xff))>>3;

		    N(" %02x", (uint_8)handle++);
		    if (((l = handle) & 1) != 1)
			break;
		    
		    N(" %02x", (uint_8)handle++);
		    l = handle;
		    muxrt = ((uint_64)(l&0xff))<<14;
		    N(" %02x", (uint_8)handle++);
		    l = handle;
		    muxrt = ((uint_64)(l&0xff))<<6;
		    N(" %02x", (uint_8)handle++);
		    l = handle;
		    muxrt = ((uint_64)(l&0xfc))>>2;
		    if ((l & 3) != 3)
			break;

		    N(" %02x", (uint_8)handle++);
		    if ((l = handle) & 7)
			handle += (l & 7);

		    Header = true;
		    I_Frame = 0;

		    muxrt *= 50;
		    if (muxrt > 2*1024)
			N(", mux_rate: %llukB", muxrt/1024);
		    else
			N(", mux_rate: %lluB", muxrt);
		    N(", scr90: %llums", scr90/90);
		    nl;
		}
		break;
	    case 0xbb:		// System Header
		handle += 2;
		N(" system_header");
		N(", len=%4d", m+6);
		N(", ext:");
		for (int i = 0; i < m && i < 20; i++)
		     N(" %02x", (uint_8)handle++);
		nl;
		break;
	    case 0xbc:		// Program Stream Map
		D(" program_stream_map");
		break;
	    case 0xbd:		// Private stream 1
		handle += 2;
		N(" private_stream_1");
		N(", len=%4d", m+6);
		{
		    uint_8 *p;
		    uint_8  h;
		    uint_16 b;
		    uint_16 l = m;

		    if (m < 2)
			break;
		    if (m > handle.len())
			m = handle.len();
		    if (!(p = handle(m)))
			break;
		    if (m < (h = p[2] + 3))
			break;
		    l -= h;

		    b = handle;
		    if ((b & 0xC000) != 0x8000) {
			mpeg_v1 = true;
			D(" (broken, no AC52 in Mpeg V1 multiplex)");
			break;
		    }
		    if (b & 0x0400) N(", aligned");

		    off_t off;
		    N(", ext:");
		    for (off = 0; off < h; off++)
			N(" %02x", p[off]);

		    if ((p[1] & 0x80) && (p[2] >= 5)) {
			uint_64 lpts = 0;
			lpts  = (((uint_64)p[3]) & 0x0e) << 29;
			lpts |= ( (uint_64)p[4])         << 22;
			lpts |= (((uint_64)p[5]) & 0xfe) << 14;
			lpts |= ( (uint_64)p[6])         <<  7;
			lpts |= (((uint_64)p[7]) & 0xfe) >>  1;
			N(", pts: %llums", lpts/90);
			if (pts.vid) {
			    sint_64 off = lpts - pts.vid;
			    N("(%lldms)", off/90);
			}
			if (pts.ps1) {
			    sint_64 diff = lpts - pts.ps1;
			    N("     %lldms", diff/90);
			    if (diff > 0)
				pts.ps1 = lpts;
			} else
			    pts.ps1 = lpts;
		    }

		    if (skip_bd_pay)
			handle += m;
		    else
			handle += off + scan_bd(p+off, m-off, b, handle.Offset()+off);
		}
		nl;
		break;
	    case 0xbe:		// Padding stream
		handle += 2;
		l = handle;
		if (l != 0xff)
		    break;
		handle += m;
		D(" padding_stream, len=%4d", m+6);
		break;
	    case 0xbf:		// Private stream 2
		handle += (2+m);
		D(" private_stream_2, len=%4d", m+6);
		break;
	    case 0xc0 ... 0xdf:	// MPEG-1 or MPEG-2 audio stream
		handle += 2;
		N(" audio_stream(%d)", (ub & 0x1f));
		N(", len=%4d", m+6);
		{
		    uint_8  *p;
		    uint_8  h;
		    uint_16 b;
		    uint_16 v = 3;
		    bool haspts = false;
		    int id = (ub & 0x1f);

		    if (m < 2)
			break;
		    if (m > handle.len())
			m = handle.len();
		    if (!(p = handle(m)))
			break;
		    b = handle;

		    if ((b & 0xC000) != 0x8000) {
			mpeg_v1 = true;
			h = 0;

			while (p[h] == 0xff) h++;

			if ((p[h] & 0xc0) == 0x40)
				h += 2;

			if (p[h] & 0x30) {
			    haspts = true;
			    v = h;
			    if (p[h] & 0x10)
				h += 10;
			    else
				h += 5;
			} else
			    h++;

		    } else {
			if (m < (h = p[2] + 3))
			    break;
			if (b & 0x0400)
			    N(", aligned");
			haspts = (p[1] & 0x80) && (p[2] >= 5);
		    }

		    if (p[0]&0x04)
			b |= BD_IS_ALIGNED;
		    if (p[1] & 0xC0)
			b |= BD_IS_ALIGNED;

		    off_t off;
		    N(", ext:");
		    for (off = 0; off < h; off++)
			N(" %02x", p[off]);

		    if (haspts) {
			uint_64 lpts = 0;
			lpts  = (((uint_64)p[v+0]) & 0x0e) << 29;
			lpts |= ( (uint_64)p[v+1])         << 22;
			lpts |= (((uint_64)p[v+2]) & 0xfe) << 14;
			lpts |= ( (uint_64)p[v+3])         <<  7;
			lpts |= (((uint_64)p[v+4]) & 0xfe) >>  1;
			N(", pts: %llums", lpts/90);
			if (pts.vid) {
			    sint_64 off = lpts - pts.vid;
			    N("(%lldms)", off/90);
			}
			if (id < 8) {
			    if (pts.aud[id]) {
				sint_64 diff = lpts - pts.aud[id];
				N("     %lldms", diff/90);
			    if (diff > 0)
				pts.aud[id] = lpts;
			    } else
				pts.aud[id] = lpts;
			}
		    }

		    if (skip_audpay)
			handle += m;
		    else
			handle += off + scan_c0(p+off, m-off, b, handle.Offset()+off, id);
		}
		nl;
		break;
	    case 0xe0 ... 0xef:	// MPEG-1 or MPEG-2 video stream
		handle += 2;
		N(" video_stream(%d)", (ub & 0x0f));
		N(", len=%4d", m+6);
		{
		    uint_8 *p;
		    uint_8 h;
		    uint_16 b;
		    uint_16 v = 3;
		    bool haspts = false;

		    if (m < 2)
			break;
		    if (m > handle.len())
			m = handle.len();
		    if (!(p = handle(m)))
			break;
		    b = handle;

		    if ((b & 0xC000) != 0x8000) {
			mpeg_v1 = true; 
			h = 0;

			while (p[h] == 0xff) h++;

			if ((p[h] & 0xc0) == 0x40)
				h += 2;

			if (p[h] & 0x30) {
			    haspts = true;
			    v = h;
			    if (p[h] & 0x10)
				h += 10;
			    else
				h += 5;
			} else
			    h++;

		    } else {
			if (m < (h = p[2] + 3))
			    break;
			if (b & 0x0400)
			    N(", aligned");
			haspts = (p[1] & 0x80) && (p[2] >= 5);
		    }

		    off_t off;
		    N(", ext:");
		    for (off = 0; off < h; off++)
			N(" %02x", p[off]);

		    if (haspts) {
			uint_64 lpts = 0;
			lpts  = (((uint_64)p[v+0]) & 0x0e) << 29;
			lpts |= ( (uint_64)p[v+1])         << 22;
			lpts |= (((uint_64)p[v+2]) & 0xfe) << 14;
			lpts |= ( (uint_64)p[v+3])         <<  7;
			lpts |= (((uint_64)p[v+4]) & 0xfe) >>  1;
			if (pts.vid) {
			    sint_64 diff = lpts - pts.vid;
			    N(", pts: %llums        %lldms", lpts/90, diff/90);
			    if (diff > 0)
				pts.vid = lpts;
			} else {
			    N(", pts: %llums", lpts/90);
			    pts.vid = lpts;
			}
		    }

		    if (skip_slices)
			handle += m;
		    else
			handle += off + scan_e0(p+off, m-off, b, handle.Offset()+off);
		}
		nl;
		break;
	    case 0xf0:		// ECM Stream
		D(" ECM Stream");
		break;
	    case 0xf1:		// EMM Stream
		D(" EMM Stream");
		break;
	    case 0xf2:		// ITU-T Rec. H.222.0
				// ISO/IEC 13818-1 Annex A or ISO/IEC 13818-6 DSMCC stream
		D(" ITU-T Rec. H.222.0 | ISO/IEC 13818-1 | ISO/IEC 13818-6 DSMCC");
		break;
	    case 0xf3:		// ISO/IEC_13522_stream
		D(" ISO/IEC_13522_stream");
		break;
	    case 0xf4:		// ITU-T Rec. H.222.1 type A
		D(" ITU-T Rec. H.222.1 type A");
		break;
	    case 0xf5:		// ITU-T Rec. H.222.1 type B
		D(" ITU-T Rec. H.222.1 type B");
		break;
	    case 0xf6:		// ITU-T Rec. H.222.1 type C
		D(" ITU-T Rec. H.222.1 type C");
		break;
	    case 0xf7:		// ITU-T Rec. H.222.1 type D
		D(" ITU-T Rec. H.222.1 type D");
		break;
	    case 0xf8:		// ITU-T Rec. H.222.1 type E
		D(" ITU-T Rec. H.222.1 type E");
		break;
	    case 0xf9:		// ancillary_stream
		D(" ancillary_stream");
		break;
	    case 0xff:		// Program Stream Directory
		D(" program_stream_directory");
		break;
	    default:
		D(" unkown or reserved %08x", ul);
		break;
	    }
	} else {
	    handle++;
	}
    } end (handle);
};

static off_t scan_e0(uint_8* const buf, const off_t rest, const uint_16 flags, const uint_64 deep)
{
    cHandle handle(buf, rest);
    char I_Frame = 0; 

    foreach(handle >= 4) {
	uint_32 ul = handle;
	if ((ul & 0xffffff00) == 0x0000100) {
	    uint_64 offset = handle.Offset() + deep;
	    handle += 4;

	    uint_8  ub = (uint_8)(ul & 0x000000ff);
	    uint_32 n  = handle;
	    uint_16 m  = (uint_16)((n>>16)&0x0000ffff);
	    uint_8  l  = (uint_8) ((m>> 8)&0x00ff);

	    nl;
	    N("%8llu %02X", offset, ub);
	    switch (ub) {
	    case 0x00:		// Picture
		handle += 4;
		switch ((m>>3)&0x07) {
		case 1:  I_Frame = 'I'; break;
		case 2:  I_Frame = 'P'; break;
		case 3:  I_Frame = 'B'; break;
		case 4:  I_Frame = 'D'; break;
		default: I_Frame =  0 ; break;
		}
		N(" picture_header -");
		N(" vbv: %u,", (n >> 3) & 0x0000ffff);
		N(" temporal_reference: %4d, picture_coding_type: %c", m>>6 , I_Frame);
		break;
	    case 0x01 ... 0xaf:
		N(" slice");
		for (int i = 0; i < 20; i++) {
		    uint_32 n = handle;
		    if ((n & 0xffffff00) == 0x0000100 && n != ul)
			break;
		    N(" %02x", (uint_8)handle++);
		}
		break;
	    case 0xb2:		// User data
		N(" user_data_start");
		break;
	    case 0xb3:		// Sequence header
		N(" sequence_header");
		N(" height=%u width=%u", ((n&0xfff00000)>>20), ((n&0x000fff00)>> 8));
		handle += 3;
		l = handle;
		N(" aspect=%u", (l & 0xf0) >> 4);
		N(" rate=%u", (l & 0x0f));
		handle += 4;
		l = handle;
		handle++;
		if (l & 2) {
		    N(" intra_quantiser_matrix");
		    handle += 64;
		}
		if (l & 1) {
		    N(" non_intra_quantiser_matrix");
		    handle += 64;
		}
		break;
	    case 0xb4:		// Sequence error
		N(" sequence_error");
		break;
	    case 0xb5:		// Extension
		N(" extension_start");
		switch (l>>4) {
		case 1:
		    N(" (sequence_extension)");
		    handle +=  6;
		    break;
		case 2:
		    N(" (sequence_display_extension)");
		    l = handle;	
		    if (l & 1)
			handle += 8;
		    else
			handle += 5;
		    break;
		case 8:
		    N(" (picture_coding_extension)");
		    handle += 4;
		    l = handle;
		    if (l & 0x40)
			handle += 3;
		    else
			handle++;
		    break;
		default:
		    break;
		}
		break;
	    case 0xb7:		// Sequence end
		N(" sequence_end");
		break;
	    case 0xb8:		// Group of Pictures
		N(" group_of_pictures_header - time_code:");
		N(" %02u:%02u:%02u.%02u", (n>>26)&0x1f, (n>>20)&0x3f, (n>>13)&0x3f, (n>>7)&0x3f);
		N(" closed_gop: %d broken_link: %d", (n>>6)&0x01, (n>>5)&0x01);
		handle += 4;
		break;
	    default:
		N(" unkown or reserved %08x", ul);
		break;
	    }
	} else {
	    handle++;
	}
    } end (handle);
    return rest;
}

static off_t scan_c0(uint_8* const buf, const off_t rest, const uint_16 flags, const uint_64 deep, const int id)
{
    static const uint_32 samplerate_table[3] = {44100, 48000, 32000};
    static const uint_32 bitrate_table[5][15] = {
	// MPEG-1
	{0,  32000,  64000,  96000, 128000, 160000, 192000, 224000,
	    256000, 288000, 320000, 352000, 384000, 416000, 448000 },	// Layer I
	{0,  32000,  48000,  56000,  64000,  80000,  96000, 112000,
	    128000, 160000, 192000, 224000, 256000, 320000, 384000 },	// Layer II
	{0,  32000,  40000,  48000,  56000,  64000,  80000,  96000,
	    112000, 128000, 160000, 192000, 224000, 256000, 320000 },	// Layer III
	// MPEG-2 LSF
	{0,  32000,  48000,  56000,  64000,  80000,  96000, 112000,
	    128000, 144000, 160000, 176000, 192000, 224000, 256000 },	// Layer I
	{0,   8000,  16000,  24000,  32000,  40000,  48000,  56000,
	     64000,  80000,  96000, 112000, 128000, 144000, 160000 }	// Layer II&III
    };

    static sint_32 len[8];
    cHandle handle(buf, rest);
    bool aligned = (flags & 0x0400);

    if (id >= 8)
	goto out;

    if (aligned)
	len[id] = 0;

    if (len[id]) {
	test(handle) {
	    nl;
	    N("%8llu    mpeg audio rest=%d", deep+handle.Offset(), len[id]);
	    handle += len[id];
	    len[id] = 0;
	} end (handle);
    }

    foreach(handle >= 2) {		// See decode_header() of libmad
	uint_16 us = handle;
	char done = 0;
	bool ext   = ((us & 0x0010)>>4 == 0);
	bool lsf   = ((us & 0x0008)>>3 == 0);
	char layer = 4 - ((us & 0x0006)>>1);
	bool stop = false;

	if ((us & 0xffe0) != 0xffe0) {	// Sync word
	    handle++;			// Search for next frame
	    continue;
	}
	uint_64 offset = handle.Offset() + deep;

	if ((layer == 4) || (!lsf && ext)) {
	    handle++;			// Search for next frame
	    continue;
	}
#ifdef AUDIO_CRC
	bool crc = ((us & 0x0001) == 1);// Protection bit
#endif
	handle += 2;
	done   += 2;

	int index;
	uint_8 ub = handle;
	if ((index = (ub & 0x0c)) == 0x0c) {
	    handle++;			// Search for next frame
	    continue;
	}
	index >>= 2;

	uint_32 rate = samplerate_table[index];
	if (lsf) {
	    rate /= 2;
	    if (ext)
		rate /= 2;
	}

	if ((index = (ub & 0xf0)) == 0xf0)  {
	    handle++;			// Search for next frame
	    continue;
	}
	index >>= 4;

	uint_32 bitrate;
	if (lsf)
	    bitrate = bitrate_table[3 + (layer >> 1)][index];
	else
	    bitrate = bitrate_table[layer - 1][index];

	int padding = (ub & 0x02) >> 1;

	uint_16 size;
	uint_16 bytes;
	if (layer == 1) {
	    size  = 4*((12 * bitrate / rate) + padding);
	    bytes = 1536;
	} else {
	    const uint_16 slots = ((layer == 3) && lsf) ? 72 : 144;
	    size  = (slots * bitrate / rate) + padding;
	    bytes = 32 * slots;
	}

	uint_64 lpts;
	switch (rate) {
	case 48000:
	    lpts = ((15*bytes)/32);
	    break;
	case 44100:
	    lpts = ((75*bytes)/147);
	    break;
	case 32000:
	    lpts = ((45*bytes)/64);
	    break;
	default:
	    lpts = 0;
	    break;
	}
#ifdef AUDIO_CRC
	uint_16 target = 0;
	if (crc) {
	    handle += done;
	    done   += 2;
	    target  = handle;
	}
#endif
	nl;
	N("%8llu FF mpeg audio layer=%d, len=%d", offset, layer, size);
	if ((handle.Offset() - done) + size <= (uint_64)rest) {
	    handle += (size - done);
	} else {
	    len[id] = (rest - (handle.Offset() - done));
	    N("(%d)", len[id]);
	    len[id] = size - len[id];
	    stop = true;
	}
	N(", dur=%llums", lpts/90);
#ifdef AUDIO_CRC
	if (crc)
	    N(", crc=0x%04x", target);
#endif
	if (stop)
	    break;

    } end (handle);
out:
    return rest;
}

static off_t scan_bd(uint_8* const buf, const off_t rest, const uint_16 flags, const uint_64 deep)
{
    cHandle handle(buf, rest);
    bool substream = false;
    static bool plainac3 = false;		// true if DVB stream with plain AC3 frames in payload

    typedef struct _frmsize
    {
	uint_16 bit_rate;
	uint_16 frame_size[3];
    } frmsize_t;
    static const frmsize_t frmsizecod_tbl[64] = {
	{ 32  ,{64   ,69   ,96   } }, { 32  ,{64   ,70   ,96   } },
	{ 40  ,{80   ,87   ,120  } }, { 40  ,{80   ,88   ,120  } },
	{ 48  ,{96   ,104  ,144  } }, { 48  ,{96   ,105  ,144  } },
	{ 56  ,{112  ,121  ,168  } }, { 56  ,{112  ,122  ,168  } },
	{ 64  ,{128  ,139  ,192  } }, { 64  ,{128  ,140  ,192  } },
	{ 80  ,{160  ,174  ,240  } }, { 80  ,{160  ,175  ,240  } },
	{ 96  ,{192  ,208  ,288  } }, { 96  ,{192  ,209  ,288  } },
	{ 112 ,{224  ,243  ,336  } }, { 112 ,{224  ,244  ,336  } },
	{ 128 ,{256  ,278  ,384  } }, { 128 ,{256  ,279  ,384  } },
	{ 160 ,{320  ,348  ,480  } }, { 160 ,{320  ,349  ,480  } },
	{ 192 ,{384  ,417  ,576  } }, { 192 ,{384  ,418  ,576  } },
	{ 224 ,{448  ,487  ,672  } }, { 224 ,{448  ,488  ,672  } },
	{ 256 ,{512  ,557  ,768  } }, { 256 ,{512  ,558  ,768  } },
	{ 320 ,{640  ,696  ,960  } }, { 320 ,{640  ,697  ,960  } },
	{ 384 ,{768  ,835  ,1152 } }, { 384 ,{768  ,836  ,1152 } },
	{ 448 ,{896  ,975  ,1344 } }, { 448 ,{896  ,976  ,1344 } },
	{ 512 ,{1024 ,1114 ,1536 } }, { 512 ,{1024 ,1115 ,1536 } },
	{ 576 ,{1152 ,1253 ,1728 } }, { 576 ,{1152 ,1254 ,1728 } },
	{ 640 ,{1280 ,1393 ,1920 } }, { 640 ,{1280 ,1394 ,1920 } }
    };

    bool aligned = (flags & 0x0400);
    bool haspts  = (flags & 0x00C0);

    if (plainac3)			// Likely a singular DVB AC3 stream
	goto nosub;

    test(handle >= 4) {
	uint_32 ul = handle;
	uint_8  ub = (uint_8) (ul>>24);

	uint_8 c = (uint_8)((ul>> 16)&0x00ff);
	off_t  o = (off_t)(ul & 0x0000ffff) + 3;

	uint_16 m;
	uint_32 n;

	switch (ub) {
	case 0x20 ... 0x3f:		// Subpictures
	    handle++;
	    {
		static sint_32 len = 0;
		if (!aligned && !haspts && !len)
		    break;
		if (aligned) len = 0;
		if ((len == 0) || haspts) {
		    substream = true;
		    len = (uint_16)handle;
		    nl;
		    N("%8llu %02X", deep+handle.Offset()-1, (ub & 0xf8));
		    N(" substream(subpicture), len=%d spuh:", len);
		    for (int i = 0; i < 4; i++)
			 N(" %02x", (uint_8)handle++);
		    len -= (rest - 1);
		} else if (len) {
		    substream = true;
		    nl;
		    N("%8llu %02X", deep+handle.Offset()-1, (ub & 0xf8));
		    N(" substream(subpicture), rest=%d", len);
		    if ((len -= (rest - 1)) < 0)
			len = 0;
		}
	    }
	    break;
	case 0x80 ... 0x87:		// Dolby Digital Audio
	    if (o >= rest)
		break;
	    handle += o;
	    m = handle;
	    if (m == 0x0b77) {
		substream = true;
		nl;
		N("%8llu %02X", deep+handle.Offset()-o, (ub & 0xf8));
		N(" substream(AC3), stream: %u, count: %u, start: %lu", (ub & 7), c, o - 3);
	    }
	    break;
	case 0x88 ... 0x8f:		// DTS Audio
	    if (o+2 >= rest)
		break;
	    handle += o;
	    n = handle;
	    if (n == 0x7ffe8001) {
		substream = true;
		nl;
		N("%8llu %02X", deep+handle.Offset()-o, (ub & 0xf8));
		N(" substream(DTS), stream: %u, count: %u, start: %lu", (ub & 7), c, o - 3);
	    }
	    break;
	case 0xa0 ... 0xa7:		// Linear PCM Audio
	    if (o == 3)
		o = 7;
	    if (o >= rest)
		break;
	    {
		static sint_32 len = 0;
		if (!aligned && !haspts && !len)
		    break;
		if (aligned) len = 0;
		if ((len == 0) || haspts) {
		    substream = true;
		    len = (rest - o);
		    nl;
		    N("%8llu %02X", deep+handle.Offset(), (ub & 0xf8));
		    N(" substream(PCM), len=%d, flags:", len);
		    handle += 4;
		    for (int i = 0; i < 3; i++)
			 N(" %02x", (uint_8)handle++);
		} else if (len) {
		    substream = true;
		    len = (rest - o);
		    nl;
		    N("%8llu %02X", deep+handle.Offset(), (ub & 0xf8));
		    N(" substream(PCM), rest=%d, flags", len);
		    handle += 4;
		    for (int i = 0; i < 3; i++)
			 N(" %02x", (uint_8)handle++);
		}
	    }
	    break;
	default:
	    break;
	}
    } end (handle);

nosub:

    if (!substream) {			// Just DVB AC3 stream
	cHandle handle(buf, rest);

	test(handle >= 4) {
	static sint_32 len = 0;		// Set if DVB stream with not aligned AC3 frames in payload
	    sint_32 l = rest;
	    uint_16 m;
	    if (aligned) len = 0;
	    if (len) {
		off_t off = handle.Offset();
		handle += len;
		nl;			// If not on boundary a overroll may occured
		N("%8llu   ", deep+off);
		N(" plain(AC3) rest=%d", len);
		l -= len;
		len = 0;
	    }
	    while (l > 0) {
		m = handle;		// May throw an Exeption if no AC3 frame follows
		if (m == 0x0b77) {
		    uint_16 fscod, frmsizecod;
		    handle += 4;
		    plainac3 = true;

		    fscod	= (((uint_8)handle)>>6)&3;
		    frmsizecod	=  ((uint_8)handle)&0x3f;
		    len = 2*frmsizecod_tbl[frmsizecod].frame_size[fscod];

		    nl;
		    N("%8llu 0B", deep+handle.Offset()-4);
		    N(" plain(AC3) size=%d", len);
		    if (l < len) {
			N(" included=%u", l);
			len -= l;
			break;
		    }
		    l -= len;
		    handle += (len - 4);
		} else {
		    handle++;		// Scan for initial AC3 frame for not aligned payload
		    l--;
		}
	    }
	} end (handle);
    }

    return rest;
}
