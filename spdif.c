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

#include <getopt.h>
#include <unistd.h>
#include "spdif.h"
#include "iec60958.h"

// --- cPsleep : Be able to sleep within a thread without any usleep -------------------

cPsleep::cPsleep()
{
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
}

cPsleep::~cPsleep()
{
    pthread_cond_broadcast(&cond);
    pthread_yield();
    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&mutex);
}

void cPsleep::msec(const int ms)
{
    pthread_mutex_lock(&mutex);
    if (gettimeofday(&now, NULL) == 0) {
	int status;

	now.tv_usec += ms * 1000;
	while (now.tv_usec >= 1000000) {
	    now.tv_sec++;
	    now.tv_usec -= 1000000;
	}
	res.tv_sec  = now.tv_sec;
	res.tv_nsec = now.tv_usec * 1000;

	do { status = pthread_cond_timedwait(&cond, &mutex, &res);
	} while (status == EINTR);
    }
    pthread_mutex_unlock(&mutex);
}

void cPsleep::usec(const int us)
{
    pthread_mutex_lock(&mutex);
    if (gettimeofday(&now, NULL) == 0) {
	int status;

	now.tv_usec += us;
	while (now.tv_usec >= 1000000) {
	    now.tv_sec++;
	    now.tv_usec -= 1000000;
	}
	res.tv_sec  = now.tv_sec;
	res.tv_nsec = now.tv_usec * 1000;

	do { status = pthread_cond_timedwait(&cond, &mutex, &res);
	} while (status == EINTR);
    }
    pthread_mutex_unlock(&mutex);
}

// --- cHold : Be able to lock/unlock within a thread without cThreadLock --------------

bool cHold::Hold(cThread *Thread)
{
    if (thl) return true;
    thl = new cThreadLock(Thread);
    return (thl) ? true : false;
}

void cHold::Unhold(void)
{
    if (!thl) return;
    delete thl;
    thl = (cThreadLock*)0;
}

// --- spdif : VDR interface for opening S/P-DIF interface of sound cards --------------

uint_32 spdif::silent_buf[PCM_SILENT_10MS48KHZ];

spdif::spdif(ctrl_t &up)
: cHold(), thread(NULL), setup(up), wait()
{
    out = NULL;
    stream = NULL;
    delay = 0;
    pause = 0;
    buffer_size = 0;
    paysize = 0;
    count = 10;
    format = SND_PCM_FORMAT_S16_LE;
    (void)snd_pcm_format_set_silence(format, (void*)(&silent_buf[0]), PCM_SILENT_10MS48KHZ);
    silent.burst = &silent_buf[0];
    fragsize = 0;
    period = 0;
    writei = NULL;
    status = NULL;
    log = NULL;
    opt.card = 0;
    opt.device = 2;
    opt.type = SPDIF_CON;
    opt.first = 5;
    opt.mmap = false;
    opt.audio = false;
}

spdif::~spdif()
{
    if (out)
	Close();
}

// (Re)set a stream
bool spdif::Stream(iec60958 *in)
{
    stream = in;
    if (!stream)
	goto xout;
    burst_size = stream->BurstSize();
    periods = (16<<10)/burst_size;
    period = ((burst_size * 1000) / stream->SampleRate());
    switch (stream->SampleRate()) {
    case 48000:
	opt.iec958_aes3_con_fs_rate = IEC958_AES3_CON_FS_48000;
	opt.iec958_aes0_pro_fs_rate = IEC958_AES0_PRO_FS_48000;
	silent.size = (10*48000)/1000;
	break;
    case 44100:
	opt.iec958_aes3_con_fs_rate = IEC958_AES3_CON_FS_44100;
	opt.iec958_aes0_pro_fs_rate = IEC958_AES0_PRO_FS_44100;
	silent.size = (10*44100)/1000;
	break;
    case 32000:
	opt.iec958_aes3_con_fs_rate = IEC958_AES3_CON_FS_32000;
	opt.iec958_aes0_pro_fs_rate = IEC958_AES0_PRO_FS_32000;
	silent.size = (10*32000)/1000;
	break;
    default:
	esyslog("S/P-DIF: Invalid sampling rate (%u)!", stream->SampleRate());
	break;
    }
xout:
    return (stream != NULL);
}

inline bool spdif::Frame(frame_t &pcm,
			 const uint_8 *&head, const uint_8 *const tail)
{
    HOLD(thread);
    return (pcm = stream->Frame(head, tail)).burst != NULL;
}

