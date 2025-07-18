/*
 * handle.c:	Implementation of an useful class for byte handling
 *		on static buffer _and_ dynamic byte streams
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
 *
 * Partly based on ftp://ftp.cadsoft.de/vdr/xlist.c
 * Copyright (C) 2001 Klaus Schmidinger, <kls@cadsoft.de>
 *
 * Also used:
 * libmad   source as documentation, http://sourceforge.net/projects/mad/
 * libmpeg2 source as documentation, http://sourceforge.net/projects/libmpeg2
 * also the documentation found at http://mpucoder.kewlhair.com/DVD/
 */

#include "handle.h"
#include <netinet/in.h>

inline bool cHandle::watch(unsigned long int ms)
{
    fd_set watch;
    suseconds_t usec = ms*1000;
    struct timeval tv = { 0, usec};

    FD_ZERO(&watch);
    FD_SET(fd, &watch);

    int ret = 0;
    do {
	errno = 0;
	ret = select(fd+1, &watch, (fd_set*)0, (fd_set*)0, &tv);
    } while (ret < 0 && errno == EINTR);

    if (tv.tv_usec == usec)
	ret = -1;
    return (ret > 0) ? FD_ISSET(fd, &watch) : false;
}

void cHandle::refill(void) throw(cExeption)
{
    size_t free;

    if (fd < 0)		// Static buffer, skip
	goto out;

    if (!(free = (size - avail)))
	goto out;

    do {
	size_t  put = (tail+free > size) ? size - tail : free;
	ssize_t real = read(fd, data+tail, put);
	if (real < 0) {
	    if (errno == EINTR || errno == EAGAIN)
		continue;
	    throw ex;
	}
	if (real == 0) {
	    if (watch(320))
		continue;
	    fd = -1;	// Nothing new, we now have a static buffer
	    break;
	}
	avail += real;
	free  -= real;
	tail   = (tail + real) % size;

    } while (free);
    out:
    return;
};

inline void cHandle::rolling(const size_t pos)
{
    ssize_t roll;
    //
    // Case of rolling the memory window over the end of the ring
    // buffer, just copy the data from the buffer begin to the
    // overroll area to get one continues memory window.
    //
    if ((pos < size) && ((roll = (pos+over) - size) > 0))
	memcpy(data+size, data, roll);
}

void cHandle::movwin(const size_t len)
{
    size_t  want, pos = head;

    if (!len || (over && (len > over)))
	goto out;

    if ((fd >= 0) && (avail < size/2))
	(void)refill();

    if (avail == 0)
	goto out;

    if ((want = len) > avail)
	want = avail;

    pos += want;
    if (over)
	rolling(pos);

    head = pos % size;
    curr = data+head;
    avail -= want;
    offset += want;
    out:
    return;
};

bool cHandle::check(const size_t off) throw(cExeption)
{
    if ((fd >= 0) && (avail < size/2))
	(void)refill();
    left = avail;
    if (over) {
	if (left > over)
	    left = over;
	rolling(head+off);
    }
    if (left < off)
	throw ex;
    return true;
}

cHandle::cHandle(uint_8 *buf, size_t len, const size_t step, const int file)
: data(buf), curr(data), size(len-step), over(step), fd(file), ex(1)
{
    avail = head = tail = 0;
    offset = 0;
    if (fd < 0) {		// For static buffers we assume a filled buffer
	tail  += size;
	avail += size;
    } else
	(void)refill();
};
