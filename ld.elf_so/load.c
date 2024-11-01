/*	$NetBSD: load.c,v 1.49 2020/09/21 16:08:57 kamil Exp $	 */

/*
 * Copyright 1996 John D. Polstra.
 * Copyright 1996 Matt Thomas <matt@3am-software.com>
 * Copyright 2002 Charles M. Hannum <root@ihack.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by John Polstra.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Dynamic linker for ELF.
 *
 * John Polstra <jdp@polstra.com>.
 */

#include "include/NetBSD/cdefs.h"
#ifndef lint
__RCSID("$NetBSD: load.c,v 1.49 2020/09/21 16:08:57 kamil Exp $");
#endif /* not lint */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <phoenix/sysinfo.h>

#include "debug.h"
#include "rtld.h"

static bool _rtld_load_by_name(const char *, Obj_Entry *, Needed_Entry **,
    int);

#ifdef RTLD_LOADER
Objlist _rtld_list_main =	/* Objects loaded at program startup */
  SIMPLEQ_HEAD_INITIALIZER(_rtld_list_main);
Objlist _rtld_list_global =	/* Objects dlopened with RTLD_GLOBAL */
  SIMPLEQ_HEAD_INITIALIZER(_rtld_list_global);

void
_rtld_objlist_push_head(Objlist *list, Obj_Entry *obj)
{
	Objlist_Entry *elm;

	elm = NEW(Objlist_Entry);
	elm->obj = obj;
	SIMPLEQ_INSERT_HEAD(list, elm, link);
}

void
_rtld_objlist_push_tail(Objlist *list, Obj_Entry *obj)
{
	Objlist_Entry *elm;

	elm = NEW(Objlist_Entry);
	elm->obj = obj;
	SIMPLEQ_INSERT_TAIL(list, elm, link);
}

Objlist_Entry *
_rtld_objlist_find(Objlist *list, const Obj_Entry *obj)
{
	Objlist_Entry *elm;

	SIMPLEQ_FOREACH(elm, list, link) {
		if (elm->obj == obj)
			return elm;
	}
	return NULL;
}
#endif

/*
 * Load a shared object into memory, if it is not already loaded.
 *
 * Returns a pointer to the Obj_Entry for the object.  Returns NULL
 * on failure.
 */