//
// Forward the incoming data to S/P-DIF
//
void spdif::Forward(const uint_8 *const data,
		    const size_t dlen, class cBounce *bounce)
{
    const uint_8 *      head = data;
    const uint_8 *const tail = data + dlen;
    frame_t pcm;
    off_t offset = 0;

    if (test_setup(CLEAR))
	goto xout;

    if (!out || !stream)
	goto xout;

    clear_ctrl(IO);
    while (Frame(pcm, head, tail)) {

	if (ctrlbits & ((1<<FL_NOEXSYNC)|(1<<FL_IO)))
	    check();
	set_ctrl(IO);

	if (ctrlbits & ((1<<FL_FIRST)|(1<<FL_UNDERRUN)|(1<<FL_PAUSE)|(1<<FL_REPEAT)|(1<<FL_OVERRUN))) {
	    // Let us play with the error detection of the receiver and
	    // send some of the current bursts twice with error bit set.
	    static local int repeat;

	    if (test_ctrl(FIRST)) {
		uint_32 duration = (B2F(pcm.size)*1000)/stream->SampleRate();
		int mcnt;
#define USE_MAD_BUFFER_GUARD
#ifndef USE_MAD_BUFFER_GUARD
		//
		// The Mpeg audio decoder requires always two data frames (one full
		// and the start of the next frame), which we have to receive first,
		// to be able to decode the first data frame. Therefore we loose the
		// duration of the almost first resulting PCM frame.
		//
		if (opt.audio) duration = 1;
#endif
		//
		// If current STC value is not valid, we try next frame
		// to get the correct pts offset we need the duration of
		// the skiped frame.
		//
		if (!stream->pts.dvbcheck(duration))
		    continue;

		// Ignore differences with more than +/- 1000 (accordingly DVB specs)
		offset = stream->pts.delay(1000);
		debug_pts("spdif::Forward(): offset = %ld duration=%d\n", offset, duration);

		// Add delay if any
		offset += (10*opt.first);

		// After Pause/Mute in Replay mode our buffer is empty
		// wheras the buffers of VDR are not.
		if (test_and_clear_ctrl(PAUSE)) {
		    offset += pause;
		    pause = 0;
		}

		Hold(thread);			// Hold the lock on the calling thread

		//
		// Time offset to real linear and nonlinear PCM data
		//
		{
		    register int ddelay = offset/10;

		    if (opt.audio) ddelay += opt.adelay;

		    for (int n = 0; n < ddelay; n++) {	// Every burst is 10 ms silent
			switch (check()) {
			case SPDIF_HIGH:
			    Unhold();		// Do not hold lock on calling thread
			    EINTR_RETRY(snd_pcm_wait(out, 10));
			    // fall through
			case SPDIF_OK:
			default:
			    burst(silent);
			    break;
			}
		    }
		}

		Hold(thread);			// Hold the lock on the calling thread

		//
		// Start frame, PCM_WAIT frames seems to be ignored by decoders
		// for linear PCM we use PCM_WAIT2 to get the input buffer for
		// linear PCM of the AV receiver free.
		//
		const frame_t init = stream->Frame(((opt.audio) ? PCM_WAIT2 : PCM_WAIT));

		if (test_setup(LIVE)) {
		    mcnt  = opt.mdelay;
		    count = (mcnt < 7) ? 10 : mcnt + 4;
		} else {
		    mcnt  = 2;
		    count = 10;
		}

		do {
		    switch (check()) {
		    case SPDIF_HIGH:
			// fall through
		    case SPDIF_OK:
			break;
		    default:
			burst(init);
			count--;
			break;
		    }
		} while (count > mcnt);

		if (opt.audio) count += 5;

		Unhold();			// Do not hold lock on calling thread

		if (check() == SPDIF_HIGH)
		    EINTR_RETRY(snd_pcm_wait(out, 10));

		ctrlbits &= ~((1<<FL_FIRST)|(1<<FL_UNDERRUN));
		repeat = 0;
	    }

	    if (test_ctrl(UNDERRUN)) {
		//
		// This recovers the underrun in the case we're getting
		// more input.
		//

		if (test_ctrl(VRPERIOD)) {

		    Hold(thread);		// Hold the lock on the calling thread

		    if (opt.audio) {

			if (!(repeat = (++repeat) % 4)) {
			    // Use a wait frame for filling
			    const frame_t fill = stream->Frame(PCM_WAIT2);
			    stream->SetErr();
			    burst(fill);
			    stream->ClearErr();
			}

		    } else {
			// Let us play with the error detection of the receiver and
			// send some of the current bursts twice with error bit set.
	    
			if (!(repeat = (++repeat) % 4)) {
			    stream->SetErr();
			    burst(pcm);
			    stream->ClearErr();
			}
		    }

		    Unhold();			// Do not hold lock on calling thread

		    // FL_UNDERRUN will be removed by external call of
		    // Synchronize() check()ing the buffers state

		} else {
		    // This also recovers the underrun, nevertheless the
		    // stream to the receiver will get short leaks.

		    Hold(thread);		// Hold the lock on the calling thread

		    snd_pcm_drain(out);
		    snd_pcm_prepare(out);
		    clear_ctrl(UNDERRUN);

		    Unhold();			// Do not hold lock on calling thread
		}

	    } else if (test_ctrl(OVERRUN)) {
		wait.msec(10);
		//
		// This is a workaround for overruns.
		//

		// If outer ring buffer is full then we see an
		// extrem buffer overrun (first shot or long term).
		bool skip = !(bounce->free(2*pcm.pay));

		// Just avoid that burst() is hanging around.
		if (check() == SPDIF_HIGH) {
		    Unhold();			// Do not hold lock on calling thread
		    if (skip)
			// This avoids extrem overruns
			EINTR_RETRY(snd_pcm_wait(out, period));
		    else
			// This may happen if external clock is faster
			// then the quart used by the sound card
			EINTR_RETRY(snd_pcm_wait(out, 10));
		}

		// If outer ring buffer is full then skip the
		// burst to avoid further problems.
		if (skip)
		    continue;

		// FL_OVERRUN will be removed by external call of
		// Synchronize() check()ing the buffers state

	    }

	    // We play now, no Pause nor we Repeat anymore
	    ctrlbits &= ~((1<<FL_PAUSE)|(1<<FL_REPEAT));
	}

	if (count > 0) {
	    //
	    // At least 10 start frames should go around for nonlinear PCM
	    //
	    const unsigned int size = pcm.size;
	    pcm = stream->Frame(((opt.audio) ? PCM_SILENT : PCM_WAIT));
	    pcm.size = size;
	    count--;
	}

	paysize = pcm.pay;		// Remember the last pay load size
	burst(pcm);

    }
xout:
    return;
}

