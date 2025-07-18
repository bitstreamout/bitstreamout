/*
 * mp2.c:	Use the S/P-DIF interface for redirecting MP2 stream
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
 * Copyright (C) 2003 Sven Goethel, <sven@jausoft.com>
 */

#include <netinet/in.h>
#include <vdr/thread.h>
#include "types.h"
#include "mp2.h"
#include "shm_memory_tool.h"

#define USE_LAST_FRAME		1	// In case of CRC error

//#define DEBUG_MP2
#ifdef  DEBUG_MP2
# define debug_mp2(args...) fprintf(stderr, args)
#else
# define debug_mp2(args...)
#endif

// --- cMP2 : Scanning MP2 stream for counting and decoding into PCM frames ------------

cMP2 mp2(48000);

const uint_32 cMP2::samplerate_table[3] = {44100, 48000, 32000};
const uint_32 cMP2::bitrate_table[5][15] = {
    // MPEG-1
    {0,  32000,  64000,  96000, 128000, 160000, 192000, 224000,
	256000, 288000, 320000, 352000, 384000, 416000, 448000 }, // Layer I
    {0,  32000,  48000,  56000,  64000,  80000,  96000, 112000,
	128000, 160000, 192000, 224000, 256000, 320000, 384000 }, // Layer II
    {0,  32000,  40000,  48000,  56000,  64000,  80000,  96000,
	112000, 128000, 160000, 192000, 224000, 256000, 320000 }, // Layer III
    // MPEG-2 LSF
    {0,  32000,  48000,  56000,  64000,  80000,  96000, 112000,
	128000, 144000, 160000, 176000, 192000, 224000, 256000 }, // Layer I
    {0,   8000,  16000,  24000,  32000,  40000,  48000,  56000,
	 64000,  80000,  96000, 112000, 128000, 144000, 160000 }  // Layer II&III
};
const uint_16 cMP2::magic = 0xffe0;

cMP2::cMP2(unsigned int rate)
: iec60958(rate, MP2_BURST_SIZE, 0),		// 24ms buffer
running(false)
{
    reset_scan();
    reset_count();
}

cMP2::~cMP2(void)
{
    Release();
}

inline void cMP2::parse_syncinfo(mp2info_t &syncinfo, const uint_8 *data)
{
    uint_32 bit_rate;
    int index;
    uint_8 padding;

    syncinfo.frame_size = 0;

    syncinfo.ext   = ((data[1] & 0x10) >> 4) == 0;
    syncinfo.lsf   = ((data[1] & 0x08) >> 3) == 0;
    syncinfo.layer = 4 - ((data[1] & 0x06) >> 1);

    if ((syncinfo.layer == 4) || (!syncinfo.lsf && syncinfo.ext))
	return;

    if ((index = (data[2] & 0x0c)) == 0x0c)
	return;
    index >>= 2;

    syncinfo.sampling_rate = samplerate_table[index];
    if (syncinfo.lsf) {
	syncinfo.sampling_rate /= 2;
	if (syncinfo.ext)
	    syncinfo.sampling_rate /= 2;
    }

    if ((index = (data[2] & 0xf0)) == 0xf0)
	return;
    index >>= 4;

    if (syncinfo.lsf)
	bit_rate = bitrate_table[3 + (syncinfo.layer >> 1)][index];
    else
	bit_rate = bitrate_table[syncinfo.layer - 1][index];

    padding = (data[2] & 0x02) >> 1;

    if (syncinfo.layer == 1) {
	syncinfo.frame_size = 4*((12 * bit_rate / syncinfo.sampling_rate) + padding);
	syncinfo.burst_size = 1536;
    } else {
	const uint_16 slots = ((syncinfo.layer == 3) && syncinfo.lsf) ? 72 : 144;
	syncinfo.frame_size = (slots * bit_rate / syncinfo.sampling_rate) + padding;
	syncinfo.burst_size = 32 * slots;
    }
}

inline mad_fixed_t cMP2::Prng(const mad_fixed_t state)
{
    return (state * 0x0019660dL + 0x3c6ef35fL) & 0xffffffffL;
}

