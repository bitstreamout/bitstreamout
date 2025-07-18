/*
 * lpcm.c:	Use the S/P-DIF interface for redirecting PCM stream
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
 * Copyright (C) 2003,2004 Werner Fink, <werner@suse.de>
 */

#include "types.h"
#include "lpcm.h"

// --- cPCM : Scanning PCM stream simply for forwarding it -----------------------------

cPCM pcm(48000);

cPCM::cPCM(unsigned int rate)
: iec60958(rate, SPDIF_BURST_SIZE, 0)
{
    s.pos = s.payload_size = 0;
}

// This function requires two arguments:
//   first is the start of the data segment
//   second is the tail of the data segment
const frame_t & cPCM::Frame(const uint_8 *&out, const uint_8 *const tail)
{
    // We overwrite the none audio magic PCM header (offset is zero)
    s.payload_size = SPDIF_BURST_SIZE;

    play.burst = (uint_32 *)0;
    play.size = 0;

    while(s.pos < s.payload_size) {
	int rest = tail - out;
	if (rest <= 0)
	    goto done;
	if (rest + s.pos > s.payload_size)
	    rest = s.payload_size - s.pos;
	memcpy(&current[s.pos], out, rest);
	s.pos += rest;
	out   += rest;
    }
    swab(payload, payload, s.payload_size);

    play.burst = (uint_32 *)(&current[0]);
    play.size  = s.payload_size;
    play.pay   = s.payload_size;

    Switch();

    last.burst = (uint_32 *)(&remember[0]);
    last.size  = play.size;
    last.pay   = play.pay;

    s.pos = 0;
done:
    return play;
}

const bool cPCM::Count(const uint_8 *buf, const uint_8 *const tail)
{
    // One sample has two bytes, two channels for stereo
    // and 192 samples for a vaild S/P-DIF sequence
    return (tail - buf) >= 768;
}
