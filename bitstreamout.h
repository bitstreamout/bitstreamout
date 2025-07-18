/*
 * bitstreamout.h:	VDR plugin for redirecting 16bit encoded audio streams
 *			(mainly AC3) received from a DVB card or VDR recording 
 *			to S/P-DIF out of a sound card with ALSA (NO decoding!).
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

#ifndef __BITSTREAMOUT_H
#define __BITSTREAMOUT_H
#ifndef SPDIF_TEST
# include <vdr/tools.h>
#endif
#include "types.h"

#define BOUNCE_MEM	KILOBYTE(16*64)
#define TRANSFER_MEM	KILOBYTE(64)
#define SPDIF_MEM	(2*SPDIF_BURST_SIZE)
#define OVERALL_MEM	(BOUNCE_MEM+TRANSFER_MEM+SPDIF_MEM)
#define TRANSFER_START	 BOUNCE_MEM
#define SPDIF_START	(BOUNCE_MEM+TRANSFER_MEM)

typedef struct _opt {
    int card;
    int device;
    int delay;
    int ldelay;
    int mdelay;
    int adelay;
    int mmap;
    int variable;
    int type;
} opt_t;

#define test_and_set_setup(flag)         test_and_set_bit(SETUP_ ## flag, &(setup.flags))
#define test_and_clear_setup(flag)       test_and_clear_bit(SETUP_ ## flag, &(setup.flags))
#define test_setup(flag)                 test_bit(SETUP_ ## flag, &(setup.flags))
#define set_setup(flag)                  set_bit(SETUP_ ## flag, &(setup.flags))
#define clear_setup(flag)                clear_bit(SETUP_ ## flag, &(setup.flags))

typedef struct _ctrl {
    uint_8 *buf;
#define SETUP_AUDIO	0
#define SETUP_RESET	1
#define SETUP_MUTE	2
#define SETUP_ACTIVE	3
#define SETUP_LIVE	4
#define SETUP_MP2ENABLE	5
#define SETUP_MP2DITHER	6
#define SETUP_STILLPIC	7
#define SETUP_CLEAR	8
#define SETUP_MP2SPDIF	9
    volatile flags_t flags;
    opt_t opt;
} ctrl_t;

#endif // __BITSTREAMOUT_H
