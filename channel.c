/*
 * channel.c:	Get and forward AC3 stream from DVB card to the
 *		S/P-DIF interface of sound cards supported by ALSA.
 *		(NO decoding!)
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

#define USE_SWITCH_THREAD

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include "types.h"
#include "channel.h"
#include "ac3.h"
#include "dts.h"
#include "lpcm.h"
#include "mp2.h"

// #define DEBUG_CHL
#ifdef  DEBUG_CHL
# define debug_chl(args...) fprintf(stderr, args)
#else
# define debug_chl(args...)
#endif

// --- cInStream : Get AC3 stream for redirecting it to  S/P-DIF of a sound card--------

const uint_32 cInStream::PS1magic = 0x000001bd;
uint_8  * cInStream::tsdata;
cBounce * cInStream::bounce;

cInStream::cInStream(int Pid, spdif *dev, ctrl_t &up, cBounce *bPtr)
:cReceiver(tChannelID(), -1, Pid),
 cThread("bso(instream): Forwarding bitstream"),
 ctrl(), wait(), spdifDev(dev), setup(up)
{
    flags = 0;
    ctrl.Lock();
    stream = NULL;
    ctrl.Unlock();
    Apid = Pid;
    ResetScan();
    bounce = bPtr;
    bounce->bank(0);
    tsdata = setup.buf + TRANSFER_START;
    audioType = audioTypes[IEC_NONE];
}

cInStream::~cInStream(void)
{
    Clear();
}

void cInStream::Activate(bool onoff)
{
    if (test_setup(CLEAR))
	onoff = false;

    debug_chl("%s %d (%s)\n", __FUNCTION__, __LINE__, onoff ? "on" : "off");

    if (onoff) {
	if (*spdifDev)
	    goto out;				// An other has opened the S/P-DIF
	if (test_setup(RESET)) {
	    clear_flag(FAILED);
	    clear_setup(RESET);
	}
	if (test_flag(FAILED) || test_flag(ACTIVE))
	    goto out;
	set_flag(ACTIVE);
	clear_flag(STREAMING);			// Streaming only after receiving data
	set_setup(LIVE);
	bounce->flush();
	Start();				// Warning: Start() sleeps 10ms after work
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
	    esyslog("INSTREAM: Forwarding bitstream thread was broken");
	    wait.msec(10);			// More than 2ms
	    ctrl.Lock();
	    iec60958* curr = stream;
	    ctrl.Unlock();
	    if (curr) {				// thread broken
		spdifDev->Close();
		clear_flag(BOUNDARY);
		curr->Reset();
		if (bounce)
		    bounce->leaveio();
	    }
	    clear_flag(RUNNING);		// Should not happen
	}

	ResetScan();
	clear_setup(LIVE);
	clear_flag(STREAMING);
	ctrl.Lock();
	stream = NULL;
	ctrl.Unlock();
    }
out:
    return;
}

void cInStream::Action(void)
{
    set_flag(RUNNING);
    iec60958 *curr = NULL;

    realtime("INSTREAM: ");

    do {
	if (!test_flag(ACTIVE))
	    break;
	if (StreamReady.Wait(100)) {		// Wait 100ms
	    ctrl.Lock();
	    curr = stream;
	    ctrl.Unlock();
	    if (curr) set_flag(STREAMING);	// Ready for streaming
	}
    } while (!curr);

    if (!test_flag(ACTIVE))
	goto out;

    if (!curr) {
	esyslog("INSTREAM: no stream for spdif interface");
	set_flag(FAILED);
	goto out;
    } else {
	if (!spdifDev->Open(curr, this)) {
	    esyslog("INSTREAM: can't open spdif interface");
	    set_flag(FAILED);
	    goto out;
	}
    }
    debug_chl("%s %d\n", __FUNCTION__, __LINE__);

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

	if ((len = bounce->fetch(tsdata, spdifDev->Available(TRANSFER_MEM))))
	    spdifDev->Forward(tsdata, len, bounce);
    }
    spdifDev->Close(this);
    clear_flag(BOUNDARY);
    curr->Reset();
out:
    ctrl.Lock();
    stream = NULL;
    ctrl.Unlock();
    if (bounce) {
	bounce->flush();
	bounce->leaveio();
    }
    clear_flag(ACTIVE);
    clear_flag(RUNNING);
}

void cInStream::Receive(uchar *b, int cnt)
{
    uint_8 *ptr = NULL;
    uint_8 off = 4;
    uint_8 start = 0;
    static uint_16 skip = 0;

    if (test_setup(CLEAR))
	goto out;

    if (!test_setup(ACTIVE)) {
	if (test_flag(RUNNING))
	    Activate(false);
	goto out;
    }

    if (skip) {
	debug("cReplayOutSPDif::Play(skip=%d)\n", skip);
	skip--;
	goto out;
    }

    if (test_setup(MUTE)) {
	clear_flag(PAYSTART);
	goto out;
    }

    if (!b || *b != 0x47) {
	esyslog("INSTREAM: have seen broken TS packet");
	goto out;
    }

    if (cnt != TS_SIZE) {
	esyslog("INSTREAM: have seen broken TS packet");
	goto out;
    }

    start = (b[1] & PAY_START);

    // Start engine only if PES frame starts
    if (!test_flag(PAYSTART)) {
	if (!start)
	    goto out;
	set_flag(PAYSTART);
    }

    if (Apid != (((uint_16)(b[1]&0x1F)<<8)|b[2]))
	goto out;

#if 0
    if (b[1] & TS_ERROR)
	TSerr++;			// Skip?

    if ((b[3] ^ TScount) & CONT_CNT_MASK) {
	if (/* TScount != -1 && */ ((b[3] ^ (TScount + 1)) & CONT_CNT_MASK))
	    TScnterr++;			// Skip?
	TScount = (b[3] & CONT_CNT_MASK);
    }