sint_32 cMP2::Dither(mad_fixed_t sample, dither_t &dither)
{
    const uint_32 scalebits = MAD_F_FRACBITS + 1 - SAMPLE_DEPTH;
    const mad_fixed_t mask = (1L << scalebits) - 1;
    mad_fixed_t output, random;

    sample += dither.error[0] - dither.error[1] + dither.error[2];

    dither.error[2] = dither.error[1];
    dither.error[1] = dither.error[0] >> 1;

    // bias
    output = sample + (1L << (scalebits - 1));

    // dither
    random  = Prng(dither.random);
    output += (random & mask) - (dither.random & mask);

    dither.random = random;

    // clip
    if (output > MAX_SPL) {
	output = MAX_SPL;
	if (sample > MAX_SPL)
	    sample = MAX_SPL;
    } else if (output < MIN_SPL) {
	output = MIN_SPL;
	if (sample < MIN_SPL)
	    sample = MIN_SPL;
    }

    // quantize
    output &= ~mask;

    // error feedback
    dither.error[0] = sample - output;

    // scale
    return output >> scalebits;
}

sint_32 cMP2::Round(mad_fixed_t sample, dither_t &dither)
{
    const uint_32 scalebits = MAD_F_FRACBITS + 1 - SAMPLE_DEPTH;

    // round
    sample += (1L << (scalebits - 1));

    // clip
    if (sample > MAX_SPL)
	sample = MAX_SPL;
    else if (sample < MIN_SPL)
	sample = MIN_SPL;

    // scale
    return sample >> scalebits;
}

inline ssize_t cMP2::Sample(uint_8 *data, size_t cnt)
{
    ssize_t len = B2F(cnt);
    struct mad_pcm *pcm;

    mad_synth_frame(&synth, &frame);
    pcm = &synth.pcm;

    if (pcm->channels > 2) {
	esyslog("MP2PCM: ** Invalid channel number found - try to syncing **");
	goto fail;
    }

    if (pcm->samplerate != sample_rate) {
	esyslog("MP2PCM: ** Invalid sample rate found - try to syncing **");
	goto fail;
    }

    if (len > pcm->length)
	len = pcm->length;

#if !defined(_GNU_SOURCE)
# define pcm_cast(a)     (unsigned char)((a) & 0xff)
    if (pcm->channels > 1) {
	const mad_fixed_t * left  = pcm->samples[0];
	const mad_fixed_t * right = pcm->samples[1];
	const uint_8 *end = data + F2B(len);

	while (data < end) {
	    sint_32 sample;
	    sample = Scale(*left++,  dith[0]);
# ifndef WORDS_BIGENDIAN
	    *data++ = pcm_cast(sample >> 0);
	    *data++ = pcm_cast(sample >> 8);
# else
	    *data++ = pcm_cast(sample >> 8);
	    *data++ = pcm_cast(sample >> 0);
# endif
	    sample = Scale(*right++, dith[1]);
# ifndef WORDS_BIGENDIAN
	    *data++ = pcm_cast(sample >> 0);
	    *data++ = pcm_cast(sample >> 8);
# else
	    *data++ = pcm_cast(sample >> 8);
	    *data++ = pcm_cast(sample >> 0);
# endif
	}
    } else {
	const mad_fixed_t * left  = pcm->samples[0];
	const uint_8 *end = data + F2B(len);

	while (data < end) {
	    sint_32 sample = Scale(*left++,  dith[0]);
# ifndef WORDS_BIGENDIAN
	    *data++ = pcm_cast(sample >> 0);
	    *data++ = pcm_cast(sample >> 8);
	    *data++ = pcm_cast(sample >> 0);
	    *data++ = pcm_cast(sample >> 8);
# else
	    *data++ = pcm_cast(sample >> 8);
	    *data++ = pcm_cast(sample >> 0);
	    *data++ = pcm_cast(sample >> 8);
	    *data++ = pcm_cast(sample >> 0);
# endif
	}
    }

#else // if defined(_GNU_SOURCE)
# define pcm_cast(a)     (unsigned short)((a) & 0xffff)
    if (pcm->channels > 1) {
	const mad_fixed_t * left  = pcm->samples[0];
	const mad_fixed_t * right = pcm->samples[1];
	uint_16 *to = (uint_16 *)data;
	const uint_16 *end = to + (len << 1);

	while (to < end) {
	    sint_32 sample;
	    sample = Scale(*left++,  dith[0]);
	    *to++ = pcm_cast(sample);

	    sample = Scale(*right++, dith[1]);
	    *to++ = pcm_cast(sample);
	}

    } else {
	const mad_fixed_t * left  = pcm->samples[0];
	uint_16 *to = (uint_16 *)data;
	const uint_16 *end = to + (len << 1);

	while (to < end) {
	    sint_32 sample = Scale(*left++,  dith[0]);
	    *to++ = pcm_cast(sample);
	    *to++ = pcm_cast(sample);
	}
    }
#endif
#undef pcm_cast

    return F2B(len);
fail:
    return -1;
}