//
// Put out the burst to S/P-DIF of sound card
//
void spdif::burst(const frame_t &pcm)
{
    snd_pcm_uframes_t frames = B2F(pcm.size);
    const uint_32 *data = pcm.burst;
    int eagain = 0;
    uint_32 term = 0;

    if (!writei)
	goto xout;

    set_ctrl(BURSTRUN);
    while((frames > 0) && out) {
	snd_pcm_sframes_t res = 0;
	if ((res = writei(out, (const void *)data, frames)) < 0) {

	    Unhold();			// Do not hold lock on calling thread

	    switch(res) {
	    case -EBUSY:
		EINTR_RETRY(snd_pcm_wait(out, 10));
		// fall through
	    case -EINTR:
		// fall through
	    case -EAGAIN:
		pthread_yield();
		if (eagain++ > 100) {
		    // Real time processes may block
		    sync();
		    wait.msec(1);
		    goto xout;
		}
		continue;
	    case -EPIPE:
		xunderrun();
		set_ctrl(FIRST);
		if (stream)
		    stream->Clear();
		delay = 0;
		goto xout;
	    case -ESTRPIPE:
		xsuspend();
		continue;
	    case -EBADFD:
		if (!out)
		    goto xout;
		(void)snd_pcm_prepare(out);
		continue;
	    default:
		esyslog("S/P-DIF: snd_pcm_writei returned error: %s", snd_strerror(res));
		goto xout;
	    }
	}

	if (res < (snd_pcm_sframes_t)fragsize) {
	    Unhold();			// Do not hold lock on calling thread
	    EINTR_RETRY(snd_pcm_wait(out, -1));
	}

	frames -= res;
	data   += res;
	term   += res;
    }
xout:
    clear_ctrl(BURSTRUN);
    stream->pts.lead(term);

    return;
}

