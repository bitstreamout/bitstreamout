/*
 * shm_memory_tool.c:   Define the alloc interface for shared memory
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

#include <sys/utsname.h>
#include <sched.h>
#include "types.h"
#include "shm_memory_tool.h"

static cList<PointerShmFdListObject> SHMMemoryMappings;

static unsigned int shm_number = 0;

static int kernel_version(int major, int minor, int release)
{
    return (((major) << 16) + ((minor) << 8) + (release));
}

static int kernel_version(void)
{
    static int major, minor, release;
    if (!major) {
	struct utsname uts;
	char *endptr;
	uname(&uts);
	major   = strtol(uts.release,	&endptr, 10);
	minor   = strtol(endptr+1,	&endptr, 10);
	release = strtol(endptr+1, (char**)NULL, 10);
    }
    return (((major) << 16) + ((minor) << 8) + (release));
}

void * shm_malloc(size_t size)
{
    return shm_malloc(size, MAP_PRIVATE|MAP_NORESERVE|MAP_LOCKED);
}

void * shm_malloc(size_t size, int flags)
{
    int    shmfd = -1;
    void * ptr   = NULL;
    char name[255];
    PointerShmFdListObject * p_item = NULL;

    snprintf(name, 255, "/vdr_memory_%8.8u", shm_number++);

    if ((shmfd = shm_open(name, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR)) < 0) {
	esyslog("shm_malloc: shm_open of %s failed: %s\n", name, strerror(errno));
	goto err;
    }

    if(ftruncate(shmfd, size) < 0) {
#if defined __GNUC__ && __GNUC__ > 2
	esyslog("shm_malloc: ftruncate of %s to size %zd failed: %s\n",
		 name, size, strerror(errno));
#else
	esyslog("shm_malloc: ftruncate of %s to size %d failed: %s\n",
		 name, size, strerror(errno));
#endif
	goto err;
    }

    if (getuid() != 0) {
	dsyslog("shm_malloc: memory area will not locked\n");
	flags &= ~MAP_LOCKED;
    }

    if ((ptr = mmap(NULL, size, PROT_READ|PROT_WRITE, flags, shmfd, 0)) == MAP_FAILED) {
#if defined __GNUC__ && __GNUC__ > 2
	esyslog("shm_malloc: mmap of %s to size %zd failed: %s\n",
		 name, size, strerror(errno));
#else
	esyslog("shm_malloc: mmap of %s to size %d failed: %s\n",
		 name, size, strerror(errno));
#endif
	goto err;
    }

    if ((flags & MAP_LOCKED) && (kernel_version() < kernel_version(2,5,37))) {
	if (mlock(ptr, size) != 0) {
#if defined __GNUC__ && __GNUC__ > 2
	    esyslog("shm_malloc: mlock of %s to size %zd failed: %s\n",
		     name, size, strerror(errno));
#else
	    esyslog("shm_malloc: mlock of %s to size %d failed: %s\n",
		     name, size, strerror(errno));
#endif
	    goto err;
	}
    }

    if ((p_item = new PointerShmFdListObject(shmfd, name, ptr, size)) == NULL) {
	esyslog("shm_malloc: list create failed: %s\n", strerror(errno));
	goto err;
    }

    SHMMemoryMappings.Add(p_item);
    SHMMemoryMappings.Sort();

    debug("shm_malloc pointer %p alloced\n", ptr);
    return ptr;

err:
    if (ptr) {
	if ((flags & MAP_LOCKED) && (kernel_version() < kernel_version(2,5,37)))
	    (void) munlock(ptr, size);
	(void) munmap (ptr, size);
    }
    ptr=NULL;
    if (shmfd >= 0) {
	if (name)
	    shm_unlink(name);
	close(shmfd);
    }
    shmfd = -1;
    return NULL;
}

void shm_free(void * memptr)
{
    PointerShmFdListObject * p_item = shm_find(memptr);
    void * ptr = NULL;

    if (p_item == NULL) {
	dsyslog("shm_free: pointer %p not found in alloc space\n", memptr);
    	return;
    }

    if ((ptr = p_item->getPtr())) {
	const size_t sz = p_item->getSize();
	if ((getuid() == 0) && (kernel_version() < kernel_version(2,5,37)))
	    (void) munlock(ptr, sz);
	(void) munmap (ptr, sz);
    }
    if (p_item->getFd() >= 0) {
	const char * name = p_item->getFileName();
	if (name)
	    shm_unlink(name);
	close(p_item->getFd());
    }

    SHMMemoryMappings.Del(p_item);
}

PointerShmFdListObject * shm_find(void * memptr)
{
    PointerShmFdListObject * p_item = NULL;
    int i;

    for (i = (SHMMemoryMappings.Count() - 1); i >= 0; i-- ) {
	p_item = dynamic_cast<PointerShmFdListObject *>(SHMMemoryMappings.Get(i));
	if (p_item->getPtr() == memptr)
	    break;
    }

    return p_item;
}