#endif
    if (b[3] & ADAPT_FIELD) {
	off += (b[4] + 1);
	if (off > 187)
	    goto out;
    }

    ptr = &b[off];
    if (stream == NULL) {		// We edit the buffer contents in this case
	ptr = &scan[4];
	memcpy(ptr, &b[off], TS_SIZE-off);
    }

    if (!ScanTSforAudio(ptr, TS_SIZE-off, (start != 0)))
	skip = 20;
out:
    return;
}

inline void cInStream::ResetScan(bool err)
{
    bfound = paklen = ptsoff = subfnd = suboff = 0;
    bytes = 9;
    memset(pts, 0, sizeof(pts));
    syncword = 0xffffffff;
    submagic = 0xffffffff;
    paystart = 0xff;
    if (err)
	clear_flag(BOUNDARY);
}

inline bool cInStream::ScanTSforAudio(uint_8 *buf, const int cnt, const bool wakeup)
{
    const uint_8 *const tail = buf + cnt;
    bool ret = true, search;
    ctrl.Lock();
    iec60958 *curr = stream;
    ctrl.Unlock();

resync:
    if (bfound < bytes) {
	//
	// Scan for payload length and PTS, the switch is used here
	// to be able to start at any offset (controlled by bfound)
	//
	switch (bfound) {
	case 0 ... 3:

	    if (!wakeup && !curr)			// No start TS frame, no PES start
		goto reset;

	    //
	    // Scan for start sequence of the Private Stream 1 or Mpeg2 Audio
	    //
	    search = true;
	    while (search) {
		if (buf >= tail)
		    goto out;
		syncword = (syncword << 8) | *buf++;

		switch (syncword) {
		case PS1magic:				// Audio of PES Private Stream 1
		    if (curr == &mp2)
			goto out;
		    search = false;
		    set_flag(PS1AUDIO);
		    break;
		case 0x000001c0 ... 0x000001df:		// Audio of PES Mpeg Audio frame
		    if (!test_setup(MP2ENABLE))
			goto out;
		    if (curr && curr != &mp2)
			goto out;
		    search = false;
		    clear_flag(PS1AUDIO);
		    break;
		default:
		    break;
		}
	    }
	    bfound = 4;
	case 4:
	    if (buf >= tail)
		goto out;
	    paklen  = (uint_16)(*buf) << 8;
	    buf++;
	    bfound++;
	case 5:
	    if (buf >= tail)
		goto out;
	    paklen |= *buf;
	    paklen += 6;				// Full packet length
	    buf++;
	    bfound++;
	case 6:
	    if (buf >= tail)
		goto out;
	    if ((*buf & 0xC0) != 0x80) {		// First mpeg2 flag byte
		ResetScan();				// Reset for next scan
		esyslog("INSTREAM: broken TS stream");
		goto resync;				// Check for rest
	    }
	    if (*buf & 0x04)				// Is the sub stream aligned?
		set_flag(BOUNDARY);
	    buf++;
	    bfound++;
	case 7:
	    if (buf >= tail)
		goto out;
	    memset(pts, 0, sizeof(pts));
	    if (*buf & 0xC0) {				// Second mpeg2 flag byte (PTS/DTS only)
		bytes = 14;
		set_flag(BOUNDARY);
	    } else {
		bytes = 9;
	    }
	    buf++;
	    bfound++;
	case 8:
	    if (buf >= tail)
		goto out;
	    ptsoff = *buf;				// Third mpeg2 byte (PTS offset)
	    buf++;
	    bfound++;
	    if (bytes <= 9)				// No PTS data: out here
		break;
	case 9 ... 13:					// Save PTS
	    do {
		if (buf >= tail)
		    goto out;
		pts[bfound - 9] = *buf;
		buf++;
		bfound++;
		ptsoff--;
	    } while (bfound < 14);
            set_flag(SET_PTS);
	default:
	    break;
	}
    }

    //
    // Start only at a logical begin of a sub stream
    //
    if (!test_flag(BOUNDARY))
	goto reset;

    //
    // Skip PTS
    //
    while (ptsoff) {
	register int skip;
	if ((skip = tail - buf) <= 0)
	    goto out;
	if (skip > ptsoff)
	    skip = ptsoff;
	buf    += skip;
	bfound += skip;
	ptsoff -= skip;
    }

    if (!curr) {
	if (test_flag(PS1AUDIO)) {			// Scan for DVB/DVD data
	    int todo = 0;

	    //
	    // BOUNDARY is set and stream is NULL at first time therefore we
	    // are able to scan for magics in payload due the data are aligned
	    // (start at first payload byte).  For Radio stations using DTS ...
	    //
	    switch (subfnd) {
	    case 0:
		if (buf >= tail)
		    goto out;
		paystart = *buf;			// Remember for the case we find a DVD structure
		submagic = (*buf) & 0x000000ff;		// Normal DVB audio (the aligned case)
		buf++; subfnd++; bfound++;
	    case 1:
		if (buf >= tail)
		    goto out;
		submagic = (submagic << 8) | *buf;
		buf++; subfnd++; bfound++;
		if ((submagic & 0x0000ffff) == 0x0b77) {
		    todo = 1;
		    break;				// found, skip rest
		}
	    case 2:
		if (buf >= tail)
		    goto out;
		submagic = (submagic << 8) | *buf;
		suboff = (uint_16)(*buf) << 8;
		buf++; subfnd++; bfound++;
		if ((submagic & 0x0000ffff) == 0x0b77) {
		    todo = 1;
		    break;				// found, skip rest
		}
	    case 3:
		if (buf >= tail)
		    goto out;
		submagic = (submagic << 8) | *buf;
		suboff |= *buf;
		buf++; subfnd++; bfound++;
		if ((submagic & 0x0000ffff) == 0x0b77) {
		    todo = 1;
		    break;				// found, skip rest
		}
		if (submagic == 0x7ffe8001) {
		    todo = 2;
		    break;
		}
		if (paystart >= 0x80 && paystart <= 0x87) {
		    todo = 3;
		    break;				// found, skip rest
		}
		if (paystart >= 0x88 && paystart <= 0x8f) {
		    todo = 4;
		    break;				// found, skip rest
		}
	    default:
		while (bfound + 2 <= paklen) {		// Normal DVB audio (the non-aligned case e.g. ORF)
		    if (buf >= tail)
			goto out;
		    submagic = (submagic << 8) | *buf;
		    buf++; subfnd++; bfound++;
		    if ((submagic & 0x0000ffff) == 0x0b77) {
			todo = 1;
			break;
		    }
		    if ((bfound + 4 <= paklen) && (submagic == 0x7ffe8001)) {
			todo = 2;
			break;
		    }
		}
		if (todo == 0) goto reset;
		break;
	    }

	    switch (todo) {
	    case 1:
		buf -= 2;
		buf[0] = 0x0b; buf[1] = 0x77;
		suboff = 0;
		curr = &ac3;
		clear_setup(AUDIO);
		curr->Reset(setup.flags);
		curr->isDVD = false;
		curr->track = 0x21;
		audioType = audioTypes[IEC_AC3];
		break;
	    case 2:
		buf -= 4;
		buf[0] = 0x7f; buf[1] = 0xfe;
		buf[2] = 0x80; buf[3] = 0x01;
		suboff = 0;
		curr = &dts;
		clear_setup(AUDIO);
		curr->Reset(setup.flags);
		curr->isDVD = false;
		curr->track = 0x21;
		audioType = audioTypes[IEC_DTS];
		break;
	    case 3:					// DVD AC3 data stream may used over DVB (??)
		curr = &ac3;
		clear_setup(AUDIO);
		curr->Reset(setup.flags);
		curr->isDVD = true;
		curr->track = (paystart & 0x1F) + 0x21;
		audioType = audioTypes[IEC_AC3];
		break;
	    case 4:					// DVD DTS data stream may used over DVB (??)
		curr = &dts;
		clear_setup(AUDIO);
		curr->Reset(setup.flags);
		curr->isDVD = true;
		curr->track = (paystart & 0x1F) + 0x21;
		audioType = audioTypes[IEC_DTS];
		break;
	    case 0:
	    default:
		break;
	    }

	} else {
	    //
	    // At BOUNDARY scanning for Mpeg2 Audio data start is not needed
	    // we do it here to save some bytes in the bounce buffer
	    //
	    while (bfound + 2 <= paklen) {
		if (buf >= tail)
		    goto out;
		submagic = (submagic << 8) | *buf;
		buf++; subfnd++; bfound++;

		if ((submagic & 0xfffe) == 0xffe0) {
		    const bool ext  = ((submagic & 0x0010) >> 4) == 0;
		    const bool lsf  = ((submagic & 0x0008) >> 3) == 0;
		    const int layer = 4 - ((submagic & 0x0006) >> 1);

		    if ((layer == 4) || (!lsf && ext))	// Sanity check for mpeg audio
			continue;

		    buf -= 2;
		    ((uint_16*)&buf[0])[0] = ntohs(submagic);
		    break;				// Found, skip rest
		}
	    }
	    curr = &mp2;
	    if (test_setup(MP2SPDIF))
		clear_setup(AUDIO);
	    else
		set_setup(AUDIO);
	    curr->Reset(setup.flags);
	    curr->isDVD = false;
	    curr->track = ((uint_8)(syncword & 0xFF) - 0xC0) + 1;
	    audioType = audioTypes[IEC_MP2];
	}

    } else if (test_flag(PS1AUDIO) && curr && curr->isDVD) {
	// Skip sub stream head for DVD case
	suboff = 4;
	switch (*buf) {
	case 0x80 ... 0x87:
	    if (curr == &ac3 && curr->track == ((*buf) & 0x1F) + 0x21)
		break;
	case 0x88 ... 0x8f:
	    if (curr == &dts && curr->track == ((*buf) & 0x1F) + 0x21)
		break;
	default:
	    goto reset;
	    break;
	}
    }

    //
    // We have to had a stream behind this point
    //
    if (!curr)
	goto reset;

    //
    // Start or continue after mute the forwarding thread
    // only on PES boundary and if we got a Present Time
    // Stamp (PTS). With this we should get as fast as
    // possible into A/V sync.
    //
    if (!test_flag(STREAMING)) {
	if (!test_flag(SET_PTS))
	    goto reset;
	//
	// Set the stream we use and signal the awaiting thread
	//
	ctrl.Lock();
	stream = curr;
	ctrl.Unlock();
	StreamReady.Signal(true);
	clear_flag(WASMUTED);
    } else if (test_flag(WASMUTED)) {
	if (!test_flag(SET_PTS))
	    goto reset;
	clear_flag(WASMUTED);
    }

    //
    // Skip substream offset, if any
    //
    while (suboff) {
	register int skip;
	if ((skip = tail - buf) <= 0)
	    goto out;
	if (skip > suboff)
	    skip = suboff;
	buf    += skip;
	bfound += skip;
	ptsoff -= skip;
    }

    //
    // Initial synchronization
    //
    if (!curr->pts.synch()) {
	bool HasPTS = (test_and_clear_flag(SET_PTS) != 0);

	if (!curr->pts.stcsync(HasPTS))		// Check if STC has constant time flow
	    goto reset;				// to avoid old data in bounce buffer

	if (HasPTS)			 	// PTS given, remember value than
	    curr->pts.mark(pts);		// and start the sync engine now
	else {
	    if (!curr->pts.mark())
		goto reset;			// Currently not started yet
	}

	(void)curr->pts.synch(true);		// We are done here
    }

    //
    // Send data to payload scanner
    //
    while (bfound < paklen) {
	register int transmit, pay;
	bool start;

	if ((transmit = tail - buf) <= 0)
	    goto out;
	if (transmit > (pay = paklen - bfound))	// The rest is handled below
	    transmit = pay;
	bfound += transmit;

	//
	// Check if we have one or more data frames
	// within the submitted payload
	//
	start = curr->Count(buf, buf+transmit);

	// Submit data
	ret  = bounce->store(buf, transmit, start);
	buf += transmit;
    }

    //
    // Bytes found are equal to packet length if we have found the PES packet,
    // for the rest restart scanning.
    //
    if (paklen && (bfound >= paklen)) {
	ResetScan(false);			// Reset for next scan
	if (buf >= tail)
	    goto out;
	goto resync;				// Check the rest for next frame
    }
out:
    return ret;
reset:
    ResetScan();				// Reset during frame scanning
    return ret;
}


