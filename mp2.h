/*
 * mp2.h:	Use the S/P-DIF interface for redirecting MP2 stream
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
 * Copyright (C) 2003 Sven Goethel, <sven@jausoft.com>
 * Copyright (C) 2003-2005 Werner Fink, <werner@suse.de>
 */

#ifndef __MP2_H
#define __MP2_H

#ifndef HAS_MAD_H
# error The mad.h header file is mised, please install packages mad and mad-devel!
#endif
#include <mad.h>
#include "types.h"
#include "iec60958.h"
#define MP2_BURST_SIZE	192*24

typedef struct _mp2_syncinfo {
    uint_32 sampling_rate;
    int     layer;
    bool    ext;
    bool    lsf;
    uint_16 frame_size;
    uint_16 burst_size;
} mp2info_t;

class cMP2 : public iec60958 {
private:
    static const uint_16 magic;
    struct {
	size_t    pos;
	size_t    buffer_gard;
	size_t    sample_size;
	size_t    payload_size;
	mp2info_t syncinfo;
	uint_16   syncword;
    } s;
    struct {
	uint_16   bfound;
	uint_16   syncword;
	uint_16   fsize;
	uint_16   buffer_gard;
	mp2info_t sync;
    } c;
    static const uint_32 samplerate_table[3];
    static const uint_32 bitrate_table[5][15];
    inline void parse_syncinfo(mp2info_t &syncinfo, const uint_8 *data);
#   define SAMPLE_DEPTH	16
    enum {MIN_SPL = -MAD_F_ONE, MAX_SPL =  MAD_F_ONE - 1};
    // noise shape
    typedef struct dither {
	mad_fixed_t error[3];
	mad_fixed_t random;
    } dither_t;
    typedef struct mad_stream mad_stream_t;
    typedef struct mad_frame  mad_frame_t;
    typedef struct mad_synth  mad_synth_t;
    dither_t     dith[2];
    mad_stream_t stream;
    mad_frame_t  frame;
    mad_synth_t  synth;
    static inline mad_fixed_t Prng(const mad_fixed_t state);
    static sint_32 Dither(mad_fixed_t sample, dither_t &dither);
    static sint_32 Round (mad_fixed_t sample, dither_t &dither);
    typedef sint_32 (*scale_t)(mad_fixed_t sample, dither_t &dither);
    scale_t Scale;
    inline ssize_t Sample(uint_8 *data, size_t cnt);
    // we use double bouffering ..
    uint_8* currin;
    uint_8* nextin;
    cMutex  ctr;
    inline  bool BufferGuard(void);
    size_t  Stream(uint_8 *data, size_t cnt);
#   define MP2_PLAY	0		// We can play
#   define MP2_DATA	1		// We need more data
#   define MP2_LAST	2		// Repeat last frame
#   define MP2_ERROR	-1		// Restart decoder
#   define MP2_STOP	-2		// Stop of the engine
    int     Decode(void);
    void    Start (void);
    void    Stop  (void);
    bool    running;
    inline void reset_scan (void)
    {
	s.pos = 2;
	s.syncword = 0x001f;
	s.buffer_gard = 0;
	s.sample_size = 0;
	s.payload_size = 0;
	memset(&s.syncinfo, 0, sizeof(mp2info_t));
    };
    inline void reset_count(void)
    {
	c.syncword = 0x001f;
	c.bfound = 0;
	c.fsize = 0;
	c.buffer_gard = 0;
	memset(&c.sync, 0, sizeof(mp2info_t));
    };
public:
    cMP2(unsigned int rate);	// Sample rate
    virtual ~cMP2();
    virtual const bool Initialize(void);
    virtual const void Release(void);
    virtual const bool Count(const uint_8 *buf, const uint_8 *const tail);
    virtual const frame_t & Frame(const uint_8 *&out, const uint_8 *const end);
    virtual const void ClassReset(void)
    {
	Stop();
	reset_scan();
	reset_count();
    }
};

extern cMP2 mp2;

#endif // __MP2_H
