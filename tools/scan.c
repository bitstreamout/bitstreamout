/*
 * scan.c:	Scan for various frames within static buffers
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
 * Copyright (C) 2003 Werner Fink, <werner@suse.de>
 */

#include "handle.h"

#include <stdio.h>
static FILE* out = stdout;
#define D(format, args...)              fprintf(out, format "\n", ## args)
#define N(format, args...)              fprintf(out, format,      ## args)
#define nl                              putc('\n', out);
#define dsyslog(format, args...)	fprintf(stderr, "cutter: " format "\n", ## args)
#define esyslog(format, args...)	fprintf(stderr, "cutter: " format "\n", ## args)


typedef struct struct_scan {
    uint_8* buffer;
    uint_32 length;
    uint_32 missed;
    bool aligned;
} scan_t;

scan_t scan_ac3(scan_t in, const off_t rest)
{
    cHandle handle(in.buffer, rest);
    typedef struct _frmsize
    {
	uint_16 bit_rate;
	uint_16 frame_size[3];
    } frmsize_t;
    static const frmsize_t frmsizecod_tbl[64] = {
	{ 32  ,{64   ,69   ,96   } }, { 32  ,{64   ,70   ,96   } },
	{ 40  ,{80   ,87   ,120  } }, { 40  ,{80   ,88   ,120  } },
	{ 48  ,{96   ,104  ,144  } }, { 48  ,{96   ,105  ,144  } },
	{ 56  ,{112  ,121  ,168  } }, { 56  ,{112  ,122  ,168  } },
	{ 64  ,{128  ,139  ,192  } }, { 64  ,{128  ,140  ,192  } },
	{ 80  ,{160  ,174  ,240  } }, { 80  ,{160  ,175  ,240  } },
	{ 96  ,{192  ,208  ,288  } }, { 96  ,{192  ,209  ,288  } },
	{ 112 ,{224  ,243  ,336  } }, { 112 ,{224  ,244  ,336  } },
	{ 128 ,{256  ,278  ,384  } }, { 128 ,{256  ,279  ,384  } },
	{ 160 ,{320  ,348  ,480  } }, { 160 ,{320  ,349  ,480  } },
	{ 192 ,{384  ,417  ,576  } }, { 192 ,{384  ,418  ,576  } },
	{ 224 ,{448  ,487  ,672  } }, { 224 ,{448  ,488  ,672  } },
	{ 256 ,{512  ,557  ,768  } }, { 256 ,{512  ,558  ,768  } },
	{ 320 ,{640  ,696  ,960  } }, { 320 ,{640  ,697  ,960  } },
	{ 384 ,{768  ,835  ,1152 } }, { 384 ,{768  ,836  ,1152 } },
	{ 448 ,{896  ,975  ,1344 } }, { 448 ,{896  ,976  ,1344 } },
	{ 512 ,{1024 ,1114 ,1536 } }, { 512 ,{1024 ,1115 ,1536 } },
	{ 576 ,{1152 ,1253 ,1728 } }, { 576 ,{1152 ,1254 ,1728 } },
	{ 640 ,{1280 ,1393 ,1920 } }, { 640 ,{1280 ,1394 ,1920 } }
    };
    scan_t ret =  {NULL, 0, 0, false};

#if 1
    if (in.aligned && in.missed > 0) {	// Something wrong
	dsyslog("Scan: Missed part of AC3 frame");
	in.missed = 0;
    }

    test(handle >= 4) {
	uint_32 l = rest;
	if (in.missed > 0) {
	    if (l < in.missed) {
		esyslog("Scan: Overlapping AC3 frame larger than buffer size");
		goto out;
	    }
	    ret.buffer = handle(in.missed);
	    ret.length = in.missed;
	    ret.missed = 0;
	    ret.aligned = true;
	    goto out;
	}
	uint_16 m = handle;
	uint_8 *p = handle(l);
	if (m == 0x0b77) {
	}
    } end(handle)
out:
    return ret;

#else

    test(handle >= 4) {
	static sint_32 len = 0;
	sint_32 l = rest;
	if (in.aligned || len) {
	    if (in.aligned)
		len = 0;		// On boundary normally AC3 starts
	    if (len) {
		nl;			// If not on boundary a overroll may occured
		N("%8llu   ", deep+handle.Offset());
		N(" plain(AC3) rest=%d", len);
		uint_8 *p = handle(len);
//		Out.Digital->Add(0x80, p, len);
		handle += len;
		l -= len;
	    }
	    while (l > 0) {
		uint_16 m = handle;
		uint_8 *p = handle(l);
		if (m == 0x0b77) {
		    uint_16 fscod, frmsizecod;
		    handle += 4;

		    fscod	= (((uint_8)handle)>>6)&3;
		    frmsizecod	=  ((uint_8)handle)&0x3f;
		    len = 2*frmsizecod_tbl[frmsizecod].frame_size[fscod];

		    nl;
		    N("%8llu 0B", deep+handle.Offset()-4);
		    N(" plain(AC3) size=%d", len);

		    if (!cpts) {
//			cpts = Out.Digital->PTS(0x80);
			cpts += 32*90UL;
		    }

		    if (l < len) {
			N(" included=%u", l);
			len -= l;
//			Out.Digital->Push(0x80, p, l, cpts, flags);
			break;
		    }
		    l -= len;
		    handle += (len - 4);
//		    Out.Digital->Push(0x80, p, len, cpts, flags);
		    cpts += 32*90UL;
		} else {
		    len = 0;
		    break;
		}
	    }
	}
    } end (handle);

    return ret;
#endif
}

#if 0
while ((scan = scan_ac3()).buf)
    ;
#endif