void cInStream::Clear(void)
{
    if (test_flag(RUNNING))
	Activate(false);
    if (bounce)
	bounce->flush();
    ResetScan();
    clear_flag(FAILED);
}

// --- cChannelOutSPDif : Live AC3 stream over S/P-DIF of a sound card with ALSA ---------
cBounce * cChannelOutSPDif::bounce;
const char * cChannelOutSPDif::audioType = audioTypes[IEC_NONE];
uint_16 cChannelOutSPDif::Apid = 0x1FFF;

cChannelOutSPDif::cChannelOutSPDif(spdif &dev, ctrl_t &up, cBounce * bPtr, const char *script)
:cStatus(),
 cThread("bso(channelout): Switching bitstream"),
 Channel(NULL), wait(), SPDIFmute(script), spdifDev(&dev), setup(up)
{
    flags = 0;
    in = NULL;
    bounce = bPtr;
}

cChannelOutSPDif::~cChannelOutSPDif(void)
{
    Clear();
}

bool cChannelOutSPDif::InTransferMode(void)
{
    cDevice *PrimaryDevice = cDevice::PrimaryDevice();

    // No primary device is always in transfer mode
    if (!PrimaryDevice)
	goto out;

    // Just missed a few years
    if (PrimaryDevice->Transferring())
	goto out;

    // Sanity check not be attached in transfer mode.
    // In this case the replay part should take the data
    if (PrimaryDevice != cDevice::ActualDevice())
	goto out;

    // Next sanity check not be attached in transfer mode
    // from the primary device to the primary device.
    // As mentioned above the replay part should take the data
    if (PrimaryDevice == cTransferControl::ReceiverDevice())
	goto out;

    return false;
out:
    AudioOff();
    return true;
}

