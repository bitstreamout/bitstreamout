/*
 * types.h:	Useful types and macros
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
 * Copyright (C) 2002,2003 Werner Fink, <werner@suse.de>
 */

#ifndef __TYPES_H
#define __TYPES_H

#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#if defined(HAS_CDEFS_H)
# include <sys/cdefs.h>
#endif
#ifndef  __always_inline
# define __always_inline inline
#endif
#ifndef __vasm
# define __vasm __asm__ __volatile__
#endif
extern "C" {
#if defined(__i386__) 
    extern __always_inline void clear_bit(int nr, volatile unsigned long * addr)
    {
	__vasm ("btrl %1,%0" :"+m" (*(volatile long *)addr) :"Ir" (nr));
    }
    extern __always_inline void set_bit(int nr, volatile unsigned long * addr)
    {
        __vasm ("btsl %1,%0" :"+m" (*(volatile long *)addr) :"Ir" (nr));
    }
    extern __always_inline int test_bit(int nr, const volatile unsigned long * addr)
    {
	int oldbit;
	__vasm ("btl %2,%1\n\tsbbl %0,%0" :"=r" (oldbit) :"m" (*(volatile long *)addr),"Ir" (nr));
	return oldbit;
    }
    extern __always_inline int test_and_set_bit(int nr, volatile unsigned long * addr)
    {
	int oldbit;
	__vasm ("btsl %2,%1\n\tsbbl %0,%0" :"=r" (oldbit),"+m" (*(volatile long *)addr) :"Ir" (nr) :"memory");
	return oldbit;
    }
    extern __always_inline int test_and_clear_bit(int nr, volatile unsigned long * addr)
    {
	int oldbit;
	__vasm ("btrl %2,%1\n\tsbbl %0,%0" :"=r" (oldbit),"+m" (*(volatile long *)addr) :"Ir" (nr) :"memory");
	return oldbit;
    }
#elif defined(__x86_64__)
    extern __always_inline void clear_bit(int nr, volatile void * addr)
    {
	__vasm ("btrl %1,%0" :"+m" (*(volatile long *)addr) :"dIr" (nr));
    }
    extern __always_inline void set_bit(int nr, volatile void * addr)
    {
        __vasm ("btsl %1,%0" :"+m" (*(volatile long *)addr) :"dIr" (nr));
    }
    extern __always_inline int test_bit(int nr, const volatile void * addr)
    {
	int oldbit;
	__vasm ("btl %2,%1\n\tsbbl %0,%0" :"=r" (oldbit) :"m" (*(volatile long *)addr),"dIr" (nr));
	return oldbit;
    }
    extern __always_inline int test_and_set_bit(int nr, volatile void * addr)
    {
	int oldbit;
	__vasm ("btsl %2,%1\n\tsbbl %0,%0" :"=r" (oldbit),"+m" (*(volatile long *)addr) :"dIr" (nr) :"memory");
	return oldbit;
    }
    extern __always_inline int test_and_clear_bit(int nr, volatile void * addr)
    {
	int oldbit;
	__vasm ("btrl %2,%1\n\tsbbl %0,%0" :"=r" (oldbit),"+m" (*(volatile long *)addr) :"dIr" (nr) :"memory");
	return oldbit;
    }
#else
    extern __always_inline void clear_bit(int nr, volatile void * addr)
    {
	*(volatile long *)addr &= ~(1 << (nr));
    }
    extern __always_inline void set_bit(int nr, volatile void * addr)
    {
        *(volatile long *)addr |= (1 << (nr));
    }
    extern __always_inline int test_bit(int nr, const volatile void * addr)
    {
	return (*(volatile long *)addr) & (1 << (nr));
    }
    extern __always_inline int test_and_set_bit(int nr, volatile void * addr)
    {
	int oldbit = test_bit(nr, addr);
	set_bit(nr, addr);
	return oldbit;
    }
    extern __always_inline int test_and_clear_bit(int nr, volatile void * addr)
    {
	int oldbit = test_bit(nr, addr);
	clear_bit(nr, addr);
	return oldbit;
    }
#endif
};

#ifndef AARONS_TYPES
#define AARONS_TYPES
# if __WORDSIZE == 64
typedef unsigned long int	uint_64;
# else
typedef unsigned long long int	uint_64;
# endif
typedef unsigned int		uint_32;
typedef unsigned short		uint_16;
typedef unsigned char		uint_8;
typedef unsigned long		flags_t;

