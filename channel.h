/*
 * channel.h:	Get and forward AC3 stream from DVB card to the 
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
 */

#ifndef __CHANNEL_H
#define __CHANNEL_H

#include <vdr/receiver.h>
#include <vdr/transfer.h>
#include <vdr/status.h>
#include <vdr/thread.h>
#include <vdr/dvbdevice.h>
#include "spdif.h"
#include "bounce.h"
#include "bitstreamout.h"
#include "iec60958.h"

#ifndef TS_SIZE
# define TS_SIZE	188
#endif
#ifndef TS_ERROR
# define TS_ERROR	0x80
#endif
#ifndef PAY_START
# define PAY_START	0x40
#endif
#ifndef CONT_CNT_MASK
# define CONT_CNT_MASK	0x0F
#endif
#ifndef ADAPT_MASK
# define ADAPT_MASK	0x30
#endif
#ifndef ADAPT_FIELD
# define ADAPT_FIELD	0x20
#endif
#ifndef PTS_ONLY
# define PTS_ONLY	0x80
#endif

class cInStream : public cReceiver, cThread {
private:
    // Bit flags
    volatile flags_t flags;
    // Sync/Underrun thread
    #define FLAG_RUNNING	0		// Forwarding thread is running
    #define FLAG_ACTIVE		1		// Forwarding thread loop is running
    #define FLAG_FAILED		2		// We failed
    #define FLAG_BOUNDARY	3		// PES boundary detected
    #define FLAG_WASMUTED	4		// Previous muted		
    #define FLAG_PAYSTART	5		// The payload of the PES frame
    #define FLAG_PS1AUDIO	6		// We've seen a PS1 PES frame
    #define FLAG_STREAMING	7		// We've set a stream in ScanTSforAudio()
    // The TS scanner
    static const uint_32 PS1magic;
    // Stream detection
    iec60958* stream;
    cMutex ctrl;
    cIoWatch StreamReady;
    const char *audioType;
    uint_32 syncword;
    uint_32 submagic;
    uint_16 Apid;
    uint_16 bfound;
    uint_16 bytes;
    uint_16 paklen;
    uint_16 ptsoff;
    uint_16 suboff;
    uint_16 subfnd;
    uint_8  paystart;
    uint_8  pts[5];
    uint_8  scan[TS_SIZE+4];
    #define FLAG_SET_PTS	8		// PTS found in PES frame
    inline bool ScanTSforAudio(uint8_t *buf, const int cnt, const bool wakeup);
    inline void ResetScan(bool err = true);
    // Fast ring buffer
    static uint_8  * tsdata;
    static cBounce * bounce;
    cPsleep wait;
protected:
    spdif *const spdifDev;
    ctrl_t &setup;
    virtual void Action(void);
    virtual void Activate(bool on);
    virtual void Receive(uchar *b, int cnt);
public:
    cInStream(int Pid, spdif *dev, ctrl_t &up, cBounce * bPtr);
    ~cInStream();
    uint_16 AudioPid(void) const { return Apid; };
    const char  *AudioType(void) const { return audioType; };
    virtual void Clear(void);
};

enum {
    MpegAudio    = false,
    DolbyDigital = true
};

class cDevicePerm : public cDevice {
private:
    cDevicePerm(void) : cDevice() {};
public:
    bool UsePid(int Pid) const { return HasPid(Pid); }
};

class cChannelOutSPDif : public cStatus, cThread {
private:
    volatile flags_t flags;
    // Sync/Underrun thread
    #define FLAG_RUNNING	0
    #define FLAG_ACTIVE		1
    #define FLAG_SWITCHED	2
    cInStream *in;
    static const char *audioType;
    static uint_16 Apid;
    cMutex sw, ctrl;
    cCondVar watch;
    const cChannel *Channel;			// Active Live Channel if any
    virtual void IfNeededMuteSPDIF(void);
    static cBounce * bounce;
    cPsleep wait;
    bool InTransferMode(void);
    bool GetCurrentAudioTrack(uint_16 &apid, const char* &type);
    static bool GetCurrentAudioTrack(uint_16 &apid, const char* &type, const cChannel *channel);
    virtual void AttachReceiver(bool onoff);
protected:
    const char *const SPDIFmute;
    spdif *const spdifDev;
    ctrl_t &setup;
    virtual void SetAudioTrack(int Index, const char * const *Tracks);
    virtual void Action(void);
    virtual void Activate(bool on);
    virtual void ChannelSwitch(const cDevice *Device, int ChannelNumber);
    virtual void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On);
    virtual void Replaying(const cControl *Control, const char *Name, const char *FileName, bool On);
public:
    cChannelOutSPDif(spdif &dev, ctrl_t &up, cBounce * bPtr, const char *script);
    ~cChannelOutSPDif(void);
    bool    AudioTrack(int num, const char* type, int &apid);
    uint_16 AudioPid(void);
    const char * AudioType(void);
    virtual void AudioSwitch(uint_16 apid, const char* type, const cChannel* channel = NULL);
    virtual void AudioOff(void);
    const bool Replaying(void) const {
	cDevice *PrimaryDevice = cDevice::PrimaryDevice();
	return PrimaryDevice ? PrimaryDevice->Replaying() : true;
    };
    virtual void Clear(void);
};

#endif // __CHANNEL_H