//
// Stop current attached input filer thread if any and wake up the
// switching thread if a new audio track was choosen.  The Apid
// will be determined by GetCurrentAudioTrack() in this thread.
//
void cChannelOutSPDif::SetAudioTrack(int Index, const char * const *Tracks)
{
    cDevice *PrimaryDevice = cDevice::PrimaryDevice();
    const char* type = audioTypes[IEC_NONE];
    uint16_t apid = 0x1FFF;
    int num = 0;

    if (!PrimaryDevice)
	goto out;

    if (!Channel)
	goto out;

    if (InTransferMode())
	goto out;

    if (!test_setup(ACTIVE))
	goto out;

    for (int i = ttAudioFirst; i <= ttDolbyLast; i++) {
	const tTrackId *track = PrimaryDevice->GetTrack(eTrackType(i));
	if (!track || !track->id)
	    continue;
	if (Index == num) {
	    apid = track->id;
	    type = (i >= ttDolbyFirst) ? audioTypes[IEC_AC3] : audioTypes[IEC_MP2];
	    break;
	}
	num++;
    }

    if (apid == 0 && apid >= 0x1FFF)
	goto out;

    if (Apid == apid)
	goto out;

    sw.Lock();
    if (!Replaying())
    {
	if (in) AttachReceiver(false);		// Do _not_ reset Channel
	if (!test_flag(RUNNING))
	    Activate(true);
    }
    sw.Unlock();
    ctrl.Lock();
    watch.Broadcast();
    ctrl.Unlock();
out:
    return;
}