inline bool cMP2::BufferGuard(void)
{
    bool ret = true;

    if (!stream.next_frame)
	goto out;

    if ((stream.bufend - stream.next_frame) == MAD_BUFFER_GUARD)
	goto out;

    ret = false;

out:
    return ret;
}

size_t cMP2::Stream(uint_8 *data, size_t cnt)
{
    size_t todo = MAD_BUFFER_MDLEN;	// mad.h says: MAD_BUFFER_MDLEN = 511 + 2048 + MAD_BUFFER_GUARD

    if (!running) Start();

    if (!BufferGuard()) {
	//
	// We always start with a buffer which starts its self with
	// the mp2 audio sync word and with MAD_BUFFER_GUARD bytes
	// for the next mp2 frame appended to the current mp2 frame.
	// This requires that the previous rest is also of the size
	// of MAD_BUFFER_GUARD or simply zero at start.
	//
	esyslog("MP2PCM: ** Invalid rest found - try to syncing **");
	return 0;
    }

    if (cnt > todo) {
	esyslog("MP2PCM: ** Invalid frame size - try to syncing **");
	return 0;
    }

    if (todo > cnt) todo = cnt;

    mad_stream_buffer(&stream, data, todo);
    stream.error = MAD_ERROR_NONE;

    return todo;
}

int cMP2::Decode(void)
{
    int todo = MP2_DATA;

    if (!stream.buffer || stream.error == MAD_ERROR_BUFLEN)
	goto out;
					// Scan always the header due our
    frame.header.flags &= ~MAD_FLAG_INCOMPLETE;	// buffer guard workaround

    todo = MP2_PLAY;			// We can play
    if (mad_frame_decode(&frame, &stream) == 0)
	goto out;
    if (stream.error == MAD_ERROR_NONE)
	goto out;

    todo = MP2_DATA;			// We need more data
    if (stream.error == MAD_ERROR_BUFLEN)
	goto out;

    todo = MP2_ERROR;			// Restart decoder
    if (!MAD_RECOVERABLE(stream.error))
	goto out;

    todo = MP2_LAST;			// Repeat last frame
    mad_stream_skip(&stream, stream.next_frame - stream.this_frame);
out:
    return todo;
}

void cMP2::Start(void)
{
    if (test_flags(MP2SPDIF)) {
	Offset(8);			// We use an IEC60958 PCM head
	return;
    }
    if (running) Stop();
    Offset(0);
    Scale = (test_flags(MP2DITHER)) ? Dither : Round;
    reset_scan();
    mad_stream_init(&stream);
    mad_stream_options(&stream, (MAD_OPTION_IGNORECRC));
    mad_frame_init (&frame);
    mad_header_init(&frame.header);
    mad_synth_init (&synth);
    memset(&dith[0], 0, sizeof(dither_t));
    memset(&dith[1], 0, sizeof(dither_t));
    running = true;
}

void cMP2::Stop (void)
{
    mad_synth_finish (&synth);
    mad_frame_finish (&frame);
    mad_stream_finish(&stream);
    reset_scan();
    running = false;
    if (test_flags(MP2SPDIF))
	Offset(8);			// We use an IEC60958 PCM head
}

const bool cMP2::Initialize(void)
{
    uint_8 * ptr = (uint_8 *)shm_malloc(MAD_BUFFER_MDLEN + MAD_BUFFER_GUARD);
    if (!ptr)				// mad.h says: MAD_BUFFER_MDLEN = 511 + 2048 + MAD_BUFFER_GUARD
	return false;
    ctr.Lock();
    nextin = ptr;
    currin = ptr + MAD_BUFFER_GUARD;
    ctr.Unlock();
    return true;
}

const void cMP2::Release(void)
{
    uint_8 * ptr = nextin;
    if (running) Stop();
    ctr.Lock();
    currin = nextin = (uint_8*)0;
    ctr.Unlock();
    shm_free(ptr);
}

