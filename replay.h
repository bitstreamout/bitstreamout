/*
 * replay.h:	Forward AC3 stream to the S/P-DIF interface of
 *		sound cards supported by ALSA. (NO decoding!)
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
 * Copyright (C) 2002-2005 Werner Fink, <werner@suse.de>
 */

#ifndef __REPLAY_H
#define __REPLAY_H

#include <vdr/audio.h>
#include <vdr/thread.h>
#include "types.h"
#include "spdif.h"
#include "bounce.h"
#include "bitstreamout.h"

class cReplayOutSPDif : public cAudio, cThread {
private:
    volatile flags_t flags;
    // Private Stream 1 magic
    static const uint_32 PS1magic;
    // Stream detection
    iec60958* stream;
    cMutex ctr;
    // magic of streams
    static const uint_16 AC3magic;
    static const uint_32 DTSmagic;
    // Sync/Underrun thread
    #define FLAG_RUNNING	0		// Forwarding thread is running
    #define FLAG_ACTIVE		1		// Forwarding thread loop is running
    #define FLAG_FAILED		2		// We failed
    #define FLAG_BOUNDARY	3		// PES boundary detected
    #define FLAG_WASMUTED	4		// Previous muted
    // The PES scanner
    inline bool OffsetToDvdOfPS1(const uchar *const b, int &off, const int cnt, const uchar id);
    inline bool ScanPayOfPS1(const uchar *const b, int &off, const int cnt, const uchar id);
    inline bool DigestPayOfMP2(const uchar *const pstart, int &off, const int cnt, const uchar id);
    // Fast ring buffer
    static uint_8  * pesdata;
    static cBounce * bounce;
    cPsleep wait;
protected:
    const char *const SPDIFmute;
    spdif *const spdifDev;
    ctrl_t &setup;
    virtual void Action(void);
    virtual void Activate(bool onoff);	// We do NOT use cReceiver class
public:
    cReplayOutSPDif(spdif &dev, ctrl_t &up, cBounce * bPtr, const char *script);
    virtual ~cReplayOutSPDif(void);
    virtual void Play(const uchar *b, int cnt, uchar id);
    virtual void Mute(bool onoff);
    virtual void Clear(void);
};

#endif // __REPLAY_H