void cChannelOutSPDif::Activate(bool onoff)
{
    if (test_setup(CLEAR))
	onoff = false;

    if (onoff) {
	if (test_flag(ACTIVE))
	    goto out;
	set_flag(ACTIVE);
	Start();				// Warning: Start() sleeps 10ms after work
    } else {
	int n = 50;				// 500 ms

	clear_flag(ACTIVE);			// Set only above
	do {
	    ctrl.Lock();
	    watch.Broadcast();
	    ctrl.Unlock();
	    pthread_yield();
	    wait.msec(10);			// More than 2ms
	    if (!Active())
		break;
	} while (test_flag(RUNNING) && (n-- > 0));
	Cancel(1);

	if (test_flag(RUNNING) || Active()) {
	    esyslog("CHANNELOUT: Switching bitstream thread broken");
	    ctrl.Unlock();
	    clear_flag(RUNNING);		// Should not happen
	}
    }
out:
    return;
}

void cChannelOutSPDif::Action(void)
{
    set_flag(RUNNING);

    ctrl.Lock();
    while (test_flag(ACTIVE)) {
	uint_16 apid;
	const char* type;

	watch.TimedWait(ctrl, 100);			// 100ms

	if (!test_flag(ACTIVE))
	    break;

	if (test_setup(CLEAR) || !test_setup(ACTIVE))
	    break;

	if (Replaying())
	    continue;

	if (!GetCurrentAudioTrack(apid, type))		// Get audio pid and type
	    continue;

	if (Apid == apid)
	    continue;

	AudioSwitch(apid, type);
    }
    ctrl.Unlock();

    clear_flag(ACTIVE);
    clear_flag(RUNNING);
}