// This function requires two arguments:
//   first is the start of the data segment
//   second is the tail of the data segment
const frame_t & cMP2::Frame(const uint_8 *&out, const uint_8 *const tail)
{
    play.burst = (uint_32 *)0;
    play.size  = 0;

    ctr.Lock();
    if (!currin) goto done;
newsync:
    //
    // Scan the old buffer first which was remembered in
    // the nextin buffer during the last scan at the end.
    //
    if (s.buffer_gard == MAD_BUFFER_GUARD) {
	//
	// Only used if libmad decodes MP2 data frames because
	// all of this frames had to be appended with the next
	// MAD_BUFFER_GUARD bytes of the following MP2 frame.
	//
#if !defined(MAD_BUFFER_GUARD) || (MAD_BUFFER_GUARD < 3)
# error Value of MAD_BUFFER_GUARD seems to be broken
#endif
	const uint_8 * obuf	  = &nextin[0];			// Old buffer
	const uint_8 * const tbuf = obuf + s.buffer_gard;	// Tail of old buffer
	s.buffer_gard = 0;					// No guard bytes anymore

    seek:
	//
	// Find the mp2 audio sync word
	//
	while((s.syncword & magic) != magic) {
	    if (obuf >= tbuf)
		goto resync;
	    s.syncword = (s.syncword << 8) | *obuf++;
	}

	//
	// If we've our magic syncword we should
	// remember it in our current input buffer
	//
	((uint_16*)&currin[0])[0] = ntohs(s.syncword);

	//
	// Need the next 3 bytes to decide how big
	// and which mode the frame is
	//
	while(s.pos < 3) {
	    if (obuf >= tbuf)
		goto resync;
	    currin[s.pos++] = *obuf++;
	}

	//
	// Parse and check syncinfo if not known for this frame
	//
	parse_syncinfo(s.syncinfo, currin);
	if (!s.syncinfo.frame_size) {
	    s.syncword = 0x001f;
	    s.pos = 2;
	    esyslog("MP2PCM: ** Invalid frame found - try to syncing **");
	    if (obuf >= tbuf)
		goto resync;
	    else
		goto seek;
	}
	s.payload_size = s.syncinfo.frame_size;

	//
	// Maybe: Reinit spdif interface if sample rate is changed
	//
	if (s.syncinfo.sampling_rate != sample_rate) {
	    s.syncword = 0x001f;
	    s.pos = 2;
	    s.payload_size = 0;
	    esyslog("MP2PCM: ** Invalid sample rate found - try to syncing **");
	    if (obuf >= tbuf)
		goto resync;
	    else
		goto seek;
	}

	while(s.pos < s.payload_size) {
	    int rest = tbuf - obuf;
	    if (rest <= 0)
		goto resync;
	    if (rest + s.pos > s.payload_size)
		rest = s.payload_size - s.pos;
	    memcpy(&currin[s.pos], obuf, rest);
	    s.pos += rest;
	    obuf  += rest;
	}

    }	// if (s.buffer_gard == MAD_BUFFER_GUARD)

resync:
    //
    // Find the mp2 audio sync word
    //
    while((s.syncword & magic) != magic) {
	if (out >= tail)
	    goto done;
	s.syncword = (s.syncword << 8) | *out++;
    }

    //
    // If we've our magic syncword we should
    // remember it in our current input buffer
    //
    if (s.pos == 2)
	((uint_16*)&currin[0])[0] = ntohs(s.syncword);

    //
    // Need the next 3 bytes to decide how big
    // and which mode the frame is
    //
    while(s.pos < 3) {
	if (out >= tail)
	    goto done;
	currin[s.pos++] = *out++;
    }

    //
    // Parse and check syncinfo if not known for this frame
    //
    if (!s.payload_size) {
	parse_syncinfo(s.syncinfo, currin);
	if (!s.syncinfo.frame_size) {
	    s.syncword = 0x001f;
	    s.pos = 2;
	    esyslog("MP2PCM: ** Invalid frame found - try to syncing **");
	    if (out >= tail)
		goto done;
	    else
		goto resync;
	}
	s.payload_size = s.syncinfo.frame_size;
    }

    //
    // Maybe: Reinit spdif interface if sample rate is changed
    //
    if (s.syncinfo.sampling_rate != sample_rate) {
	s.syncword = 0x001f;
	s.pos = 2;
	s.payload_size = 0;
	esyslog("MP2PCM: ** Invalid sample rate found - try to syncing **");
	if (out >= tail)
	    goto done;
	else
	    goto resync;
    }

    while(s.pos < s.payload_size) {
	int rest = tail - out;
	if (rest <= 0)
	    goto done;
	if (rest + s.pos > s.payload_size)
	    rest = s.payload_size - s.pos;
	memcpy(&currin[s.pos], out, rest);
	s.pos += rest;
	out   += rest;
    }

    if (test_flags(MP2SPDIF)) {
	//
	// If we reach this point, we found a valid MP2 frame to warp into PCM (IEC60958)
	// accordingly to IEC61937
	//
	// What | PCM head              | MP2   payload
	// pos  | 0,1 | 2,3 | 4,5 | 6,7 | 8,9 |
	// size |  16 |  16 |  16 |  16 |  16 |
	// pos  |                       | 0,1 |
	//
	pcm[0] = char2short(0xf8, 0x72);				// Pa
	pcm[1] = char2short(0x4e, 0x1f);				// Pb
	pcm[2] = char2short(0x00, (s.syncinfo.layer == 1) ? 4 : 5);	// Mp2 data
	pcm[3] = shorts((s.syncinfo.frame_size * 8));			// Frame size in bits

	swab(currin, payload, s.payload_size);
	memset(payload + s.payload_size, 0,  s.syncinfo.burst_size - 8 - s.payload_size);

	play.burst = (uint_32 *)(&current[0]);
	play.size  = s.syncinfo.burst_size;
	play.pay   = s.payload_size;

    } else {

	//
	// Libmad decodes MP2 data frames only if the next MAD_BUFFER_GUARD bytes
	// of the following MP2 frame are appended to the input buffer.  Remember
	// this few bytes to be scanned first on next scan.
	//
	while(s.buffer_gard < MAD_BUFFER_GUARD) {
	    int rest = tail - out;
	    if (rest <= 0)
		goto done;
	    if (rest + s.buffer_gard > MAD_BUFFER_GUARD)
		rest = MAD_BUFFER_GUARD - s.buffer_gard;

	    memcpy(&nextin[s.buffer_gard],	 out, rest);	// A dummy head of next buffer
	    memcpy(&currin[s.pos+s.buffer_gard], out, rest);	// Real tail of current buffer
	    s.buffer_gard += rest;
	    out		  += rest;
	}

	if (Stream(currin, s.payload_size + s.buffer_gard) == 0) {
	    s.syncword = 0x001f;				// Error during streaming
	    s.pos = 2;
	    s.sample_size = 0;
	    s.payload_size = 0;
	    if (out >= tail)
		goto done;
	    else
		goto newsync;					// No goto resync due buffer_gard
	}

	int status = MP2_PLAY;
	do {
	    if ((status = Decode()) != MP2_PLAY)
		break;

	    s.sample_size = Sample(&current[0], MP2_BURST_SIZE - s.sample_size);

	    if (BufferGuard())					// Do we cross buffer guard line?
		break;

	} while (status == MP2_PLAY);

	switch (status) {
	case MP2_LAST:
	    s.syncword = 0x001f;
	    s.pos = 2;
	    s.sample_size = 0;
	    s.payload_size = 0;
#ifdef USE_LAST_FRAME
	    dsyslog("MP2PCM: ** mad library failed - repeat last frame **");
	    play.burst = last.burst;
	    play.size  = last.size;
	    play.pay   = last.pay;
	    goto done;
#else
	    dsyslog("MP2PCM: ** mad library failed - try to syncing **");
	    if (out >= tail)
		goto done;
	    else
		goto newsync;					// No goto resync due buffer_gard
#endif
	case MP2_ERROR:
	    s.syncword = 0x001f;
	    s.pos = 2;
	    s.sample_size = 0;
	    s.payload_size = 0;
	    esyslog("MP2PCM: ** Error in mad library - try to restart **");
	    if (out >= tail)
		goto done;
	    else
		goto newsync;					// No goto resync due buffer_gard
	default:
	    break;
	}

	play.burst = (uint_32 *)(&current[0]);
	play.size  = s.sample_size;
	play.pay   = s.sample_size;
    }

    Switch();

    last.burst = (uint_32 *)(&remember[0]);
    last.size  = play.size;
    last.pay   = play.pay;

    s.syncword = 0x001f;
    s.pos = 2;
    s.sample_size = 0;
    s.payload_size = 0;
done:
    ctr.Unlock();
    return play;
}

