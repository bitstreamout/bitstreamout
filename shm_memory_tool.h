/*
 * shm_memory_tool.h:	Define the alloc interface for shared memory
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
 */

#ifndef SHM_MEMORY_TOOL_H
#define SHM_MEMORY_TOOL_H

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <vdr/tools.h>

class PointerShmFdListObject : public cListObject {
private:
    const int shmfd;
    char * name;
    void * ptr;
    const size_t sz;
    typedef const PointerShmFdListObject * const	pShmObj;
public:
    PointerShmFdListObject(const PointerShmFdListObject &v)
    : cListObject(), shmfd(v.shmfd), name(NULL), ptr(v.ptr), sz(v.sz)
    { name  = strdup(v.name); }

    PointerShmFdListObject(int fd, const char * fname, void * memptr, const size_t size)
    : cListObject(), shmfd(fd), name(NULL), ptr(memptr), sz(size)
    { name  = strdup(fname); }

    ~PointerShmFdListObject() { if (name) free(name); }

    void *       getPtr ()     const { return ptr; }
    const size_t getSize()     const { return sz; }
    const int    getFd  ()     const { return shmfd; }
    const char * getFileName() const { return name; }

    virtual bool operator == (const cListObject & ListObject ) const
    {
	pShmObj tmp = dynamic_cast<pShmObj>(&ListObject);
	return tmp && (ptr == tmp->ptr) && (shmfd == tmp->shmfd);
    }

    virtual bool operator  < (const cListObject & ListObject)
    {
	pShmObj tmp = dynamic_cast<pShmObj>(&ListObject);
	return tmp && (shmfd < tmp->shmfd);
    }

    virtual bool operator  > (const cListObject & ListObject) const
    {
	pShmObj tmp = dynamic_cast<pShmObj>(&ListObject);
	return tmp && (shmfd > tmp->shmfd);
    }

    operator int          () const { return name ? shmfd : -1; }
    operator const void * () const { return name ? ptr : NULL; }
};

extern void * shm_malloc (size_t size);
extern void * shm_malloc (size_t size, int flags);
extern void   shm_free   (void * memptr);
extern PointerShmFdListObject * shm_find (void * memptr);

#endif