void cChannelOutSPDif::AudioSwitch(uint_16 apid, const char* type, const cChannel * channel)
{
    sw.Lock();
    if (in) AttachReceiver(false);	// Do _not_ reset Channel

    if (!Replaying())
    {
	if (channel)
	    Channel = channel;

	if (!Channel)
	    goto out;

	Apid = apid;
	audioType = type;

	if (!test_setup(MP2ENABLE) && (strcmp(audioType, audioTypes[IEC_AC3]) != 0))
	    goto out;

	if (Apid && Apid < 0x1FFF)
	    AttachReceiver(true);
    }
out:
    sw.Unlock();
    IfNeededMuteSPDIF();	// On close the S/P-DIF is not muted anymore
    return;
}

void cChannelOutSPDif::AudioOff(void)
{
    sw.Lock();
    Channel = NULL;		// Reset Channel
    Apid = 0x1FFF;		// Reset Apid
    audioType = audioTypes[IEC_NONE];	// ... and its type
    if (in) AttachReceiver(false);
    sw.Unlock();
}

void cChannelOutSPDif::AttachReceiver(bool onoff)
{
    cDevice *PrimaryDevice = cDevice::PrimaryDevice();

    if (test_setup(CLEAR))
	onoff = false;

    if (in) {
	in->Clear();
	if (PrimaryDevice)
	    PrimaryDevice->Detach(in);
	delete in;
	in = NULL;
	if (bounce)
	    bounce->flush();
	Apid = 0x1FFF;
	audioType = audioTypes[IEC_NONE];
    }

    if (!onoff)
	goto out;

    if (!PrimaryDevice)
	goto out;

    if (PrimaryDevice->Replaying())
	goto out;

    in = new cInStream(Apid, spdifDev, setup, bounce);
    if (!in) {
	esyslog("ERROR: out of memory");
	Apid = 0x1FFF;
	audioType = audioTypes[IEC_NONE];
	goto out;
    }
    PrimaryDevice->AttachReceiver(in);
out:
    IfNeededMuteSPDIF();	// On close the S/P-DIF is not muted anymore
    return;
}

void cChannelOutSPDif::ChannelSwitch(const cDevice *Device, int channelNumber)
{
    uint_16 apid;
    const char * type;
    cChannel * channel;

    if (!test_setup(ACTIVE)) {
	Clear();
	goto out;
    } else if (!test_flag(RUNNING))
	Activate(true);

    // LiveView is only on the primary device possible, isn't it?
    if (!Device->IsPrimaryDevice())
	goto out;

    if (InTransferMode())
	goto out;

    // If we're noticed only for channel switch we're done.
    // Also during a replay we're done.
    if (!channelNumber || Replaying()) {
	//
	// Problem: Channel switches for recording on the same
	// transponder will cause a small audio pause.
	//
	AudioOff();
	goto out;
    }

    // Now get the current live channel but _not_ the given
    // channel number (maybe this one is for recording).
    channel = Channels.GetByNumber(cDevice::CurrentChannel());

    // Oops, no channel is no audio
    if (!channel) {
	AudioOff();
	goto out;
    }

    // New channel, new audio
    if (Channel != channel)
	AudioOff();

    if (!GetCurrentAudioTrack(apid, type, channel))
	goto out;

    AudioSwitch(apid, type, channel);		// Set Channel, Apid and its type
out:
    return;
}