# if __WORDSIZE == 64
typedef signed long int		sint_64;
# else
typedef signed long long int	sint_64;
# endif
typedef signed int		sint_32;
typedef signed short		sint_16;
typedef signed char		sint_8;
#endif

// Used for little endian data stream (most sound cards)
#ifndef WORDS_BIGENDIAN
# define char2short(a,b)	((((uint_16)(a) << 8) & 0xff00) ^ ((uint_16)(b) & 0x00ff))
# define shorts(a)		(a)
# define char2int(a,b,c,d)	((((uint_32)(a)<<24)&0xff000000)^(((uint_32)(b)<<16)&0x00ff0000)^\
				 (((uint_32)(c)<< 8)&0x0000ff00)^( (uint_32)(d)     &0x000000ff))
# define mshort(a)		(uint_16)(((a)&0xffff0000)>>16)
#else
# define char2short(a,b)	((((uint_16)(b) << 8) & 0xff00) ^ ((uint_16)(a) & 0x00ff))
# define shorts(a)		char2short(((uint_8)(a) & 0xff),((uint_8)((a) >> 8) & 0xff));
# define char2int(a,b,c,d)	((((uint_32)(d)<<24)&0xff000000)^(((uint_32)(c)<<16)&0x00ff0000)^\
				 (((uint_32)(b)<< 8)&0x0000ff00)^( (uint_32)(a)     &0x000000ff))
# define mshort(a)		(uint_16)((a)&0x0000ffff)
#endif

#define SPDIF_BURST_SIZE	6144		// 32ms at 48kHz
#define B2F(a)			((a)>>2)	// Bytes to PCM 16bit stereo samples
#define F2B(a)			((a)<<2)	// PCM 16bit stereo samples to Bytes
#define SPDIF_SAMPLE_FRAMES	B2F(SPDIF_BURST_SIZE)
#define SPDIF_SAMPLE_MAGIC	B2F(8)		// 64 bits are required for IEC60958 magic
#define SPDIF_SAMPLE_ALIGN(x)	(((x)+(SPDIF_SAMPLE_MAGIC-1))&~(SPDIF_SAMPLE_MAGIC-1))

#ifndef DEBUG
# include <linux/rtc.h>
# include <sys/ioctl.h>
# define debug(args...)
# define realtime(name)									\
    if (getuid() == 0) {								\
	const pthread_t self = pthread_self();						\
	/* We increase priority to decrease delays */					\
	struct sched_param param;							\
	int policy = SCHED_RR;								\
	(void)pthread_getschedparam(self, &policy, &param);				\
	param.sched_priority = (sched_get_priority_max(policy)*4)/5;			\
	if (pthread_setschedparam(self, policy, &param) < 0)				\
	    esyslog(name " thread can not set scheduling priority: %s", strerror(errno));\
	/* Free resources immediately on exit */					\
	(void)pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);			\
	(void)pthread_detach(self);							\
	if (nice(-15) < 0 && errno)							\
	    esyslog(name " thread can not set process priority: %s", strerror(errno));	\
    } else {										\
	/* Free resources immediately on exit */					\
	(void)pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);			\
	(void)pthread_detach(pthread_self());						\
    }
#else
# define debug(args...)	fprintf(stderr, args)
# define realtime(name)									\
    /* Free resources immediately on exit */						\
    (void)pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);			\
    (void)pthread_detach(pthread_self());
#endif

#define test_and_set_flag(flag)		test_and_set_bit(FLAG_ ## flag, &flags)
#define test_and_clear_flag(flag)	test_and_clear_bit(FLAG_ ## flag, &flags)
#define test_flag(flag)			test_bit(FLAG_ ## flag, &flags)
#define set_flag(flag)			set_bit(FLAG_ ## flag, &flags)
#define clear_flag(flag)		clear_bit(FLAG_ ## flag, &flags)

#ifndef GCC_VERSION
# if defined(__GNUC__) && defined(__GNUC_MINOR__)
#  define GCC_VERSION	(__GNUC__ * 1000 + __GNUC_MINOR__)
# else
#  define GCC_VERSION	0
# endif
#endif

#if 0 // GCC_VERSION >= 3003
# define local	__thread
#else
# define local
#endif

typedef struct _frame {
    uint_32 *burst;
    unsigned int size;
    unsigned int pay;
} frame_t;

#endif // __TYPES_H
