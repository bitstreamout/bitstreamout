/*
 * cutter.c:	Cut the content of MPEG VDR recordings
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
 * Copyright (C) 2003 Werner Fink, <werner@suse.de>
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
#include "handle.h"

#define FRAMESPERSEC 25

int HMSFToIndex(const char *HMSF)
{
    register int h, m, s, f = 0;
    int ret = 0;
    if (3 <= sscanf(HMSF, "%d:%d:%d.%d", &h, &m, &s, &f))
	ret = (h * 3600 + m * 60 + s) * FRAMESPERSEC + f - 1;
    return ret;
}

// PTS (Present Time Stamps)
typedef struct _pts {
    uint_64 vid;
    sint_64 off;
    uint_64 aud[8];
    uint_64 ps1;
} pts_t;
static pts_t pts;

#define dsyslog(format, args...)	fprintf(stderr, "cutter: " format "\n", ## args)
#define esyslog(format, args...)	fprintf(stderr, "cutter: " format "\n", ## args)
#define O_PIPE				(O_NONBLOCK|O_ASYNC)

#define NO_PICTURE	0
#define I_FRAME		1
#define P_FRAME		2
#define B_FRAME		3

class cFrame {
private:
    uint_8  type;	// Type like 0x0e for video
    uint_8  picture;	// For video only: I/P/B frame
    uint_8 *data;
    uint_32 len;
public:
    cFrame(const uint_8 id, const uint_8 *const ptr, const uint_32 cnt, const uint_8 pt = NO_PICTURE)
    : type(0), data(NULL), len(0)
    {
	if (!(data = (uint_8*)malloc(sizeof(uint_8)*cnt))) {
	    esyslog("cFrame: can not alloc memory (%s)", strerror(errno));
	    return;
	}
	type = id;
	memcpy(data, ptr, cnt);
	len = cnt;
	picture = pt;
    }
    virtual ~cFrame(void)
    {
	if (data) free(data);
	data = NULL;
	len  = 0;
	type = 0;
    };
    virtual void Append(const uint_8 *const ptr, const uint_32 cnt)
    {
	uint_32 add = len + cnt;
	if (!(data = (uint_8*)realloc(data, sizeof(uint_8)*add))) {
	    esyslog("cFrame: can not extend memory (%s)", strerror(errno));
	    return;
	}
	memcpy(data+len, ptr, cnt);
	len = add;
    };
    virtual uint_8 Type(void) const { return type; };
};

template<class T> class cGrip {
    friend class cFrames;
private:
    cGrip *prev, *next;
    T* rivet;
public:
    cGrip(T* object = NULL) : prev(this), next(this), rivet(object) {};
};

class cPresentation {
    friend class cFrames;
private:
    cGrip<cPresentation> *grip;
    const uint_64 stamp;
    const sint_64 offset;		// For B frames pointing backwards
    cFrame *frame;
public:
    cPresentation(const uint_64 pts, const sint_64 off = 0)
	: grip(new cGrip<cPresentation>(this)), stamp(pts), offset(off), frame(NULL)
	{};
    virtual ~cPresentation()        { delete frame; frame = NULL; };
    virtual void Add  (cFrame *Frame) { frame = Frame; };
    virtual const uint_64 Stamp(void) const { return stamp; };
    virtual cFrame* Frame(void) const { return frame; };
};

class cFrames {
private:
    cGrip<cPresentation> *head;
public:
    cFrames () : head(new cGrip<cPresentation>) {};
    virtual ~cFrames ()  { Clear(); };
    void Put(cPresentation *Stamp, cPresentation *Here = NULL, bool before = false)
    {
	if (!Stamp)
	    return;
	cGrip<cPresentation> *here = ((!Here) ? head->prev : Here->grip);
	cGrip<cPresentation> *grip = Stamp->grip;

	cGrip<cPresentation> *prev, *next;
	if (before) {
	    prev = here->prev;
	    next = here;
	} else {
	    prev = here;
	    next = here->next;
	}

	next->prev = grip;
	grip->next = next;
	grip->prev = prev;
	prev->next = grip;
    };
    cPresentation *Get(cPresentation *Stamp, bool reverse = false)
    {
	cGrip<cPresentation> *grip = ((!Stamp) ? head : Stamp->grip);

	if (reverse)
	    grip = grip->prev;
	else
	    grip = grip->next;

	return grip->rivet;
    };
    void Drop(cPresentation *Stamp)
    {
	if (!Stamp)
	    return;
	cGrip<cPresentation> *grip = Stamp->grip;
	cGrip<cPresentation> *prev = grip->prev;
	cGrip<cPresentation> *next = grip->next;

	next->prev = prev;
	prev->next = next;
	delete Stamp;
	Stamp = NULL;
    };
    virtual void Clear(void)
    {
	class cPresentation *p = NULL;
	while ((p = Get(p)))
	    Drop(p);
    };
};

class cOut {
private:
    uint_32 iframes;
public:
    cFrames Ring, *Video, *Audio, *Digital;
    cOut() : Video(&Ring), Audio(&Ring), Digital(&Ring) {};
    void Put(const pts_t pts, const uint_8 ub, const uint_8 * ptr, const uint_32 cnt, const uint_8 pt = NO_PICTURE) {
	const int id = (ub & 0x1f);
	cPresentation * present;
	switch (ub) {
	case 0xe0:
	    if (pt == NO_PICTURE)
		break;
	    if ((present = new cPresentation(pts.vid, pts.off))) {
		cFrame *frame = new cFrame(ub, ptr, cnt, pt);
		if (frame) {
		    present->Add(frame);
		    Video->Put(present);
		    if (pt == I_FRAME)
			iframes++;
		} else
		    delete present;
	    }
	    break;
	case 0xc0 ... 0xdf:
	    if (id >= 8)
		break;
	    if ((present = new cPresentation(pts.aud[id]))) {
		cFrame *frame = new cFrame(ub, ptr, cnt);
		if (frame) {
		    present->Add(frame);
		    Audio->Put(present);
		} else
		    delete present;
	    }
	    break;
	case 0x80 ... 0x87:
	case 0x88 ... 0x8f:
	case 0xa0 ... 0xa7:
	    if ((present = new cPresentation(pts.ps1))) {
		cFrame *frame = new cFrame(ub, ptr, cnt);
		if (frame) {
		    present->Add(frame);
		    Digital->Put(present);
		} else
		    delete present;
	    }
	    break;
	default:
	    dsyslog("Put(): unknown id 0x%02X", ub);
	    break;
	}
    }
    void Add(const pts_t pts, const uint_8 ub, const uint_8 * ptr, const uint_32 cnt) {
	const int id = (ub & 0x1f);
	cPresentation * present = NULL;
	switch (ub) {
	case 0xe0:
	    while ((present = Video->Get(present,true))) {
		if (present->Stamp() == pts.vid) {
		    cFrame *frame = present->Frame();
		    if (frame && frame->Type() == ub) {
			frame->Append(ptr, cnt);
			break;
		    }
		}
	    }
	    break;
	case 0xc0 ... 0xdf:
	    if (id >= 8)
		break;
	    while ((present = Audio->Get(present,true))) {
		if (present->Stamp() == pts.aud[id]) {
		    cFrame *frame = present->Frame();
		    if (frame && frame->Type() == ub) {
			frame->Append(ptr, cnt);
			break;
		    }
		}
	    }
	    break;
	case 0x80 ... 0x87:
	case 0x88 ... 0x8f:
	case 0xa0 ... 0xa7:
	    while ((present = Digital->Get(present,true))) {
		if (present->Stamp() == pts.ps1) {
		    cFrame *frame = present->Frame();
		    if (frame && frame->Type() == ub) {
			frame->Append(ptr, cnt);
			break;
		    }
		}
	    }
	    break;
	default:
	    dsyslog("Add(): unknown id 0x%02X", ub);
	    break;
	}
    }
    void Pop(void)
    {
	if (iframes > 2) {
	    class cPresentation *p = NULL;
	    while ((p = Ring.Get(p))) {
		cFrame *frame = p->Frame();
		dsyslog("Frame 0x%02X %llu", frame->Type(), p->Stamp());
		Ring.Drop(p);
	    }
	}
    };
};

static class cOut Out;

// Signal handling
static struct sigaction saved_act[SIGUNUSED];
static void sighandler(int sig)
{
    sigset_t set;

    if (sig != SIGPIPE && sig != SIGIO)
	esyslog("Catch %s", strsignal(sig));

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
        printf("Usage: cutter <options> [file]\n");
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
		esyslog("strdup() failed: %s", strerror(errno));
		exit(1);
	    }
	    if (!(out = fopen(output, "a+"))) {
		esyslog("fopen(%s) failed: %s", output, strerror(errno));
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
			esyslog("Couldn't set NONBLOCK|ASYNC mode on stdin: %s", strerror(errno));
		}
	    }
	} else {
	    if (!*argv)
		break;
	    if ((fd = open(*argv, O_NOCTTY|O_RDONLY)) < 0) {
		esyslog("Couldn't open file %s: %s", *argv, strerror(errno));
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

// Used in scan for private stream data
#define BD_IS_ALIGNED	0x01
#define BD_HAS_PTSDTS	0x02
static off_t scan_bd(uint_8* const buf, const off_t rest, pts_t pts, const bool aligned);

// Used in scan for audio data
static off_t scan_c0(uint_8* const buf, const off_t rest, const uint_8 ub, pts_t pts, const bool aligned);

// Used in scan for video data
static off_t scan_e0(uint_8* const buf, const off_t rest, const uint_8 ub, pts_t pts);

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

	    switch (ub) {
	    case 0xb9:		// Program end (terminates a program stream)
		N("%8llu %02X", offset, ub);
		D(" program_end");
		break;
	    case 0xba:		// Pack header
		N("%8llu %02X", offset, ub);
		if (((l>>2) & 0x01) != 1 && (l>>6) != 1)
		    break;
		Header = true;
		I_Frame = 0;
		N(" pack_header");
		for (int i = 0; i < 10; i++)
		     N(" %02x", (uint_8)handle++);
		nl;
		break;
	    case 0xbb:		// System Header
		N("%8llu %02X", offset, ub);
		handle += 2;
		N(" system_header");
		N(", len=%d", m+6);
		N(", ext:");
		for (int i = 0; i < m && i < 20; i++)
		     N(" %02x", (uint_8)handle++);
		nl;
		break;
	    case 0xbc:		// Program Stream Map
		N("%8llu %02X", offset, ub);
		D(" program_stream_map");
		break;
	    case 0xbd:		// Private stream 1
		handle += 2;
		{
		    uint_8 *p;
		    uint_8  off;
		    uint_16 b = 0;
		    bool aligned = false;

		    if (m < 2)
			break;
		    if (m > handle.len())
			m = handle.len();
		    if (!(p = handle(m)))
			break;
		    if (m < (off = p[2] + 3))
			break;

		    if (p[0]&0x04 || p[1] & 0xC0)
			aligned = true;

		    b = handle;
		    if ((b & 0xC000) != 0x8000) {
			N(" (broken)");
			nl;
			break;
		    }

		    if ((p[1] & 0x80) && (p[2] >= 5)) {
			uint_64 lpts = 0;
			lpts  = (((uint_64)p[3]) & 0x0e) << 29;
			lpts |= ( (uint_64)p[4])         << 22;
			lpts |= (((uint_64)p[5]) & 0xfe) << 14;
			lpts |= ( (uint_64)p[6])         <<  7;
			lpts |= (((uint_64)p[7]) & 0xfe) >>  1;
			if (pts.ps1) {
			    sint_64 diff = lpts - pts.ps1;
			    if (diff > 0)
				pts.ps1 = lpts;
			} else
			    pts.ps1 = lpts;
		    }

		    if (pts.ps1)
			scan_bd(p+off, m-off, pts, aligned);
		    handle += m;
		}
		break;
	    case 0xbe:		// Padding stream
		N("%8llu %02X", offset, ub);
		handle += 2;
		l = handle;
		if (l != 0xff)
		    break;
		handle += m;
		D(" padding_stream, len=%d", m+6);
		break;
	    case 0xbf:		// Private stream 2
		N("%8llu %02X", offset, ub);
		handle += (2+m);
		D(" private_stream_2, len=%d", m+6);
		break;
	    case 0xc0 ... 0xdf:	// MPEG-1 or MPEG-2 audio stream
		handle += 2;
		{
		    uint_8  *p;
		    uint_8  off;
		    int id = (ub & 0x1f);
		    bool aligned = false;

		    if (m < 2)
			break;
		    if (m > handle.len())
			m = handle.len();
		    if (!(p = handle(m)))
			break;
		    if (m < (off = p[2] + 3))
			break;

		    if (p[0]&0x04 || p[1] & 0xC0)
			aligned = true;

		    if ((p[1] & 0x80) && (p[2] >= 5)) {
			uint_64 lpts = 0;
			lpts  = (((uint_64)p[3]) & 0x0e) << 29;
			lpts |= ( (uint_64)p[4])         << 22;
			lpts |= (((uint_64)p[5]) & 0xfe) << 14;
			lpts |= ( (uint_64)p[6])         <<  7;
			lpts |= (((uint_64)p[7]) & 0xfe) >>  1;
			if (id < 8) {
			    if (pts.aud[id]) {
				sint_64 diff = lpts - pts.aud[id];
			    if (diff > 0)
				pts.aud[id] = lpts;
			    } else
				pts.aud[id] = lpts;
			}
		    }

		    if (id < 8 && pts.aud[id])
			(void)scan_c0(p+off, m-off, ub, pts, aligned);
		    handle += m;
		}
		break;
	    case 0xe0 ... 0xef:	// MPEG-1 or MPEG-2 video stream
		handle += 2;
		{
		    uint_8 *p;
		    uint_8 off;

		    if (m < 2)
			break;
		    if (m > handle.len())
			m = handle.len();
		    if (!(p = handle(m)))
			break;
		    if (m < (off = p[2] + 3))
			break;

		    if ((p[1] & 0x80) && (p[2] >= 5)) {
			uint_64 lpts = 0;
			lpts  = (((uint_64)p[3]) & 0x0e) << 29;
			lpts |= ( (uint_64)p[4])         << 22;
			lpts |= (((uint_64)p[5]) & 0xfe) << 14;
			lpts |= ( (uint_64)p[6])         <<  7;
			lpts |= (((uint_64)p[7]) & 0xfe) >>  1;
			pts.off = 0;
			if (pts.vid) {
			    sint_64 diff = lpts - pts.vid;
			    if (diff > 0)
				pts.vid = lpts;
			    else
				pts.off = diff;
			} else
			    pts.vid = lpts;
		    }

		    if (pts.vid)
			scan_e0(p+off, m-off, ub, pts);
		    handle += m;
		}
		break;
	    case 0xf0:		// ECM Stream
		N("%8llu %02X", offset, ub);
		D(" ECM Stream");
		break;
	    case 0xf1:		// EMM Stream
		N("%8llu %02X", offset, ub);
		D(" EMM Stream");
		break;
	    case 0xf2:		// ITU-T Rec. H.222.0
				// ISO/IEC 13818-1 Annex A or ISO/IEC 13818-6 DSMCC stream
		N("%8llu %02X", offset, ub);
		D(" ITU-T Rec. H.222.0 | ISO/IEC 13818-1 | ISO/IEC 13818-6 DSMCC");
		break;
	    case 0xf3:		// ISO/IEC_13522_stream
		N("%8llu %02X", offset, ub);
		D(" ISO/IEC_13522_stream");
		break;
	    case 0xf4:		// ITU-T Rec. H.222.1 type A
		N("%8llu %02X", offset, ub);
		D(" ITU-T Rec. H.222.1 type A");
		break;
	    case 0xf5:		// ITU-T Rec. H.222.1 type B
		N("%8llu %02X", offset, ub);
		D(" ITU-T Rec. H.222.1 type B");
		break;
	    case 0xf6:		// ITU-T Rec. H.222.1 type C
		N("%8llu %02X", offset, ub);
		D(" ITU-T Rec. H.222.1 type C");
		break;
	    case 0xf7:		// ITU-T Rec. H.222.1 type D
		N("%8llu %02X", offset, ub);
		D(" ITU-T Rec. H.222.1 type D");
		break;
	    case 0xf8:		// ITU-T Rec. H.222.1 type E
		N("%8llu %02X", offset, ub);
		D(" ITU-T Rec. H.222.1 type E");
		break;
	    case 0xf9:		// ancillary_stream
		N("%8llu %02X", offset, ub);
		D(" ancillary_stream");
		break;
	    case 0xff:		// Program Stream Directory
		N("%8llu %02X", offset, ub);
		D(" program_stream_directory");
		break;
	    default:
		N("%8llu %02X", offset, ub);
		D(" unkown or reserved %08x", ul);
		break;
	    }
	} else {
	    handle++;
	}
	Out.Pop();
    } end (handle);
};

static off_t scan_e0(uint_8* const buf, const off_t rest, const uint_8 ub, pts_t pts)
{
    cHandle handle(buf, rest);
    uint_8*  p = handle(rest);

    foreach(handle >= 4) {
	uint_32 ul = handle;
	if ((ul & 0xffffff00) == 0x0000100) {
	    handle += 4;

	    uint_8  b = (uint_8)(ul & 0x000000ff);
	    uint_32 n = handle;
	    uint_16 m = (uint_16)((n>>16)&0x0000ffff);
	    uint_8  l = (uint_8) ((m>> 8)&0x00ff);
	    uint_8  i;

	    switch (b) {
	    case 0x00:		// Picture
		handle += 4;
		switch ((i = (m>>3)&0x07)) {
		case 1:
		case 2:
		case 3:
		    Out.Put(pts, ub, p, rest, i);
		    goto out;
		case 4:
		default:
		    goto out;
		}
		break;
	    case 0x01 ... 0xaf:
		Out.Add(pts, ub, p, rest);
		goto out;
		break;
	    case 0xb2:		// User data
		break;
	    case 0xb3:		// Sequence header
		handle += 11;
		break;
	    case 0xb4:		// Sequence error
		break;
	    case 0xb5:		// Extension
		switch (l>>4) {
		case 1:
		    handle +=  6;
		    break;
		case 2:
		    handle++;
		    l = handle;	
		    if (l & 1)
			handle += 7;
		    else
			handle += 4;
		    break;
		case 8:
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
		break;
	    case 0xb8:		// Group of Pictures
		break;
	    default:
		N(" unkown or reserved %08x", ul);
		break;
	    }
	} else {
	    handle++;
	}
    } end (handle);
out:
    return rest;
}

static off_t scan_c0(uint_8* const buf, const off_t rest, const uint_8 ub, pts_t pts, const bool aligned)
{
    static const uint_32 samplerate_table[3] = {44100, 48000, 32000};
    static sint_32 len[8];
    static uint_32 off[8];
    cHandle handle(buf, rest);
    int id = (ub & 0x1f);

    if (id >= 8)
	goto out;

    if (aligned)
	len[id] = 0;

    if (len[id]) {
	test(handle) {
	    uint_8 *p = handle(len[id]);
	    pts.aud[id] -= off[id];
	    Out.Add(pts, ub, p, len[id]);
	    pts.aud[id] += off[id];
	    handle += len[id];
	    len[id] = 0;
	    off[id] = 0;
	} end (handle);
    }

    foreach(handle >= 2) {		// See decode_header() of libmad
	uint_16 us = handle;
	uint_8 *p = handle(handle.len());
	char done = 0;
	bool ext   = ((us & 0x0010)>>4 == 0);
	bool lsf   = ((us & 0x0008)>>3 == 0);
	char layer = 4 - ((us & 0x0006)>>1);
	bool stop = false;

	if ((us & 0xffe0) != 0xffe0) {	// Sync word
	    handle++;			// Search for next frame
	    continue;
	}
	uint_16 bytes  = 16 * ((layer == 1) ? 12 : (((layer == 3) && lsf) ? 18 : 36));

	if ((layer == 4) || (!lsf && ext)) {
	    handle++;			// Search for next frame
	    continue;
	}
	handle += 2;
	done   += 2;

	uint_8 b = handle;
	if ((b & 0xf0) >> 4 == 15)  {	// Bit rate index
	    handle++;			// Search for next frame
	    continue;
	}
	if ((b & 0x0c) >> 2 == 3) {	// Sample rate index
	    handle++;			// Search for next frame
	    continue;
	}

	uint_32 rate = samplerate_table[(b & 0x0c) >> 2];
	if (lsf) {
	    rate /= 2;
	    if (ext)
		rate /= 2;
	}
	uint_32 lpts = ((2*bytes)*90*1000)/rate;

	if ((handle.Offset() - done) + bytes <= (uint_64)rest) {
	    handle += (bytes - done);
	    Out.Put(pts, ub, p, bytes);
	} else {
	    len[id] = (rest - (handle.Offset() - done));
	    Out.Put(pts, ub, p, len[id]);
	    len[id] = bytes - len[id];
	    stop = true;
	}
	pts.aud[id] += lpts;
	off[id] = lpts;

	if (stop)
	    break;

    } end (handle);
out:
    return rest;
}

// Used to scan digital sub frames
static off_t scan_ac3(uint_8* const buf, const off_t rest, pts_t pts, const bool aligned, const uint_8 ub);

static off_t scan_bd(uint_8* const buf, const off_t rest, pts_t pts, const bool aligned)
{
    cHandle handle(buf, rest);
    bool substream = false;

    test(handle >= 4) {
	uint_32 ul = handle;
	uint_8  ub = (uint_8) (ul>>24);

#if 0
	uint_8 c = (uint_8)((ul>> 16)&0x00ff);   // In this frame `c' sub frames starts
#endif
	off_t  o = (off_t)(ul & 0x0000ffff) + 3;

	uint_16 m;
	uint_32 n;

	switch (ub) {
	case 0x20 ... 0x3f:	// Subpictures
	    handle++;
	    {
		static sint_32 len = 0;
		if (!aligned && !len)
		    break;
		if (aligned) {
		    substream = true;
		    len = (uint_16)handle;
		    len -= (rest - 1);
		    goto out;
		} else if (len) {
		    substream = true;
		    if ((len -= (rest - 1)) < 0)
			len = 0;
		    goto out;
		}
	    }
	    break;
	case 0x80 ... 0x87:	// Dolby Digital Audio
	    if (o >= rest)
		break;
	    handle += o;
	    m = handle;
	    if (m == 0x0b77) {
		substream = true;
		scan_ac3(handle(rest-o), rest-o, pts, aligned, ub);
		goto out;
	    }
	    break;
	case 0x88 ... 0x8f:	// DTS Audio
	    if (o+2 >= rest)
		break;
	    handle += o;
	    n = handle;
	    if (n == 0x7ffe8001) {
		substream = true;
		goto out;
	    }
	    break;
	case 0xa0 ... 0xa7:	// Linear PCM Audio
	    if (o == 3)
		o = 7;
	    if (o >= rest)
		break;
	    {
		static sint_32 len = 0;
		if (!aligned && !len)
		    break;
		if (aligned) {
		    substream = true;
		    len = (rest - o);
		    handle += 6;
		    goto out;
		} else if (len) {
		    substream = true;
		    len = (rest - o);
		    handle += 6;
		    goto out;
		}
	    }
	    break;
	default:
	    break;
	}
    } end (handle);

    if (!substream)			// Just DVB AC3 stream
	scan_ac3(buf, rest, pts, aligned, 0x80);
out:
    return rest;
}


static off_t scan_ac3(uint_8* const buf, const off_t rest, pts_t pts, const bool aligned, const uint_8 ub)
{
    cHandle handle(buf, rest);

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
    static const uint_32 ptscod_tbl[4] = { 2880, 3134, 4320, 0 };

    test(handle >= 4) {
	static sint_32 len = 0;
	static uint_32 lpts = 0;
	sint_32 l = rest;
	if (aligned || len) {
	    if (aligned)
		len = 0;		// On boundary normally AC3 starts
	    if (len) {
		uint_8 *p = handle(len);
		pts.ps1 -= lpts;
		Out.Add(pts, ub, p, len);
		pts.ps1 += lpts;
		handle += len;
		l -= len;
		lpts = 0;
	    }
	    while (l > 0) {
		uint_16 m = handle;
		uint_8 *p = handle(l);
		if (m == 0x0b77) {
		    uint_16 fscod, frmsizecod;
		    handle += 4;

		    fscod	= (((uint_8)handle)>>6)&3;
		    frmsizecod	=  ((uint_8)handle)&0x3f;
		    len = 2*frmsizecod_tbl[frmsizecod].frame_size[fscod];

		    lpts = ptscod_tbl[fscod];

		    if (l < len) {
			len -= l;
			Out.Put(pts, ub, p, l);
			pts.ps1 += lpts;
			break;
		    }
		    l -= len;
		    handle += (len - 4);
		    Out.Put(pts, ub, p, len);
		    pts.ps1 += lpts;
		} else {
		    len = 0;
		    break;
		}
	    }
	}
    } end (handle);

    return rest;
}
