/* 
 * bytes.h:     Useful class for byte handling
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

#ifndef __BYTES_H
#define __BYTES_H

#include <sys/types.h>
#include <netinet/in.h>
#include "types.h"

// Exeption class
class cExeption {
public:
    const int reason;
    cExeption(const int Reason) : reason(Reason) {};
};

class cHandle {
private:
    const uint_8 * curr;
    volatile size_t avail;
    volatile size_t offset;
    cExeption ex;
    inline bool check(const size_t off) throw(cExeption)
    {
	if (avail < off) throw ex; return true;
    }
    inline void movwin(const size_t len)
    {
	if (!len)
	    return;
	if (avail < len)
	    throw ex;
	curr   += len;
	avail  -= len;
	offset += len;
    }
public:
    cHandle(const uint_8 *buf, size_t len) : curr(buf), avail(len), offset(0), ex(1) {};
    inline operator uint_8  () { return check(1) ? *curr : 0; };
    inline operator uint_16 () { return check(2) ? ntohs(*((const uint_16*)curr)) : 0; };
    inline operator uint_32 () { return check(4) ? ntohl(*((const uint_32*)curr)) : 0; };
    inline operator bool    () { return check(1); };
    inline bool operator >= (const size_t&  o) { return          (avail >= o); };
    inline bool operator >= (const ssize_t& o) { return ((ssize_t)avail >= o); };
    inline bool operator >  (const size_t&  o) { return          (avail >  o); };
    inline bool operator >  (const ssize_t& o) { return ((ssize_t)avail >  o); };
    inline bool operator <= (const size_t&  o) { return          (avail <= o); };
    inline bool operator <= (const ssize_t& o) { return ((ssize_t)avail <= o); };
    inline bool operator <  (const size_t&  o) { return          (avail <  o); };
    inline bool operator <  (const ssize_t& o) { return ((ssize_t)avail <  o); };
    inline bool operator == (const size_t&  o) { return          (avail == o); };
    inline bool operator == (const ssize_t& o) { return ((ssize_t)avail == o); };
    inline bool operator != (const size_t&  o) { return          (avail != o); };
    inline bool operator != (const ssize_t& o) { return ((ssize_t)avail != o); };
    inline cHandle& operator ++()    { movwin(1); return *this; };
    inline cHandle  operator ++(int) { check(1); const cHandle ret(*this); movwin(1); return ret; };
    inline cHandle& operator +=(const size_t& o) { movwin(o); return *this; };
    inline const uint_8* operator ()(const size_t& o) { return check(o) ? curr : NULL; }
    inline const size_t len(void) { check(0); return avail; };
    inline const size_t Offset(void) const { return offset; };
#   define FOREACH(handle)	try { while ((handle))
#   define TEST(handle)		try { if((handle))
#   define END(handle)		} catch (cExeption& ex) { if (ex.reason != 1) throw; }
};
#endif // __BYTES_H
