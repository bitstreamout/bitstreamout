/*
 * pts.h:	P(resentation) T(ime) S(tamps) handling
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
 */
#ifndef __PTS_H
#define __PTS_H

#ifndef _POSIX_SOURCE
# define _POSIX_SOURCE
#endif
#include <sys/time.h>
#include <unistd.h>
#include <vdr/config.h>
#include <vdr/device.h>
#include "types.h"

//#define DEBUG_PTS
#if defined(DEBUG) && !defined(DEBUG_PTS)
# define DEBUG_PTS
#endif
#ifdef  DEBUG_PTS
# define debug_pts(args...) fprintf(stderr, args)
#else
# define debug_pts(args...)
#endif
#define STC_STATERR	(~0U/90U)

enum ePTSstate { eOK = 0, eMISSED = -1, eDOSKIP = 1 };

class cPTS {
private:
    uint_32 pts;
    uint_32 stc;
    off_t   off;
    off_t   min;
    struct timeval expire, syncstc, current, previous;
    uint_8  err;
    bool AVsync, Started, Synching, HasPTS;

    inline uint_32 PTSinMS(const uint_8 *pts)
    {
	return (uint_32)
	((   ((uint_64)(pts[0] & 0x0E) << 29)
	   | ((uint_64)(pts[1])        << 22)
	   | ((uint_64)(pts[2] & 0xFE) << 14)
	   | ((uint_64)(pts[3])        <<  7)
	   | ((uint_64)(pts[4] & 0xFE) >>  1))/90ULL);
    }

    inline void timermark(void)
    {
        if (!timerisset(&previous))
	    gettimeofday(&previous, NULL);
	if (!timerisset(&expire)) {
	    static const struct timeval add = {2, 0};
	    timeradd(&previous, &add, &expire);
	}
	if (!timerisset(&syncstc)) {
	    static const struct timeval add = {0, 480*1000};
	    timeradd(&previous, &add, &syncstc);
	}
    }

    inline bool timerexpired(void)
    {
	if (timerisset(&current)) {
	    previous.tv_sec  = current.tv_sec;
	    previous.tv_usec = current.tv_usec;
	}
	gettimeofday(&current, NULL);
	if (!timerisset(&expire)) {
	    debug_pts("timerexpired() set expire\n");
	    static const struct timeval add = {2, 0};
	    timeradd(&current, &add, &expire);
	    return false;
	}
	return timercmp(&current, &expire, >);
    }

    inline bool timersyncstc(void)
    {
	if (timerisset(&current)) {
	    previous.tv_sec  = current.tv_sec;
	    previous.tv_usec = current.tv_usec;
	}
	gettimeofday(&current, NULL);
	if (!timerisset(&syncstc)) {
	    debug_pts("timersyncstc() set syncstc\n");
	    static const struct timeval add = {0, 480*1000};
	    timeradd(&current, &add, &syncstc);
	    return false;
	}
	return timercmp(&current, &syncstc, >);
    }

    inline bool  dvbtime (void)
    {
	cDevice* PrimaryDevice = cDevice::PrimaryDevice();
	sint_64 ret;

	debug_pts("dvbtime in\n");

	if (!PrimaryDevice) {	// No Device, always wrong
	    err++;		// due missed reference
	    goto out;
	}

	if ((ret = PrimaryDevice->GetSTC()) < 0) {
	    err++;		// Device error
	    goto out;
	}
	stc = (uint_32)(((uint_64)ret)/90ULL);

	//
	// The av711x DVB card/driver seems to have trouble
	// with large values (missing 33th bit aka > ~0U)
	//
	if (pts > STC_STATERR && stc < STC_STATERR+10000)
	    stc += STC_STATERR;

	//
	// We have following cases
	//   off >  0 Earlier (we may play silent with duration `off')
	//   off <  0 Late    (we're in trouble, no time back)
	//   off == 0 Fit     (Strike)
	//
	off = pts - stc;
	debug_pts("dvbtime PTS=%u STC=%u off=%ld\n", pts, stc, off);

	return true;
    out:
	off = -1000;
	debug_pts("dvbtime out bad\n");

	return false;
    };

