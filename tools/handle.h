/*
 * handle.h:	Declaration of an useful class for byte handling
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

#ifndef __HANDLE_H
#define __HANDLE_H

#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>

#ifndef AARONS_TYPES
#define AARONS_TYPES
typedef unsigned long long uint_64;
typedef unsigned int   uint_32;
typedef unsigned short uint_16;
typedef unsigned char  uint_8;

typedef signed long long sint_64;
typedef signed int     sint_32;
typedef signed short   sint_16;
typedef signed char    sint_8;
#endif

// Exeption class
class cExeption {
public:
    const int reason;
    cExeption(const int Reason) : reason(Reason) {};
};

class cHandle {
private:
    uint_64 offset;
    uint_8 *const data;
    uint_8 * curr;
    const size_t size;
    const size_t over;
    volatile size_t left;
    volatile size_t head;
    volatile size_t tail;
    volatile size_t avail;
    int fd;
    cExeption ex;
    inline bool watch(unsigned long int ms);
    void refill(void) throw(cExeption);
    inline void rolling(const size_t pos);
    void movwin(const size_t len);
    bool check(const size_t off) throw(cExeption);
public:
	//
	// Dynamic buffers filled from file opened with descriptor `file'
	// or static buffer aready filled.  In case of a dynamic filter
	// you may provide a minimal memory window step size to be able
	// to ensure continues buffers from operators even if a buffer
	// boundary is reached.
	//
    cHandle(uint_8 *buf, size_t len, const size_t step = 0, const int file = -1);
	//
	// Return current offset
	//
    virtual const uint_64 Offset() const { return offset; };
	//
	// Return unsigned character value of
	// current buffer position
	//
    inline operator uint_8  () { return check(1) ? *curr : 0; };
	//
	// Return unsigned short value of current
	// buffer position (in big endian order)
	//
    inline operator uint_16 () { return check(2) ? ntohs(*((uint_16*)curr)) : 0; };
	//
	// Return unsigned int value of current
	// buffer position (in big endian order)
	//
    inline operator uint_32 () { return check(4) ? ntohl(*((uint_32*)curr)) : 0; };
	//
	// Returns true if buffer has at least one
	// left member
	//
    inline operator bool    () { return check(1); };
	//
	// A few useful relational operators
	//
    inline bool operator >= (const size_t  &o) { return          (avail >= o); };
    inline bool operator >= (const uint_32 &o) { return          (avail >= (const size_t) o); };
    inline bool operator >= (const int     &o) { return ((ssize_t)avail >= (const ssize_t)o); };
    inline bool operator >  (const size_t  &o) { return          (avail >  o); };
    inline bool operator >  (const uint_32 &o) { return          (avail >  (const size_t) o); };
    inline bool operator >  (const int     &o) { return ((ssize_t)avail >  (const ssize_t)o); };
    inline bool operator <= (const size_t  &o) { return          (avail <= o); };
    inline bool operator <= (const uint_32 &o) { return          (avail <= (const size_t) o); };
    inline bool operator <= (const int     &o) { return ((ssize_t)avail <= (const ssize_t)o); };
    inline bool operator <  (const size_t  &o) { return          (avail <  o); };
    inline bool operator <  (const uint_32 &o) { return          (avail <  (const size_t) o); };
    inline bool operator <  (const int     &o) { return ((ssize_t)avail <  (const ssize_t)o); };
    inline bool operator == (const size_t  &o) { return          (avail == o); };
    inline bool operator == (const uint_32 &o) { return          (avail == (const size_t) o); };
    inline bool operator == (const int     &o) { return ((ssize_t)avail == (const ssize_t)o); };
    inline bool operator != (const size_t  &o) { return          (avail != o); };
    inline bool operator != (const uint_32 &o) { return          (avail != (const size_t) o); };
    inline bool operator != (const int     &o) { return ((ssize_t)avail != (const ssize_t)o); };
	//
	// Skip current read position, goto next (++x).
	//
    inline cHandle& operator ++()      { movwin(1); return *this; };
	//
	// Skip current read position, goto next.
	// Return previous (x++).
	//
    inline cHandle  operator ++(int)
    {
	check(1);		// Before moving position, check and maybe rolling over
	const cHandle ret(*this); movwin(1); return ret;
    };
	//
	// Skip o positions from current read
	// position (x += o).
	//
    inline cHandle& operator +=(const size_t& o) { movwin(o); return *this; };
	//
	// Return current buffer position with at least
	// o members included. Do not use any position
	// operator during usage of the pointer to this
	// buffer position
	//
    inline uint_8*  operator ()(const size_t& o) { return check(o) ? curr : NULL; }
	//
	// Return left members of buffer. Should be less or
	// equal to step size if step size is larger than zero.
	//
    inline const size_t len(void) { check(0); return left; };
	//
	// Exeption handling: you should use this instead
	// of `while (handle)' or `if (handle)' ... this
	// avoids checks for potential buffer shortage.
	//
#   define foreach(handle)	try { while ((handle))
#   define test(handle)		try { if((handle))
#   define end(handle)		} catch (cExeption& ex) { if (ex.reason != 1) throw; }

};

#endif // __HANDLE_H