Obj_Entry *
_rtld_load_object(const char *filepath, int flags)
{
	Obj_Entry *obj;
	int fd = -1;
	struct stat sb;
	size_t pathlen = strlen(filepath);
	const char *syspagename = rtld_syspage_libname(filepath);
	syspageprog_t sysprog;
	int syspagesz;

	for (obj = _rtld_objlist->next; obj != NULL; obj = obj->next)
		if (pathlen == obj->pathlen && !strcmp(obj->path, filepath))
			break;

	/*
	 * If we didn't find a match by pathname, open the file and check
	 * again by device and inode.  This avoids false mismatches caused
	 * by multiple links or ".." in pathnames.
	 *
	 * To avoid a race, we open the file and use fstat() rather than
	 * using stat().
	 */
	if (obj == NULL) {
		if (syspagename == NULL) {
			if ((fd = open(filepath, O_RDONLY)) == -1) {
				_rtld_error("Cannot open \"%s\"", filepath);
				return NULL;
			}
			if (fstat(fd, &sb) == -1) {
				_rtld_error("Cannot fstat \"%s\"", filepath);
				close(fd);
				return NULL;
			}
			for (obj = _rtld_objlist->next; obj != NULL; obj = obj->next) {
				if (obj->ino == sb.st_ino && obj->dev == sb.st_dev) {
					close(fd);
					break;
				}
			}
		} else {
			syspagesz = syspageprog(NULL, -1);
			if (syspagesz < 0) {
				_rtld_error("Cannot get syspage size");
				return NULL;
			}

			for(syspagesz = syspagesz -1; syspagesz >= 0; syspagesz--) {
				if (syspageprog(&sysprog, syspagesz) < 0) {
					_rtld_error("Cannot get syspage prog: %d", syspagesz);
					return NULL;
				}
				if (strcmp(sysprog.name, syspagename) == 0) {
					break;
				}
			}
			if (syspagesz < 0) {
				_rtld_error("Cannot find syspage prog: %s", syspagename);
				return NULL;
			}
		}
	}

#ifdef RTLD_LOADER
	if (pathlen == _rtld_objself.pathlen &&
	    strcmp(_rtld_objself.path, filepath) == 0) {
		close(fd);
		return &_rtld_objself;
	}
#endif

	if (obj == NULL) { /* First use of this object, so we must map it in */
		if (syspagename == NULL) {
			obj = _rtld_map_object(filepath, fd, &sb, NULL);
			(void)close(fd);
		} else {
			obj = _rtld_map_object(filepath, -1, NULL, &sysprog);
		}
		if (obj == NULL)
			return NULL;
		_rtld_digest_dynamic(filepath, obj);

		if (flags & _RTLD_DLOPEN) {
			if (obj->z_noopen || (flags & _RTLD_NOLOAD)) {
				dbg(("refusing to load non-loadable \"%s\"",
				    obj->path));
				_rtld_error("Cannot dlopen non-loadable %s",
				    obj->path);
				rtld_unmap((const struct elf_fdpic_loadmap *)&obj->loadmap);
				_rtld_obj_free(obj);
				return OBJ_ERR;
			}
		}

		*_rtld_objtail = obj;
		_rtld_objtail = &obj->next;
		_rtld_objcount++;
		_rtld_objloads++;
#ifdef RTLD_LOADER
		_rtld_linkmap_add(obj);	/* for the debugger */
#endif
		dbg(("Loadmap: %s", obj->path));
		dbg_rtld_dump_loadmap((const struct elf_fdpic_loadmap *)&obj->loadmap);
		if (obj->textrel)
			dbg(("  WARNING: %s has impure text", obj->path));
	}

	++obj->refcount;
#ifdef RTLD_LOADER
	if (flags & _RTLD_MAIN && !obj->mainref) {
		obj->mainref = 1;
		dbg(("adding %p (%s) to _rtld_list_main", obj, obj->path));
		_rtld_objlist_push_tail(&_rtld_list_main, obj);
	}
	if (flags & _RTLD_GLOBAL && !obj->globalref) {
		obj->globalref = 1;
		dbg(("adding %p (%s) to _rtld_list_global", obj, obj->path));
		_rtld_objlist_push_tail(&_rtld_list_global, obj);
	}
#endif
	return obj;
}

static bool
_rtld_load_by_name(const char *name, Obj_Entry *obj, Needed_Entry **needed,
    int flags)
{
	Obj_Entry *o;

	dbg(("load by name %s", name));
	for (o = _rtld_objlist->next; o != NULL; o = o->next)
		if (_rtld_object_match_name(o, name)) {
			++o->refcount;
			(*needed)->obj = o;
			return true;
		}

	return ((*needed)->obj = _rtld_load_library(name, obj, flags)) != NULL;
}


/*
 * Given a shared object, traverse its list of needed objects, and load
 * each of them.  Returns 0 on success.  Generates an error message and
 * returns -1 on failure.
 */
int
_rtld_load_needed_objects(Obj_Entry *first, int flags)
{
	Obj_Entry *obj;
	int status = 0;

	for (obj = first; obj != NULL; obj = obj->next) {
		Needed_Entry *needed;

		for (needed = obj->needed; needed != NULL;
		    needed = needed->next) {
			const char *name = obj->strtab + needed->name;
#ifdef RTLD_LOADER
			Obj_Entry *nobj;
#endif
			if (!_rtld_load_by_name(name, obj, &needed,
			    flags & ~_RTLD_NOLOAD))
				status = -1;	/* FIXME - cleanup */
#ifdef RTLD_LOADER
			if (status == -1)
				return status;

			if (flags & _RTLD_MAIN)
				continue;

			nobj = needed->obj;
			if (nobj->z_nodelete && !obj->ref_nodel) {
				dbg(("obj %s nodelete", nobj->path));
				_rtld_ref_dag(nobj);
				nobj->ref_nodel = true;
			}
#endif
		}
	}

	return status;
}

#ifdef RTLD_LOADER
int
_rtld_preload(const char *preload_path)
{
	const char *path;
	char *cp, *buf;
	int status = 0;

	if (preload_path != NULL && *preload_path != '\0') {
		cp = buf = xstrdup(preload_path);
		while ((path = strsep(&cp, " :")) != NULL && status == 0) {
			if (!_rtld_load_object(path, _RTLD_MAIN))
				status = -1;
			else
				dbg((" preloaded \"%s\"", path));
		}
		xfree(buf);
	}

	return status;
}
#endif
