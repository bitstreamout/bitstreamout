/*
 * replay.c:	Forward AC3 stream to the S/P-DIF interface of
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
 * Copyright (C) 2003 Sven Goethel, <sven@jausoft.com>
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include "types.h"
#include "bytes.h"
#include "replay.h"
#include "channel.h"
#include "ac3.h"
#include "dts.h"
#include "lpcm.h"
#include "mp2.h"

// --- cReplayOutSPDif : Forward AC3 stream to S/P-DIF of a sound card with ALSA -----------

const uint_32 cReplayOutSPDif::PS1magic = 0x000001bd;
const uint_16 cReplayOutSPDif::AC3magic = 0x0b77;
const uint_32 cReplayOutSPDif::DTSmagic = 0x7ffe8001;
uint_8  * cReplayOutSPDif::pesdata;
cBounce * cReplayOutSPDif::bounce;

cReplayOutSPDif::cReplayOutSPDif(spdif &dev, ctrl_t &up, cBounce * bPtr, const char *script)
:cAudio(), cThread("bso(replay): Forwarding bitstream"),
 ctr(), wait(), SPDIFmute(script), spdifDev(&dev), setup(up)
{
    flags = 0;
    ctr.Lock();
    stream = NULL;
    ctr.Unlock();
    bounce = bPtr;
    bounce->bank(0);
    pesdata = setup.buf + TRANSFER_START;
}

cReplayOutSPDif::~cReplayOutSPDif(void)
{
    Clear();
}

void cReplayOutSPDif::Activate(bool onoff)
{
    if (test_setup(CLEAR))
	onoff = false;

    if (onoff) {
	if (*spdifDev) {
	    debug("Activated failed due S/P-DIF already open\n");
	    goto out;				// An other has opened the S/P-DIF
	}
	if (test_setup(RESET)) {
	    clear_flag(FAILED);
	    clear_setup(RESET);
	}
	if (test_flag(FAILED) || test_flag(ACTIVE)) {
	    debug("Activated failed due set %s\n", test_flag(FAILED) ? "FAILED" : "ACTIVE");
	    goto out;
	}
	set_flag(ACTIVE);
	bounce->flush();
	Start();				// Warning: Start() sleeps 10ms after work
	debug("Activated\n");
    } else {
	int n = 50;				// 500 ms

	clear_flag(ACTIVE);			// Set only above
	do {
	    if (bounce) {
		bounce->flush();
		bounce->signal();
	    }
	    wait.msec(10);			// More than 2ms
	    if (!Active())
		break;
	} while (test_flag(RUNNING) && (n-- > 0));
	Cancel(1);

	if (test_flag(RUNNING) || Active()) {
	    esyslog("REPLAY: Forwarding bitstream thread was broken");
	    wait.msec(10);			// More than 2ms
	    ctr.Lock();
	    iec60958* curr = stream;
	    ctr.Unlock();
	    if (curr) {				// thread broken
		spdifDev->Close();
		clear_flag(BOUNDARY);
		curr->Reset();
		if (bounce)
		    bounce->leaveio();
            }
	    clear_flag(RUNNING);		// Should not happen
	}

	ctr.Lock();
	stream = NULL;
	ctr.Unlock();
	debug("DeActivated\n");
    }
out:
    return;
}

void cReplayOutSPDif::Action(void)
{
    if (test_setup(CLEAR))
	return;

    set_flag(RUNNING);
    ctr.Lock();
    iec60958 *curr = stream;
    ctr.Unlock();

    realtime("REPLAY: ");
    if (!curr) {
	esyslog("REPLAY: no stream for spdif interface");
	set_flag(FAILED);
	goto out;
    } else {
        if (!spdifDev->Open(curr, this)) {
	    esyslog("REPLAY: can't open spdif interface");
	    set_flag(FAILED);
	    goto out;
	}
    }

    bounce->takeio();
    while (test_flag(ACTIVE)) {
	ssize_t len;

	if (test_setup(MUTE)) {
	    if (!test_flag(WASMUTED)) {
		spdifDev->Clear();
		clear_flag(BOUNDARY);
		bounce->flush();
		curr->Clear();
	    }
	    set_flag(WASMUTED);
	}

	if (!spdifDev->Synchronize(bounce))	// Wait on bounce->signal()
	    continue;				// No break on large delay gaps

	if (!test_flag(ACTIVE))
	    break;

	if (test_setup(MUTE)) {
	    if (!test_flag(WASMUTED)) {
		spdifDev->Clear();
		clear_flag(BOUNDARY);
		bounce->flush();
		curr->Clear();
	    }
	    set_flag(WASMUTED);
	    continue;
	}

	if ((len = bounce->fetch(pesdata, spdifDev->Available(TRANSFER_MEM))))
	    spdifDev->Forward(pesdata, len, bounce);

    }
    spdifDev->Close(this);
    clear_flag(BOUNDARY);
    curr->Reset();
out:
    ctr.Lock();
    stream = NULL;
    ctr.Unlock();
    if (bounce) {
	bounce->flush();
	bounce->leaveio();
    }
    clear_flag(ACTIVE);
    clear_flag(RUNNING);
}

inline bool cReplayOutSPDif::OffsetToDvdOfPS1(const uchar *const b, int &off, const int cnt, const uchar id)
{
    const off_t rest = cnt-off;
    bool ret = false;				// DVB streams do not have sub stream headers
    cHandle dvd(&b[off], rest);
    ctr.Lock();
    iec60958* curr = stream;
    ctr.Unlock();

    TEST(dvd >= (size_t)4) {
	uint_32 ul = dvd;
	uint_8  ub = (uint_8)(ul>>24);
	uint_8  tr = (ub & 0x1F) + 0x21;
	off_t   o  = (off_t) (ul & 0x0000ffff) + 3;

	unsigned int sample_rate;
	uint_8 m;

	switch (ub) {
	case 0x80 ... 0x87:			// AC3 from e.g. DVD
	    if (curr != &ac3 || curr->track != tr)
		break;
	    if (id != ub)
		break;
	    ret = true;				// found, success
	    off += 4;
	    break;
	case 0x88 ... 0x8f:			// DTS
	    if (curr != &dts || curr->track != tr)
		break;
	    if (id != ub)
		break;
	    ret = true;				// found, success
	    off += 4;
	    break;
	case 0xa0 ... 0xa7:			// Linear PCM Audio
	    if (o == 3) {			// broken stream
		dvd++;
		uint_32 ui = dvd;
		dvd += 4;
		m = dvd;
		if ((ui == 0) && (m == 0)) {
		    off = cnt;
		    break;			// Don't support broken streams (dvd plugin).
		}
	    } else {
		dvd += 5;
		m = dvd;
	    }
	    if (curr != &pcm || curr->track != tr)
		break;
	    if (id != ub)
		break;
	    switch (m & 0x30) {
	    case 0x00: sample_rate = 48000;
		break;
	    case 0x20: sample_rate = 44100;
		break;
	    case 0x30: sample_rate = 32000;
		break;
	    case 0x10:				// 96kHz  currently not supported
	    default:   sample_rate = 0;
		break;
	    }
	    if (curr->sample_rate != sample_rate)
		break;
	    if (rest < 7)
		break;
	    ret = true;				// found, success
	    off += 7;
	    break;
	default:				// Should not happen, e.g. stream
	    break;				// is broken. Should we reset???
	}
    } END (dvd);

    return ret;
}

inline bool cReplayOutSPDif::ScanPayOfPS1(const uchar *const b, int &off, const int cnt, const uchar id)
{
    const off_t rest = cnt-off;
    cHandle dvb(&b[off], rest);
    ctr.Lock();
    iec60958* curr = stream;
    ctr.Unlock();

    if (curr)
	goto out;

    TEST(dvb >= (size_t)4) {
	// byte0	(substream number)
	// byte1	(Number of frames which begin in this package)
	// byte2, byte3 (offset to frame which corresponds to PTS value)
	uint_32 ul = dvb;
	uint_8  ub = (uint_8)(ul>>24);
	uint_8  tr = (ub & 0x1F) + 0x21;
	off_t   o  = (off_t) (ul & 0x0000ffff);

	bool isDVD = false;
	uint_8 m;

	switch (ub) {
	case 0x80 ... 0x87:			// AC3 from e.g. DVD
	    if (o == 0) o = 1;			// broken audio sub header
	    o += 3;
	    if (o >= rest)			// word magic
		break;
	    if (id != ub)
		break;
	    dvb += o;
	    off += o;
	    isDVD = true;
	case 0x0b:
	    if ((uint_16)dvb != AC3magic)
		break;
	    curr = &ac3;
	    clear_setup(AUDIO);
	    curr->Reset(setup.flags);
	    curr->isDVD = isDVD;
	    curr->track = ((isDVD) ? tr : 0x21);
	    goto out;				// found, go out
	case 0x88 ... 0x8f:			// DTS
	    if (o == 0) o = 1;			// broken audio sub header
	    o += 3;
	    if (o+2 >= rest)			// double word magic
		break;
	    if (id != ub)
		break;
	    dvb += o;
	    off += o;
	    isDVD = true;
	case 0x7f:
	    if ((uint_32)dvb != DTSmagic)
		break;
	    curr = &dts;
	    clear_setup(AUDIO);
	    curr->Reset(setup.flags);
	    curr->isDVD = isDVD;
	    curr->track = ((isDVD) ? tr : 0x21);
	    goto out;				// found, go out
	case 0xa0 ... 0xa7:			// Linear PCM Audio
	// byte4	(emphasis, mute, reserved, 4-0: frame number modulo 20)
	// byte5	(7-6: Quantization, 5-4: Sample rate, reserved, 2-0: channels-1)
	// byte6	(Dynamic range)
	    if (o == 3) {			// broken stream
		dvb++;
		uint_32 ui = dvb;
		dvb += 4;
		m = dvb;
		if ((ui == 0) && (m == 0))
		    goto out;			// Don't support broken streams (dvd plugin).
		o = 7;
	    } else {
		dvb += 5;
		m = dvb;
	    }
	    off += o;

	    if (o >= rest)
		break;
	    if (id != ub)
		break;
	    curr = &pcm;
	    set_setup(AUDIO);
	    curr->Reset(setup.flags);
	    curr->isDVD = true;
	    curr->track = tr;

	    switch (m & 0x30) {
	    case 0x00: curr->sample_rate = 48000;
		break;
	    case 0x20: curr->sample_rate = 44100;
		break;
	    case 0x30: curr->sample_rate = 32000;
		break;
	    case 0x10:				// 96kHz  currently not supported
	    default:   curr = NULL;
		break;
	    }
	    switch (m & 0xC0) {
	    case 0x00:				// 16 bit samples
		break;
	    case 0x40:				// 20 bit currently not supported
	    case 0x80:				// 24 bit currently not supported
	    default:   curr = NULL;
		break;
	    }
	    switch (m & 0x07) {
	    case 0x00:				// 1 channel
	    case 0x01:				// 2 channels
		break;
	    default:   curr = NULL;		// More channels not supported
		break;
	    }
	    goto out;				// found, go out (even if not supported)
	default:
	    dvb++;
	    off++;
	    while (dvb > (size_t)2) {
		if ((uint_16)dvb == AC3magic) {
		    curr = &ac3;
		    clear_setup(AUDIO);
		    curr->Reset(setup.flags);
		    curr->isDVD = false;
		    curr->track = 0x21;
		    break;
		}
		if ((dvb > (size_t)4) && (uint_32)dvb == DTSmagic) {
		    curr = &dts;
		    clear_setup(AUDIO);
		    curr->Reset(setup.flags);
		    curr->isDVD = false;
		    curr->track = 0x21;
		    break;
		}
		dvb++;
		off++;
	    }
	    goto out;
	}
    } END (dvb);

out:
    ctr.Lock();
    stream = curr;
    ctr.Unlock();
    return (curr != NULL);
}

inline bool cReplayOutSPDif::DigestPayOfMP2(const uchar *const b, int &off, const int cnt, const uchar id)
{
    static const uint_32 samplerate_table[3] = {44100, 48000, 32000};
    const off_t rest = cnt-off;
    cHandle audio(&b[off], rest);
    ctr.Lock();
    iec60958* curr = stream;
    ctr.Unlock();

    FOREACH(audio >= (size_t)2) {
	uint_16 us = audio;
	bool ext   = ((us & 0x0010)>>4 == 0);	// First of last five bits
	bool lsf   = ((us & 0x0008)>>3 == 0);	// Second of last five bits
	char layer = 4 - ((us & 0x0006)>>1);	// Next two bits of the last five
						// Last bit is the protection bit

	if ((us & 0xffe0) != 0xffe0) {		// Sync start: The first eleven bits
	    audio++;
	    continue;
	}
	size_t o = audio.Offset();

	if ((layer == 4) || (!lsf && ext)) {	// Sanity checks for mpeg audio
	    audio++;
	    continue;
	}
	audio += 2;

	uint_8 ub = audio;
	if ((ub & 0xf0) >> 4 == 15) {		// Bit rate index
	    audio++;
	    continue;
	}
	if ((ub & 0x0c) >> 2 == 3) {		// Sample rate index
	    audio++;
	    continue;
	}

	uint_32 rate = samplerate_table[(ub & 0x0c) >> 2];
	if (lsf) {				// Sample rate
	    rate /= 2;
	    if (ext)
		rate /= 2;
	}

	curr = &mp2;
	if (test_setup(MP2SPDIF))
	    clear_setup(AUDIO);
	else
	    set_setup(AUDIO);
	curr->Reset(setup.flags);
	curr->isDVD = false;
	curr->sample_rate = rate;
	off += o;
	break;

    } END (handle);

    ctr.Lock();
    stream = curr;
    ctr.Unlock();
    return (curr != NULL);
}

void cReplayOutSPDif::Play(const uchar *b, int cnt, uchar id)
{
    static uint_16 skip = 0;
    cHandle play(b, cnt);
    bool pts = false;
    ctr.Lock();
    iec60958* curr = stream;
    ctr.Unlock();

    if (test_setup(CLEAR))
	goto out;

    if (!test_setup(ACTIVE)) {
	if (test_flag(ACTIVE))
	    Clear();
	goto out;
    }

    if (skip) {
	debug("cReplayOutSPDif::Receive(skip=%d)\n", skip);
	skip--;
	goto out;
    }

    if (test_setup(MUTE) || test_setup(LIVE)) {
	bounce->flush();
	goto out;
    }

    TEST (play >= (size_t)10) {
	uint_32 mag = play;
	play += 4;
	uint_16 len = (uint_16)play + 6;
	play += 2;
	uint_16 flg = play;
	play += 2;
	int     off = (uint_8)play + 9;

	if ((flg & 0xC000) != 0x8000)
	    goto out;

	pts = ((flg & 0x0080) && (off >= (9+5)));

	switch (mag) {
	case PS1magic:				// Audio of PES Private Stream 1

	    if (curr == &mp2) {			// MP2 already active
		Clear();			// Reset
		goto out;
	    }

	    if (!test_flag(BOUNDARY)) {
	    
		// If the stream provides PTS/DTS information and/or
		// the data in the pay load is marked as aligned we
		// assume that that we'll find immediately the sync words
		// of the pay load its self or the corresponding sub
		// stream identification with the offset to the pay load.
		if ((flg & 0x00C0) || (flg & 0x0400)) {

		    if (!ScanPayOfPS1(&b[0], off, cnt, id))
			goto out;
		    
		    ctr.Lock();
		    curr = stream;
		    ctr.Unlock();
		    if (!curr) goto out;	// Paranoid

		    set_flag(BOUNDARY);		// At the beginning of a stream

		    if (!test_flag(RUNNING)) {	// Start or continue after mute the
			if (!pts)		// forwarding thread only on if we
			    goto out;		// got a Present Time Stamp (PTS).
			Activate(true);
			clear_flag(WASMUTED);
		    } else if (test_flag(WASMUTED)) {
			if (!pts)
			    goto out;
			clear_flag(WASMUTED);
		    }

		} else
		    goto out;			// Start only at a boundary of a stream

	    } else if (curr) {

		if (curr->isDVD) {
		    // Check if DVD stream is still valid
		    if (!OffsetToDvdOfPS1(b, off, cnt, id)) {
			Clear();		// Reset
			goto out;
		    }
		} else {
		    // Check if id of the stream is still valid
		    if (id != 0xBD) {
			Clear();		// Reset
			goto out;
		    }
		}
	    }
	    break;
	case 0x000001c0 ... 0x000001df:		// Audio of PES Mpeg Audio frame
	    if (!test_setup(MP2ENABLE))
		goto out;

	    if (curr) {
		uint_8 track = ((uint_8)(mag & 0xFF) - 0xC0) + 1;

		if (curr != &mp2) {		// PS1 already active
		    Clear();			// Reset
		    goto out;
		}
		if (curr->track != track) {	// Wrong track
		    Clear();			// Reset
		    goto out;
		}
	    }

	    if (test_flag(BOUNDARY))
		break;
						// Start only at a boundary of a stream
	    if (!(flg & 0x00C0) && !(flg & 0x0400))
		goto out;

	    if (!DigestPayOfMP2(&b[0], off, cnt, id))
		goto out;

	    ctr.Lock();
	    curr = stream;
	    ctr.Unlock();
	    if (!curr) goto out;		// Paranoid

	    set_flag(BOUNDARY);			// At the beginning of a stream

	    if (!test_flag(RUNNING)) {		// Start or continue after mute the
		if (!pts)			// forwarding thread only on if we
		    goto out;			// got a Present Time Stamp (PTS).
		Activate(true);
		clear_flag(WASMUTED);
	    } else if (test_flag(WASMUTED)) {
		if (!pts)
		    goto out;
		clear_flag(WASMUTED);
	    }

	    curr->track = ((uint_8)(mag & 0xFF) - 0xC0) + 1;

	    break;
	default:
	    goto out;
	}

	if (curr) {
	    const uint_8 * buf = (const uint_8 *)&b[off];
	    const size_t transmit = min(len - off, cnt);
	    const bool start = curr->Count(buf, buf+transmit);

	    //
	    // Initial synchronization
	    //
	    if (!curr->pts.synch()) {

		if (!curr->pts.stcsync(pts))	// Check if STC has constant time flow
		    goto out;			// to avoid old data in bounce buffer

		if (pts) {			// PTS given, remember value than
		    play++;			// ...
		    curr->pts.mark(play(5));	// and start the sync engine now
		} else {
		    if (!curr->pts.mark())
			goto out;		// Currently not started yet
		}

		curr->pts.synch(true);		// We are done here
	    }

	    if (!bounce->store(buf, transmit, start))
		skip = 2;

	}

    } END (play);
out:
    return;
}

void cReplayOutSPDif::Mute(bool onoff)
{
    debug("cReplayOutSPDif::Mute(%d) called\n", onoff);
    if ((test_setup(MUTE) ? true : false) == onoff)
	goto out;

    clear_setup(STILLPIC);
    if (onoff) {
	cDevice *PrimaryDevice = cDevice::PrimaryDevice();
	LOCK_THREAD;
	ctr.Lock();
	iec60958* curr = stream;
	ctr.Unlock();

	set_setup(MUTE);
	if (PrimaryDevice && !PrimaryDevice->IsMute())
	    set_setup(STILLPIC);
	spdifDev->Pause(onoff);
	if (curr)
	    curr->Clear();
	bounce->flush();
	bounce->signal();
	clear_flag(BOUNDARY);
    } else
	clear_setup(MUTE);
out:
    if (SPDIFmute) {	// Normal TV mode
	char *cmd = NULL;
	asprintf(&cmd, "%s %s 2> /dev/null", SPDIFmute, test_setup(MUTE) ? "mute" : "unmute");
	if (cmd) {
	    if (test_setup(ACTIVE)) {
		if (test_setup(MP2ENABLE))
		    setenv("loop", "off", 1);
		else
		    setenv("loop", "on", 1);
	    } else
		setenv("loop", "on", 1);
	    system(cmd);
	    free(cmd);
	    cmd = NULL;
	}
    }
    return;
}

void cReplayOutSPDif::Clear(void)
{
    cDevice *PrimaryDevice = cDevice::PrimaryDevice();

    debug("cReplayOutSPDif::Clear() called\n");
    if (test_flag(RUNNING))
	Activate(false);
    ctr.Lock();
    iec60958* curr = stream;
    stream = NULL;
    ctr.Unlock();
    if (curr) {
	LOCK_THREAD;
	curr->Reset();
	if (bounce) {
	    bounce->flush();
	    bounce->signal();
	}
    }
    clear_flag(BOUNDARY);
    clear_setup(STILLPIC);
    if (PrimaryDevice)
	Mute(PrimaryDevice->IsMute());
}