void cChannelOutSPDif::Recording(const cDevice *Device, const char *Name, const char *FileName, bool On)
{
    sw.Lock();
    cDevice *PrimaryDevice  = cDevice::PrimaryDevice();
    cDevice *ReceiverDevice = cTransferControl::ReceiverDevice();

    if (!PrimaryDevice)
	goto out;

    if (PrimaryDevice->Replaying())
	goto out;	// Nothing attached

    if (!ReceiverDevice)
	goto out;	// No receiver device

    if (PrimaryDevice != ReceiverDevice)
	goto out;	// Two or more cards

    if (On) {
	Channel = NULL;			// Reset Channel
	if (in) AttachReceiver(false);
    }
out:
    sw.Unlock();
    IfNeededMuteSPDIF();		// On close the S/P-DIF is not muted anymore
}

void cChannelOutSPDif::Replaying(const cControl *Control, const char *Name, const char *FileName, bool On)
{
    sw.Lock();

    if (On) {
	Channel = NULL;			// Reset Channel
	if (in) AttachReceiver(false);
    }
    sw.Unlock();
    IfNeededMuteSPDIF();		// On close the S/P-DIF is not muted anymore
}

void cChannelOutSPDif::IfNeededMuteSPDIF(void)
{
    if (SPDIFmute) {    // Normal TV mode
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
}

bool cChannelOutSPDif::GetCurrentAudioTrack(uint_16 &apid, const char* &type)
{
    bool ret = false;

    if (!Channel)
	goto out;

    if (InTransferMode())
	goto out;

    if (Replaying())
	goto out;

    ret = GetCurrentAudioTrack(apid, type, Channel);
out:
    return ret;
}

bool cChannelOutSPDif::GetCurrentAudioTrack(uint_16 &apid, const char* &type, const cChannel *channel)
{
    cDevicePerm *PrimaryDevice = static_cast<cDevicePerm *>(cDevice::PrimaryDevice());
    eTrackType CurrentAudioTrack = ttNone;
    const tTrackId *track;
    int num;
    bool ret = false;

    type = audioTypes[IEC_NONE];
    apid = 0x1FFF;
    if (!channel || !PrimaryDevice)
	goto out;

#ifndef  MAXDPIDS
# define MAXDPIDS 8
#endif
    if ((CurrentAudioTrack = PrimaryDevice->GetCurrentAudioTrack()) == ttNone)
	goto out;
    track = PrimaryDevice->GetTrack(CurrentAudioTrack);

    for (num = 0; num < MAXAPIDS; num++) {
	if (channel->Apid(num) == track->id) {
	    if (!PrimaryDevice->UsePid(track->id))
		goto out;
	    apid = channel->Apid(num);
	    type = audioTypes[IEC_MP2];
	    break;
	}
	if (num >= MAXDPIDS)
	   continue;
	if (channel->Dpid(num) == track->id) {
	    apid = channel->Dpid(num);
	    type = audioTypes[IEC_AC3];
	    break;
	}
    }
    ret = (apid && apid < 0x1FFF);
out:
    return ret;
}

bool cChannelOutSPDif::AudioTrack(int num, const char* type, int &apid)
{
    if (Replaying()) {
	apid = 0x1FFF;
    } else {
	apid = 0x1FFF;

	if (!Channel)
	   goto out;

	if (num >= MAXAPIDS)
	   goto out;

	apid = (strcmp(type, audioTypes[IEC_AC3]) == 0) ? Channel->Dpid(num) : Channel->Apid(num);
    }
out:
    return (apid && apid < 0x1FFF);
}

uint_16 cChannelOutSPDif::AudioPid(void)
{
    if (!Replaying())
	return in->AudioPid();
    return Apid;
}

const char* cChannelOutSPDif::AudioType(void)
{
    if (Replaying())
	return audioTypes[IEC_NONE];
    if (in)
	return in->AudioType();
    return audioType;
}

void cChannelOutSPDif::Clear(void)
{
    if (test_flag(RUNNING))
	Activate(false);
    AudioOff();
}