const bool cMP2::Count(const uint_8 *buf, const uint_8 *const tail)
{
    bool ret = false;

resync:
    if (c.bfound < 3) {
	switch(c.bfound) {
	case 0 ... 1:
	    while((c.syncword & magic) != magic) {
		if (buf >= tail)
		    goto out;
		c.syncword = (c.syncword << 8) | *buf++;
	    }

	    if (c.buffer_gard && (c.buffer_gard == MAD_BUFFER_GUARD))
		c.buffer_gard -= 2;
	    else
		c.buffer_gard = 0;

	    c.sync.ext   = ((c.syncword & 0x0010) >> 4) == 0;
	    c.sync.lsf   = ((c.syncword & 0x0008) >> 3) == 0;
	    c.sync.layer = 4 - ((c.syncword & 0x0006) >> 1);

	    // Sanity check for mpeg audio
	    if ((c.sync.layer == 4) || (!c.sync.lsf && c.sync.ext)) {	// Combination
		c.bfound = 0;
		c.syncword = 0x001f;
		memset(&c.sync, 0, sizeof(mp2info_t));
		goto resync;					// ... not valid
	    }

	    c.bfound = 2;
	case 2:
	    if (buf >= tail)
		goto out;
	    {							// Frame size calculation
		uint_32 bit_rate;
		int index;
		uint_8 padding;

		if (c.buffer_gard && (c.buffer_gard == MAD_BUFFER_GUARD - 2))
		     c.buffer_gard--;
		else
		     c.buffer_gard = 0;

		if ((index = ((*buf) & 0x0c)) == 0x0c) {	// Sample rate index
		    c.bfound = 0;
		    c.syncword = 0x001f;
		    memset(&c.sync, 0, sizeof(mp2info_t));
		    goto resync;				// ... not valid
		}
		index >>= 2;

		c.sync.sampling_rate = samplerate_table[index];
		if (c.sync.lsf) {				// Sample rate
		    c.sync.sampling_rate /= 2;
		    if (c.sync.ext)
			c.sync.sampling_rate /= 2;
		}

		if ((index = ((*buf) & 0xf0)) == 0xf0) {	// Bit rate index
		    c.bfound = 0;
		    c.syncword = 0x001f;
		    memset(&c.sync, 0, sizeof(mp2info_t));
		    goto resync;				// ... not valid
		}
		index >>= 4;

		if (c.sync.lsf)
		    bit_rate = bitrate_table[3 + (c.sync.layer >> 1)][index];
		else
		    bit_rate = bitrate_table[c.sync.layer - 1][index];

		padding = ((*buf) & 0x02) >> 1;

		if (c.sync.layer == 1) {
		    c.fsize = 4*((12 * bit_rate / c.sync.sampling_rate) + padding);
		} else {
		    const uint_16 slots = ((c.sync.layer == 3) && c.sync.lsf) ? 72 : 144;
		    c.fsize = (slots * bit_rate / c.sync.sampling_rate) + padding;
		}
	    }
	    debug_mp2("count_mp2_frame: %d %d\n", c.sync.layer, c.fsize);
	    buf++;
	    c.bfound++;
	default:
	    break;
	}
    }

    while (c.bfound < c.fsize) {
	int skip, pay;
	if ((skip = tail - buf) <= 0)
	    goto out;
	if (skip > (pay = c.fsize - c.bfound))
	    skip = pay;
	c.bfound += skip;
	buf	 += skip;

	if (c.buffer_gard) {				// Decrease also buffer guard
	    if (skip > c.buffer_gard)
		skip = c.buffer_gard;
	    if ((c.buffer_gard -= skip) == 0)		// ... here and maybe we have
		ret = true;				// crossed a frame and the guard
	}
    }

    if (c.fsize && (c.bfound >= c.fsize)) {
	c.bfound = 0;
	c.syncword = 0x001f;
	memset(&c.sync, 0, sizeof(mp2info_t));
	debug_mp2("count_mp2_frame: frame crossed\n");
	if (test_flags(MP2SPDIF))
	    ret = true;
	else
	    c.buffer_gard = MAD_BUFFER_GUARD;		// We have to await the buffer guard
	if (buf >= tail)
	    goto out;
	goto resync;
    }
out:
    return ret;
}