    //
    // timersyncstc() or timerexpired() must be called first
    //
    inline bool dvbAVsync (void)
    {
	struct timeval sub;
	off_t diff, delay;

	debug_pts("dvbAVsync in\n");

	if (AVsync) {
	    if (!dvbtime())
		goto err;
	    debug_pts("AVsync STC=%d\n", stc);
	    goto out;
	}

	timersub(&current, &previous, &sub);
	delay = (sub.tv_sec*1000 + sub.tv_usec/1000);

	diff = stc;		// Previous STC value

	if (!dvbtime())		// Fetch new STC value from DVB
	    goto err;

	if ((diff = stc - diff) < 0)
	    goto err;		// DVB AV are definitly not in sync

	//
	// The DVB STC delay should be in sync with
	// The system clock delay (at least ±1ms).
	//
	debug_pts("dvbAVsync dSTC=%ld, dCLOCK=%ld\n", diff, delay);
	if ((diff < delay - 2) || (diff > delay + 2))
	    goto err;

	//
	// Assume that DVB STC clock is now in sync with
	// the System clock.
	//
	AVsync = true;
    out:
	debug_pts("dvbAVsync out true\n");
	return true;
    err:
	debug_pts("dvbAVsync out bad\n");

	return false;
    };

    inline off_t less(void)
    {
	off_t ret = min;
	if (min >= 100)
	    min -= 20;
	else
	    min -= 10;
	return ret;
    };

public:
    inline bool  mark(const uint_8 *buf = (const uint_8 *)0)
    {
	if (buf) {
	    pts = PTSinMS(buf);
	    HasPTS = true;
	} else if (!HasPTS && timersyncstc())
	    return true;		// Do not hanging around
	return HasPTS;
    };
    inline void  lead(const uint_32 t)
    {
	timerclear(&expire);
	err = 0;
	min = 200;
	Synching = false;
    };
    inline void  reset(void)
    {
	pts = stc = 0;
	timerclear(&current);
	timerclear(&expire);
	timerclear(&syncstc);
	timerclear(&previous);
	err = 0;
	off = -1000;
	min = 200;
	AVsync = false;
	Started = false;
	Synching = false;
	HasPTS = false;
    };
    inline const bool synch(bool s = false)
    {
	if (s) Synching = true;
	return Synching;
    };

    inline bool  stcsync(bool h = false)
    {
	if (h) HasPTS = true;		// Do not hanging around later

	if (AVsync) {
	    debug_pts("stcsync AV sync true\n");
	    goto out;
	}

	if (!Started) {			// Timer should be started
	    timermark();
	    Started = true;		// Timer started
	}

	if (timersyncstc()) {
	    debug_pts("stcsync timersyncstc()\n");
	    goto out;			// Timer expired
	}

	if (!dvbAVsync()) {
	    debug_pts("stcsync !dvbAVsync()\n");
	    goto bad;
	}
    bad:
	return false;
    out:

	return true;
    };

    inline bool  dvbcheck(const uint_32 delay)
    {
	debug_pts("dvbcheck in\n");

	if (err > 10)
	    goto out;			// Ignore continuing error

	if (pts == 0) {
	    debug_pts("dvbcheck pts == 0, HasPTS = %d\n", HasPTS);
	    off = 0;
	    goto out;			// What should we compare?
	}

	if (timerexpired()) {
	    debug_pts("dvbcheck timerexpired()\n");
	    goto out;			// Timer expired
	}

	if (!dvbAVsync()) {
	    debug_pts("dvbcheck !dvbAVsync()\n");
	    goto bad;
	}

	if (off >= less()) {		// 10ms which is 33cm
	    timerclear(&expire);
	    goto out;			// Success
	}
    bad:
	pts += delay;			// Next frame starts with this PTS
	debug_pts("dvbcheck out bad\n");
	return false;
    out:
	debug_pts("dvbcheck out true\n");

	return true;
    };

    inline off_t delay(off_t ignore)
    {
	debug_pts("delay (abs(off=%ld) > ignore=%ld) == %d\n", off, ignore, (abs(off) > ignore));
	if (abs(off) > ignore)
	    off = 0;
	return off;
    };
};
#endif