//
// Scan options and open the S/P-DIF device
// 
bool spdif::Open(iec60958 *in, cThread *caller)
{
    char pcm_name[256];
    snd_pcm_access_t access;
    unsigned int channels = 2;
    int err, dir;

    Lock();		// Device locking
    if (out)
	Close();

    ctrlbits = (1<<FL_FIRST)|(1<<FL_NOEXSYNC);
    thread = caller;

    if (!Stream(in))
	goto err_null;

    Hold(thread);	// Hold lock on calling thread

    opt.card   = setup.opt.card;
    opt.device = setup.opt.device;
    if (test_setup(LIVE)) {
	opt.first  = setup.opt.ldelay;
    } else {
	opt.first  = setup.opt.delay;
    }
    opt.mdelay = setup.opt.mdelay;
    opt.adelay = setup.opt.adelay;
    opt.mmap   = setup.opt.mmap;
    if (setup.opt.variable)
	set_ctrl(VRPERIOD);
    opt.type   = setup.opt.type;
    if (test_setup(AUDIO)) {
	opt.audio = true;
	ch.status[0] = 0;
    } else {
	opt.audio = false;
	ch.status[0] = IEC958_AES0_NONAUDIO;
    }

    // Note that most alsa sound card drivers uses little endianess
    if (opt.mmap) { 
	writei = snd_pcm_mmap_writei;
	access = SND_PCM_ACCESS_MMAP_INTERLEAVED;
    } else {
	writei = snd_pcm_writei;
	access = SND_PCM_ACCESS_RW_INTERLEAVED;
    }

    switch (opt.type) {
    default:
    case SPDIF_CON:
	ch.status[0] |= (IEC958_AES0_CON_EMPHASIS_NONE);
	ch.status[1]  = (IEC958_AES1_CON_ORIGINAL | IEC958_AES1_CON_PCM_CODER);
	ch.status[2]  =  0;
	ch.status[3]  = (opt.iec958_aes3_con_fs_rate);
	break;
    case SPDIF_PRO:
	ch.status[0] |= (IEC958_AES0_PROFESSIONAL | IEC958_AES0_PRO_EMPHASIS_NONE |
			    opt.iec958_aes0_pro_fs_rate);
	ch.status[1]  = (IEC958_AES1_PRO_MODE_NOTID | IEC958_AES1_PRO_USERBITS_NOTID);
	ch.status[2]  = (IEC958_AES2_PRO_WORDLEN_NOTID);
	ch.status[3]  =  0;
	break;
    }

    if ((err = snprintf(&pcm_name[0], 255, "iec958:AES0=0x%.2x,AES1=0x%.2x,AES2=0x%.2x,AES3=0x%.2x,CARD=%1d",
			ch.status[0], ch.status[1], ch.status[2], ch.status[3], opt.card)) <= 0)
	goto err_null;

    snd_output_stdio_open(&log, "/dev/null", "a");
    if ((err = snd_pcm_open(&out, pcm_name, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
	// Next try

	snd_pcm_info_t 	*info;
	snd_ctl_elem_value_t *ctl;
	snd_ctl_t *ctl_handle;
	char ctl_name[12];
	int ctl_card;

	if ((err = snprintf(&pcm_name[0], 255, "hw:%1d,%1d", opt.card, opt.device) <= 0))
	    goto err_null;

	snd_output_close(log);
	snd_output_stdio_attach(&log, stderr, 0);
#ifdef DEBUG2
	snd_output_printf(log, "S/P-DIF: Open device %s and try to configure none audio playback ", pcm_name);
	snd_output_printf(log, "(IEC958:AES0=0x%.2x,AES1=0x%.2x,AES2=0x%.2x,AES3=0x%.2x)\n",
			  ch.status[0], ch.status[1], ch.status[2], ch.status[3]);
#endif
	if ((err = snd_pcm_open(&out, pcm_name, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
	    esyslog("S/P-DIF: sound open: %s", snd_strerror(err));
	    goto err_log;
	}

	snd_pcm_info_alloca(&info);
	if ((err = snd_pcm_info(out, info)) < 0) {
	    esyslog("S/P-DIF: sound info: %s", snd_strerror(err));
	    goto err_out;
	}

	snd_ctl_elem_value_alloca(&ctl);
	snd_ctl_elem_value_set_interface(ctl, SND_CTL_ELEM_IFACE_PCM);
	snd_ctl_elem_value_set_device(ctl, snd_pcm_info_get_device(info));
	snd_ctl_elem_value_set_subdevice(ctl, snd_pcm_info_get_subdevice(info));
	snd_ctl_elem_value_set_name(ctl, SND_CTL_NAME_IEC958("", PLAYBACK, PCM_STREAM));
	snd_ctl_elem_value_set_iec958(ctl, &ch);

	ctl_card = snd_pcm_info_get_card(info);
	if (ctl_card < 0) {
	    esyslog("S/P-DIF: Unable to setup the IEC958 (S/PDIF) interface - PCM has no assigned card");
	    goto __diga_end;
	}
	sprintf(ctl_name, "hw:%d", ctl_card);
	if ((err = snd_ctl_open(&ctl_handle, ctl_name, 0)) < 0) {
	    esyslog("S/P-DIF: Unable to open the control interface '%s': %s", ctl_name, snd_strerror(err));
	    goto __diga_end;
	}
	if ((err = snd_ctl_elem_write(ctl_handle, ctl)) < 0) {
	    esyslog("S/P-DIF: Unable to update the IEC958 control: %s", snd_strerror(err));
	    goto __diga_end;
	}
	snd_ctl_close(ctl_handle);
__diga_end:
	; // make compiler happy
    }
    {
	int fifo = 0;
	unsigned int frag;
	snd_pcm_uframes_t part, period_min, period_max, first;
	snd_pcm_hw_params_t *hwparams;
	snd_pcm_sw_params_t *swparams;
	unsigned int rate = stream->SampleRate();
	snd_pcm_uframes_t val = burst_size*periods;

	// Number of digital audio frames in hw buffer
	snd_pcm_uframes_t period_size = 0;
	snd_pcm_sframes_t err;

	snd_pcm_hw_params_alloca(&hwparams);
	snd_pcm_sw_params_alloca(&swparams);

	if ((err = snd_pcm_hw_params_any(out, hwparams)) < 0) {
	    esyslog("S/P-DIF: Broken configuration for this PCM: no configurations available");
	    goto err_out;
	}
	if ((err = snd_pcm_hw_params_set_access(out, hwparams, access)) < 0) {
	    esyslog("S/P-DIF: Access type not available");
	    goto err_out;
	}
	if ((err = snd_pcm_hw_params_set_format(out, hwparams, format)) < 0) {
	    esyslog("S/P-DIF: Sample format not available");
	    goto err_out;
	}

	if ((err = snd_pcm_hw_params_set_channels(out, hwparams, channels)) < 0) {
	    esyslog("S/P-DIF: Channels count not avaible");
	    goto err_out;
	}
#define SND_DIR(dir)	(((dir) > 0) ? ("more") : ("less"))
	dir = 0;
	if ((err = snd_pcm_hw_params_set_rate_near(out, hwparams, &rate, &dir)) < 0) {
	    esyslog("S/P-DIF: Sample rate %u not available: %s",
		    stream->SampleRate(), snd_strerror(err));
	    goto err_out;
	}
	if (dir)
	    dsyslog("S/P-DIF: Sample rate %d is %s than %d\n",
		    rate, SND_DIR(dir), stream->SampleRate());

#define MMAP_BURST	B2F(getpagesize())
	// Try to use an integer divisor of the burst size as period size
	// to avoid not needed loops and wait states in burst()

	dir = 0;
	if ((err = snd_pcm_hw_params_get_period_size_min(hwparams, &period_min, &dir)) < 0) {
	    esyslog("S/P-DIF: min period size not available: %s", snd_strerror(err));
	    goto err_out;
	}
	if (dir)
	    dsyslog("S/P-DIF: Minimal period size is %s than %ld\n", SND_DIR(dir), period_min);

	dir = 0;
	if ((err = snd_pcm_hw_params_get_period_size_max(hwparams, &period_max, &dir)) < 0) {
	     esyslog("S/P-DIF: max period size not available: %s", snd_strerror(err));
	}
	if (dir)
	    dsyslog("S/P-DIF: Maximal period size is %s than %ld\n", SND_DIR(dir), period_max);

	if (period_max > burst_size)
	    period_max = burst_size;

	if (opt.mmap)
	    period_max = MMAP_BURST;

	
	frag = 0;
	do {
	    frag++;
	    part = burst_size/frag;

	    if (part > period_max)
		continue;

	    if (part < period_min) {
		err = -ECANCELED;
		break;
	    }

	    if (part*frag != burst_size)
		continue;

	    // function returns less than 0 on error or greater than 0
	    if ((err = snd_pcm_hw_params_set_period_size(out, hwparams, part, 0)) == 0)
		break;

	} while (frag < 10);

	if (err < 0) {
	    esyslog("S/P-DIF: No valid period size available: %s", snd_strerror(err));
	    period_size = 0;
	    goto err_out;
	}

	if (frag > 1) {
	    // function returns less than 0 on error or greater than 0
	    if ((err = snd_pcm_hw_params_set_buffer_size_near(out, hwparams, &val)) < 0) {
		esyslog("S/P-DIF: Buffer size %lu not available: %s",
			(unsigned long int)(burst_size*periods), snd_strerror(err));
		goto err_out;
	    }
	} else {
	    // function returns less than 0 on error or greater than 0
	    dir = 0;
	    if ((err = snd_pcm_hw_params_set_periods(out, hwparams, periods, dir)) < 0) {
		esyslog("S/P-DIF: Period count not available: %s", snd_strerror(err));
		goto err_out;
	    }
	    if (dir)
		dsyslog("S/P-DIF: Period count %s than %lu\n", SND_DIR(dir), periods);
 	}

	// function returns less than 0 on error or greater than 0
	dir = 0;
	if ((err = snd_pcm_hw_params_get_period_size(hwparams, &part, &dir)) < 0)
	{
	    esyslog("S/P-DIF: Period size not gotten: %s", snd_strerror(err));
	    goto err_out;
	}
	err = (int)part;
	fragsize = err;
	err *= frag;
	if (frag > 1) {
	    if (dir)
		dsyslog("S/P-DIF: Period size got is %s than %ld\n", SND_DIR(dir), part);
	} else {
	    if (dir)
		dsyslog("S/P-DIF: Burst  size got is %s than %ld\n", SND_DIR(dir), burst_size);
	}
#undef MMAP_BURST
#undef SND_DIR

	if ((int)burst_size != err) {
	    esyslog("S/P-DIF: Period size not set: %s", snd_strerror(err));
	    goto err_out;
	}
	period_size = (snd_pcm_uframes_t)err;
#if defined (SPDIF_SAMPLE_MAGIC) && (SPDIF_SAMPLE_MAGIC > 0)
        fifo = snd_pcm_hw_params_get_fifo_size(hwparams);
	if (fifo <= 0)
	    fifo = SPDIF_SAMPLE_MAGIC;
#endif

	// function returns less than 0 on error or greater than 0
	if ((err = snd_pcm_hw_params_get_buffer_size(hwparams, &part)) < 0)
	{
	    esyslog("S/P-DIF: Buffer size not set: %s", snd_strerror(err));
	    goto err_out;
	}
	err = (int)part;
	if (period_size * periods != (snd_pcm_uframes_t)err) {
	    esyslog("S/P-DIF: Buffer size not set: %s", snd_strerror(err));
	    goto err_out;
	}
	{
	    snd_pcm_uframes_t tenth;
	    buffer_size = err;
	    tenth = buffer_size/10;
	    buf.upper = buffer_size - 3*tenth;
	    buf.lower = (opt.audio ? 2 : 4)*tenth;
	    buf.high  = buffer_size - tenth;
	    buf.alarm = tenth/3;
	}

	if (snd_pcm_hw_params_can_pause(hwparams) == 1)
	    set_ctrl(CANPAUSE);

	if ((err = snd_pcm_hw_params(out, hwparams)) < 0) {
	    esyslog("S/P-DIF: Cannot set buffer size");
	    snd_pcm_hw_params_dump(hwparams, log);
	    goto err_out;
	}

	if ((err = snd_pcm_sw_params_current(out, swparams)) < 0) {
	    esyslog("S/P-DIF: Cannot get soft parameters: %s", snd_strerror(err));
	    goto err_out;
	}

	if ((err =  snd_pcm_sw_params_set_xfer_align(out, swparams, fifo)) < 0)
	    esyslog("S/P-DIF: Aligned period size not available: %s", snd_strerror(err));

	if (opt.audio) {
	    // Set start timings
	    if ((err = snd_pcm_sw_params_set_sleep_min(out, swparams, 1)) < 0)
		esyslog("S/P-DIF: Minimal sleep time not available: %s", snd_strerror(err));

	    // Set silence size to silent.size (10ms)
	    if ((err = snd_pcm_sw_params_set_silence_size(out, swparams, B2F(silent.size))) < 0)
		esyslog("S/P-DIF: silence threshold not available: %s", snd_strerror(err));

	    // If near 10ms underrun play silence (MUST be the same as silence size)
	    if ((err = snd_pcm_sw_params_set_silence_threshold(out, swparams, B2F(silent.size))) < 0)
		esyslog("S/P-DIF: silence threshold not available: %s", snd_strerror(err));

	    // We need at least one stereo sample
	    if ((err = snd_pcm_sw_params_set_avail_min(out, swparams, 2)) < 0)
		esyslog("S/P-DIF: Minimal period size not available: %s", snd_strerror(err));

	} else {
	    // Set start timings
	    if ((err = snd_pcm_sw_params_set_sleep_min(out, swparams, 0)) < 0)
		esyslog("S/P-DIF: Minimal sleep time not available: %s", snd_strerror(err));

	    // AC3 and DTS require defined PCM sample lenght
	    if ((err = snd_pcm_sw_params_set_avail_min(out, swparams, period)) < 0)
		esyslog("S/P-DIF: Minimal period size not available: %s", snd_strerror(err));
	}

	first = silent.size*(opt.first+1);
	if (first > buf.upper)
	    first = buf.upper;

	if ((err = snd_pcm_sw_params_set_start_threshold(out, swparams, first)) < 0)
	    esyslog("S/P-DIF: Start threshold not available: %s", snd_strerror(err));

	if ((err = snd_pcm_sw_params_set_tstamp_mode  (out, swparams, SND_PCM_TSTAMP_MMAP)) < 0)
	    esyslog("S/P-DIF: Time stamp mode not available: %s", snd_strerror(err));

	if ((err = snd_pcm_sw_params(out, swparams)) < 0) {
	    esyslog("S/P-DIF: Cannot set soft parameters: %s", snd_strerror(err));
//	    snd_pcm_sw_params_dump(swparams, log);
	    goto err_out;
	}
#ifdef DEBUG2
	snd_pcm_sw_params_dump(swparams, log);
	snd_pcm_dump(out, log);
#endif
    }  

    // Status informations, hold over the full session.
    if ((err = snd_pcm_status_malloc(&status)) < 0) {
	esyslog("S/P-DIF: unable to prepare PCM handle: %s\n", snd_strerror(err));
    }

    if ((err = snd_pcm_prepare(out)) < 0) {
	esyslog("S/P-DIF: unable to prepare PCM handle: %s\n", snd_strerror(err));
	goto err_status;
    }

    Unhold();
    Unlock();
    return true;

err_status:
    snd_pcm_status_free(status);
err_out:
    snd_pcm_close(out);
err_log:
    snd_output_close(log);
err_null:
    log = NULL;
    out = NULL;
    status = NULL;
    esyslog("S/P-DIF: unable to establish BitStreamOut for none audio PCM\n");

    Unhold();
    Unlock();
    return false;
}

//
// Close the S/P-DIF device
// 
void spdif::Close(cThread *caller)
{
    bool exit = true;
    snd_pcm_t *tmp = out;

    Lock();
    if (!out)
        goto err;

    Unhold();					// Leave any thead lock if any
    while (test_ctrl(BURSTRUN))
	wait.msec(1);
    if (caller) Hold(caller);			// Hold lock for pause or stop frame
    Clear(exit);
    Unhold();					// leave holded lock

    thread = NULL;
    out    = NULL;
    stream = NULL;
    delay  = 0;
    pause  = 0;

    // Cleanup
    snd_pcm_nonblock(tmp, SND_PCM_NONBLOCK);
    wait.msec(1);
    if (opt.mmap) {
	// Some ALSA version have problems with mmap counter
	// in kernel space, avoid hanging at snd_pcm_close();
	int fd = dup(2); close(2);
//	block_signals();
	snd_pcm_hw_free(tmp);
//	leave_signals();
	dup2(fd, 2); close(fd);
	wait.msec(1);
    }
    errno = 0;
    snd_pcm_close(tmp);

    // Close log
    wait.msec(1);
    snd_output_close(log);
    snd_pcm_status_free(status);
    log    = NULL;
    status = NULL;
err:
    Unlock();
}

//
// Clear function, if exit is true send STOP frame
//
void spdif::Clear(bool exit)
{
    snd_pcm_sframes_t err;

    if (!out)
	goto xout;

    set_ctrl(PAUSE);				// Mute all

    if (test_ctrl(FIRST))
	goto xout;

    if ((err = snd_pcm_status(out, status)) < 0) {
	esyslog("S/P-DIF: clear: status error: %s", snd_strerror(err));
	goto xout;
    }

    switch (snd_pcm_status_get_state(status)) {
    case SND_PCM_STATE_RUNNING:

	if (exit) {
	    clear_ctrl(PAUSE);
	    pause = 0;				// VDR has cleared its buffers

	    if (stream) {
		// Say decoder to wait for pause or stop
		const frame_t stop = (opt.audio) ? stream->Frame(PCM_SILENT) : stream->Frame(PCM_STOP);
		burst(stop);
	    }

	    Unhold();				// Do not hold lock on calling thread

	    if (opt.mmap) {
		// In case of mmap access we've to wait
		(void)snd_pcm_hwsync(out);
		delay = snd_pcm_avail_update(out);
		int maxloop = delay/fragsize + 1;
		while ((delay > 0) && (maxloop-- > 0)) {
		    delay = 0;
		    EINTR_RETRY(snd_pcm_wait(out, -1));
		    delay = snd_pcm_avail_update(out);
		}
	    }
	    if ((err = snd_pcm_drop(out)) < 0) {
		switch (err) {
		case -ESTRPIPE:
		    xsuspend();
		    break;
		default:
		    esyslog("S/P-DIF: clear: drop error: %s", snd_strerror(err));
		    break;
		}
	    }
	} else {

	    Unhold();				// Leave any thead lock if any

	    //
	    // In Replay mode VDR does _not_ empty its buffers for Pause
	    //
	    do {
		if (test_setup(LIVE))		// Only in Replay mode
		    break;
		if (!test_setup(STILLPIC))	// Only for still pictures
		    break;
		if (test_ctrl(FIRST))		// Already stopped
		    break;
		if (!stream)	 		// No stream open
		    break;

		// Calculate addon of start delay in ms
		(void)snd_pcm_hwsync(out);
		if (snd_pcm_delay(out, &pause) < 0)
		    pause = 0;
		else
		    pause = (pause*1000)/stream->SampleRate();
		clear_setup(STILLPIC);

	    } while (0);

	    if ((err = snd_pcm_drain(out)) < 0) {
		switch (err) {
		case -ESTRPIPE:
		    xsuspend();
		    break;
		default:
		    esyslog("S/P-DIF: clear: drain error: %s", snd_strerror(err));
		    break;
		}
	    }
	}
	// fall through
    default:
    case SND_PCM_STATE_XRUN:
	if ((err = snd_pcm_prepare(out)) < 0)
	    esyslog("S/P-DIF: clear: prepare error: %s", snd_strerror(err));
	// fall through
    case SND_PCM_STATE_PREPARED:
	set_ctrl(FIRST);
	if (stream)
	    stream->Clear();
	break;
    } // switch (snd_pcm_status_get_state())
xout:
    return;
}

//
// We sleep on PCM stream and watch during this on events on the
// incomming data.
#define SPDIF_REPEAT	320
#define SPDIF_TIMEOUT	(3000 - SPDIF_REPEAT)
bool spdif::Synchronize(class cBounce *bounce)
{
    snd_pcm_sframes_t err;
    bool ready = true;
    int wait;

    if (test_setup(CLEAR))
	return false;

    if (test_ctrl(FIRST))
	goto do_wait;

    Unhold();				// Leave any thead lock if any

    clear_ctrl(NOEXSYNC);

repeat:
    if (!out)
	goto do_wait;

    if ((err = snd_pcm_status(out, status)) < 0) {
	esyslog("S/P-DIF: synchronize: status error: %s", snd_strerror(err));
	goto xout;
    }

    switch (snd_pcm_status_get_state(status)) {
    case SND_PCM_STATE_RUNNING:
	switch (check(status)) {
	case SPDIF_HIGH:
	    EINTR_RETRY(snd_pcm_wait(out, -1));
	    check();
	    if (!delay) break;
	    // else fall through
	case SPDIF_OK:
	    wait = (period*(delay-buf.alarm))/burst_size;
	    if (wait < 2*period) {
		if (wait > 0 && bounce->poll(wait))
		    goto xout;
	    } else {
		wait = 2*period;
		if (bounce->poll(wait))
		    goto xout;
		if (xrepeat())		// Repeat last frame for a while
		    goto repeat;
	    }
	default:
	case SPDIF_LOW:
	    if ((err = snd_pcm_drain(out)) < 0) {
		switch (err) {
		case -ESTRPIPE:
		    xsuspend();
		    break;
		default:
		    esyslog("S/P-DIF: synchronize: drain error: %s", snd_strerror(err));
		    break;
		}
	    }
	    break;
	}   // switch(check())
	// fall through
    default:
    case SND_PCM_STATE_XRUN:
	if ((err = snd_pcm_prepare(out)) < 0)
	    esyslog("S/P-DIF: synchronize: prepare error: %s", snd_strerror(err));
	// fall through
    case SND_PCM_STATE_PREPARED:
	set_ctrl(FIRST);
	if (stream)
	    stream->Clear();
	goto do_wait;
	break;
    }	// switch (snd_pcm_status_get_state())

xout:
    return ready;

do_wait:
    if (test_ctrl(PAUSE))		// Pause active
	wait = 30;
    else
	wait = SPDIF_TIMEOUT;
    ready = bounce->poll(wait);

    return (test_ctrl(PAUSE)) ? true : ready;
}

inline bool spdif::xrepeat(void)
{
    bool ret = true;
    frame_t pcm;

    if (test_ctrl(REPEAT)) {
	struct timeval now, diff;
	gettimeofday(&now, NULL);
	timersub(&now, &xrstart, &diff);
        ret = ((diff.tv_sec*1000 + diff.tv_usec/1000) < SPDIF_REPEAT);
    } else {
	set_ctrl(REPEAT);
	gettimeofday(&xrstart, NULL);
    }
    if (ret && stream && (pcm = stream->Frame()).burst)
	burst(pcm);

    return ret;
}
#undef SPDIF_REPEAT
#undef SPDIF_TIMEOUT

//
// Internal helper function in case of sound card buffer underrun
//
inline void spdif::xunderrun(void)
{
    snd_pcm_sframes_t res;

    if (!out)
	goto xout;

    Unhold();				// Leave any thead lock if any

    if ((res = snd_pcm_status(out, status))<0) {
	esyslog("S/P-DIF: status error: %s", snd_strerror(res));
	goto xout;
    }
    if (snd_pcm_status_get_state(status) == SND_PCM_STATE_XRUN) {
	struct timeval now, diff, tstamp;
	gettimeofday(&now, NULL);
	snd_pcm_status_get_trigger_tstamp(status, &tstamp);
	timersub(&now, &tstamp, &diff);
	dsyslog("S/P-DIF: xunderrun!!! (at least %.3f ms long)",
		diff.tv_sec * 1000 + diff.tv_usec / 1000.0);
	if (!out)
	    goto xout;
	if ((res = snd_pcm_prepare(out))<0) {
	    esyslog("S/P-DIF: xunderrun: prepare error: %s", snd_strerror(res));
	    goto xout;
	}
	// ok, data should be accepted again
    }
xout:
    return;
}

//
// Internal helper function in case of sound card buffer suspend
//
inline void spdif::xsuspend (void)
{
    snd_pcm_sframes_t res;

    if (!out)
	goto xout;

    Unhold();				// Leave any thead lock if any

    if ((res = snd_pcm_status(out, status))<0) {
	esyslog("S/P-DIF: status error: %s", snd_strerror(res));
	goto xout;
    }
    if (snd_pcm_status_get_state(status) == SND_PCM_STATE_SUSPENDED) {
	esyslog("S/P-DIF: xsuspend!!! trying to resume");
	while (out && (res = snd_pcm_resume(out)) == -EAGAIN)
	    wait.msec(100);
	if (!out)
	    goto xout;
	if ((res = snd_pcm_prepare(out))<0) {
	    esyslog("S/P-DIF: xsuspend: prepare error: %s", snd_strerror(res));
	    goto xout;
	}
	// ok, data should be accepted again
    }
xout:
    return;
}

//
// Internal helper to save signal set
//
inline void spdif::block_signals(void)
{
    sigset_t blkset;
    if (sigfillset(&blkset) || sigdelset(&blkset, SIGALRM) ||
	pthread_sigmask(SIG_BLOCK, &blkset, &oldset))
	esyslog("S/P-DIF: can not block signals: %s", strerror(errno));
}

//
// Internal helper to restore signal set
//
inline void spdif::leave_signals(void)
{
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);
}

//
// Internal helper to check the state of the sound card buffer
//
int spdif::check(snd_pcm_status_t * status)
{
    int grade = SPDIF_LOW;
    delay = 0;

    if (test_ctrl(FIRST))	// No AC3 running is LOW
	return grade;

    if (status)
	delay = snd_pcm_status_get_delay(status);
    else {
	(void)snd_pcm_hwsync(out);
	if (opt.mmap || test_ctrl(FIRST))
	    delay = snd_pcm_avail_update(out);
	else
	    snd_pcm_delay(out, &delay);
    }

    if (delay < 0) {
	xunderrun();
	set_ctrl(FIRST);
	if (stream)
	    stream->Clear();
	delay = 0;
	goto xout;
    }

    // Underrun dection for setting variable period size
    if      ((snd_pcm_uframes_t)delay <= buf.lower)
	set_ctrl(UNDERRUN);
    else if ((snd_pcm_uframes_t)delay >  buf.upper)
	clear_ctrl(UNDERRUN);

    // Buffer check
    clear_ctrl(OVERRUN);
    if      ((snd_pcm_uframes_t)delay <  buf.alarm)
	grade = SPDIF_LOW;
    else if ((snd_pcm_uframes_t)delay >  buf.high) {
	set_ctrl(OVERRUN);
	grade = SPDIF_HIGH;
    } else
	grade = SPDIF_OK;
xout:
    return grade;
}

//
// Return available space for next PCM frames and avoid
// to block burst() by overrun the sound cards buffer
//
size_t spdif::Available(const size_t max)
{
    ssize_t avail = sizeof(int);
    snd_pcm_uframes_t initial;

    if (!out)
	goto xout;

    if (paysize) {
	switch (check()) {
	default:
	case SPDIF_LOW:
	    if (test_ctrl(FIRST))
		break;
	case SPDIF_OK:			// Fill up to upper boundary (and empty bounce buffer)
	    initial = delay + 3*burst_size;
	    if (initial >= buffer_size)
		goto xout;		// hold data in bounce buffer
	    avail = buffer_size - initial;
	    avail = paysize * (avail/burst_size);
	    break;
	case SPDIF_HIGH:		// Take less as a frame (hold data in bounce buffer)
	    avail = paysize>>2;
	    break;
	}
    }

    if (test_ctrl(FIRST)) {		// Be able to start without delay
	initial = silent.size*opt.first + 3*burst_size;
	if (initial >= buffer_size)
	    goto xout;			// hold data in bounce buffer
	avail = buffer_size - initial;
	avail = ((paysize) ? paysize * (avail/burst_size) : F2B(avail));
    }

    if (avail <= 0)
	avail = sizeof(int);

    if ((size_t)avail > max)
	avail = max;
xout:
    return avail;
}

//
// Set Pause bit
//
void spdif::Pause(const bool onoff)
{
    if (onoff) {
	set_ctrl(PAUSE);
    }   // else {
	// FL_PAUSE will be unset in Forward()
	// }
}
