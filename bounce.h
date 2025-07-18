/*
 * bounce.h:	Fast ring buffer
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
 * Copyright (C) 2002-2004 Werner Fink, <werner@suse.de>
 */

#ifndef __BOUNCE_H
#define __BOUNCE_H

#include <string.h>
#include <sys/time.h>
#include <vdr/config.h>
#include <vdr/thread.h>
#include "types.h"

class cIoMutex {
private:
    volatile int locked;
    pthread_t thread;
    pthread_mutex_t mutex;
public:
    cIoMutex(void) : locked(0), thread(0)
    {
	pthread_mutex_init(&mutex, NULL);
    }
    ~cIoMutex(void)
    {
	pthread_mutex_destroy(&mutex);
    }
    inline void Lock(void)
    {
	if (thread != pthread_self() || !locked) {
            (void)pthread_mutex_lock(&mutex);
	    thread = pthread_self();
	}
	locked++;
    }
    inline void Unlock(void)
    {
	if (!--locked) {
	    thread = 0;
	    (void)pthread_mutex_unlock(&mutex);
	}
    }
};

class cIoWatch {
private:
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    volatile bool bounce;
    volatile bool sleeps;
public:
    cIoWatch() : bounce(false), sleeps(false)
    {
	pthread_mutex_init(&mutex, NULL);
	pthread_cond_init(&cond, NULL);
    };
    ~cIoWatch(void)
    {
	pthread_cond_broadcast(&cond);
	pthread_cond_destroy(&cond);
	pthread_mutex_destroy(&mutex);
    };
    inline void Signal(bool yield = false)
    {
	pthread_mutex_lock(&mutex);
	bounce = true;
	if (sleeps) pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mutex);
	if (yield) pthread_yield();
    }
    inline bool Wait(long msec)
    {
	struct timeval now;
	bool ret;
	pthread_mutex_lock(&mutex);
	ret = true;
	if (bounce)
	    goto out;
	sleeps = true;
	ret = false;
	if (gettimeofday(&now, NULL) == 0) {
	    int status;
	    struct timespec res;

	    now.tv_usec += msec * 1000;
	    while (now.tv_usec >= 1000000) {
		now.tv_sec++;
		now.tv_usec -= 1000000;
	    }
	    res.tv_sec  = now.tv_sec;
	    res.tv_nsec = now.tv_usec * 1000;

	    ret = true;
	    do {
		status = pthread_cond_timedwait(&cond, &mutex, &res);
	    } while (status == EINTR);

	    if (status != 0)
		ret = false; 
	}
    out:
	sleeps = bounce = false;
	pthread_mutex_unlock(&mutex);
	return ret;
    }
    inline void Init (void) const {}
    inline void Clear(void) const {}
};

class cBounce {
private:
    uint_8 *const data;
    const size_t size;
    volatile size_t head;
    volatile size_t tail;
    volatile size_t avail;
    volatile size_t threshold;
    cIoMutex mutex;
    cIoWatch iowatch;
    inline void reset(void) { avail = head = tail = 0; };
    inline const size_t stored(void) const { return avail; }
public:
    cBounce(uint_8 *buf, size_t len)
    : data(buf), size(len), head(0), tail(0), avail(0),
      threshold(0), mutex(), iowatch() {}
    inline bool store(const uint_8 *buf, const size_t len, const bool wakeup = true)
    {
	bool ret = false;
	size_t free;
	if (!len)
	    goto out;
	mutex.Lock();
	if (len > (free = (size - avail))) {
	    dsyslog("BOUNCE BUFFER: Bufferoverlow\n");
	    goto unl;
	}

	ret = (len < free);
	if (free > len)
	    free = len;

	if (tail+free > size) {         // Case of rolling over in ring buffer
	    size_t roll = size - tail;
	    memcpy(data+tail, buf, roll);
	    buf   += roll;
	    avail += roll;
	    free  -= roll;
	    tail = 0;
	}
	memcpy(data+tail, buf, free);   // Append data
	avail += free;
	tail   = (tail + free) % size;
    unl:
	mutex.Unlock();
	if (wakeup)
	    iowatch.Signal(!ret);
    out:
	return ret;
    };
    inline ssize_t fetch(uint_8 *buf, const size_t len)
    {
	const uint_8 *const start = buf;
	size_t want;
	if (!len)
	   goto out;

	mutex.Lock();
	if (!avail)
	   goto unl;

	want = len;
	if (want > avail)
	    want = avail;

	if (head+want > size) {         // Case of rolling over in ring buffer
	    size_t roll = size - head;
	    memcpy(buf, data+head, roll);
	    buf   += roll;
	    avail -= roll;
	    want  -= roll;
	    head = 0;
	}
	memcpy(buf, data+head, want);   // Read from beginning
	buf   += want;
	avail -= want;
	head   = (head + want) % size;
    unl:
	mutex.Unlock();
    out:
	return (buf - start);
    };
    inline const void flush(void)	{ mutex.Lock(); reset(); mutex.Unlock(); };
    inline const void bank(size_t val)	{ threshold = val; };
    inline const void signal(void)	{ iowatch.Signal(true); };
    inline const bool poll(int msec)
    {
	bool ret = true;
	if (stored() <= threshold)
	    ret = iowatch.Wait(msec);
	return ret;
    }
    inline const void	takeio (void)	{ iowatch.Init();  };
    inline const void	leaveio(void)	{ iowatch.Clear(); };
    inline const size_t getfree(void)	{ return (size - stored()); }
    inline const bool	free(const size_t min)
					{ return (getfree() > min); }
    inline const size_t getused(void)	{ return stored(); }
};
#endif // __BOUNCE_H
