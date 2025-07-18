/*
 * spdif.h:	Access the S/P-DIF interface of sound cards supported by ALSA.
 *		for redirecting none audio streams (NO decoding!).
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

#ifndef __SPDIF_H
#define __SPDIF_H

#include <sys/time.h>
#include <signal.h>
#ifndef HAS_ASOUNDLIB_H
# error error The file /usr/include/alsa/asoundlib.h is missed, install e.g. alsa-devel!
#endif
#include <alsa/asoundlib.h>
#include <vdr/thread.h>
#include "iec60958.h"
#include "bounce.h"
#include "bitstreamout.h"

#define EINTR_RETRY(exp)				\
    (__extension__ ({					\
	int __result;					\
	do { __result = (int) (exp);			\
	     if (__result < 0) pthread_yield();		\
	} while (__result < 0 && errno == EINTR);	\
	__result; }))

class cPsleep {
private:
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    struct timeval  now;
    struct timespec res;
public:
    cPsleep();
    ~cPsleep();
    void msec(int ms);
    void usec(int us);
};

class cHold {
private:
    cThreadLock *thl;
public:
    cHold() : thl((cThreadLock*)0) {}
    ~cHold() { Unhold(); }
    bool Hold(cThread *Thread);
    void Unhold(void);
};

class spdif : cHold {
private:
    // Thread locking
    cThread *thread;
    #define HOLD(thread) cThreadLock ThreadLock(thread)
    iec60958 *stream;
    virtual bool Stream(iec60958 *in);
    inline bool Frame(frame_t &pcm, const uint_8 *&out, const uint_8 *const tail);
    snd_pcm_t *out;
    snd_pcm_uframes_t burst_size;
    snd_pcm_uframes_t periods;
    snd_pcm_sframes_t delay;
    snd_pcm_sframes_t pause;
    snd_pcm_uframes_t buffer_size;
    snd_pcm_format_t  format;
    int count;
    size_t paysize;
#   define PCM_SILENT_10MS48KHZ	((10*48000)/1000)
    static uint_32 silent_buf[];
    frame_t silent;
    int fragsize;
    int period;
    typedef snd_pcm_sframes_t (*snd_pcm_writei_t)(snd_pcm_t *, const void *, snd_pcm_uframes_t);
    snd_pcm_writei_t writei;
    snd_pcm_status_t *status;
    snd_output_t *log;
    snd_aes_iec958_t ch;
    enum {
	FL_FIRST    = 1,	// Start and after Pause
	FL_IO       = 2,	// More frames in Forward() handled
	FL_CANPAUSE = 3,	// Hardware can do pause
	FL_UNDERRUN = 4,	// Underrun detected
	FL_VRPERIOD = 5,	// double frames in case of underrun
	FL_NOEXSYNC = 6,	// Synchronize() was not called before
	FL_BURSTRUN = 7,	// burst() active
	FL_PAUSE    = 8,	// Pause() called
	FL_OVERRUN  = 9,	// Overrun detected
	FL_REPEAT   = 10	// Repeat last frame upto 1 second
    };
    flags_t ctrlbits;
#   define test_and_set_ctrl(ctrl)         test_and_set_bit  (FL_ ## ctrl, &ctrlbits)
#   define test_and_clear_ctrl(ctrl)       test_and_clear_bit(FL_ ## ctrl, &ctrlbits)
#   define test_ctrl(ctrl)                 test_bit (FL_ ## ctrl, &ctrlbits)
#   define set_ctrl(ctrl)                  set_bit  (FL_ ## ctrl, &ctrlbits)
#   define clear_ctrl(ctrl)                clear_bit(FL_ ## ctrl, &ctrlbits)
    virtual void burst(const frame_t &pcm);
    struct timeval xrstart;
    inline bool xrepeat(void);
    inline void xunderrun(void);
    inline void xsuspend (void);
    // Block signals during handlers
    sigset_t oldset;
    virtual inline void block_signals(void);
    virtual inline void leave_signals(void);
    // buffer checking
    enum {SPDIF_LOW = -1, SPDIF_OK = 0, SPDIF_HIGH = 1};
    virtual int check(snd_pcm_status_t * status = NULL);
    struct {
	snd_pcm_uframes_t upper;
	snd_pcm_uframes_t lower;
	snd_pcm_uframes_t high;
	snd_pcm_uframes_t alarm;
    } buf;
    // Wait on close
    cMutex   _close;
    inline void Lock  (void) { _close.Lock();   };
    inline void Unlock(void) { _close.Unlock(); };
    // Option handling
    enum {SPDIF_CON, SPDIF_PRO};
    struct {
	unsigned int card;
	unsigned int device;
	unsigned int type;
	int first;
	int mdelay;
	int adelay;
	unsigned int iec958_aes3_con_fs_rate;
	unsigned int iec958_aes0_pro_fs_rate;
	bool mmap;
	bool audio;
    } opt;
    ctrl_t &setup;
    cPsleep wait;
protected:
public:
    spdif(ctrl_t &up);
    virtual ~spdif();
    virtual inline operator void* () { return (void*)out; };
    virtual void Forward(const uint_8 *data, const size_t dlen, class cBounce *bounce);
    virtual bool Open(iec60958 *in, cThread *caller = NULL);
    virtual void Close(cThread *caller = NULL);
    virtual void Clear(bool exit = false);
    virtual bool Synchronize(class cBounce *bounce);
    virtual size_t Available(const size_t max);
    virtual void Pause(const bool onoff);
};

#endif // __SPDIF_H
