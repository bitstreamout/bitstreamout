/*
 * lpcm.h:	Use the S/P-DIF interface for redirecting PCM stream
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

#ifndef __LPCM_H
#define __LPCM_H

#include "iec60958.h"

class cPCM : public iec60958 {
private:
    struct {
	size_t pos;
	size_t payload_size;
    } s;
public:
    cPCM(unsigned int rate);	// Sample rate
    virtual ~cPCM() { s.pos = s.payload_size = 0; };
    virtual const bool Count(const uint_8 *buf, const uint_8 *const tail);
    virtual const frame_t & Frame(const uint_8 *&out, const uint_8 *const end);
    virtual const void ClassReset(void) { s.pos = s.payload_size = 0; };
};

extern cPCM pcm;

#endif // __LPCM_H
