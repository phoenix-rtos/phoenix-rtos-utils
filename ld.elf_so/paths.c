/*	$NetBSD: paths.c,v 1.42 2016/01/24 01:56:04 christos Exp $	 */

/*
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
 * 3. The name of the author may not be used to endorse or promote products
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

#include "include/NetBSD/cdefs.h"
#ifndef lint
__RCSID("$NetBSD: paths.c,v 1.42 2016/01/24 01:56:04 christos Exp $");
#endif /* not lint */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "debug.h"
#include "rtld.h"

static Search_Path *_rtld_find_path(Search_Path *, const char *, size_t);
static Search_Path **_rtld_append_path(Search_Path **, Search_Path **,
    const char *, const char *, const char *);
static char *exstrdup(const char *, const char *);
static const char *getstr(const char **, const char *, const char *);
static const char *getcstr(const char **, const char *, const char *);

static const char WS[] = " \t\n";

/*
 * Like xstrdup(), but takes end of string as a argument.
 */
static char *
exstrdup(const char *bp, const char *ep)
{
	char *cp;
	size_t len = ep - bp;

	cp = xmalloc(len + 1);
	memcpy(cp, bp, len);
	cp[len] = '\0';
	return (cp);
}

/*
 * Like strsep(), but takes end of string and doesn't put any NUL.  To
 * detect empty string, compare `*p' and return value.
 */
static const char *
getstr(const char **p, const char *ep, const char *delim)
{
	const char *cp = *p, *q, *r;

	if (ep < cp)
		/* End of string */
		return (NULL);

	for (q = cp; q < ep; q++)
		for (r = delim; *r != 0; r++)
			if (*r == *q)
				goto done;

done:
	*p = q;
	return (cp);
}

/*
 * Like getstr() above, but delim[] is complemented.
 */
static const char *
getcstr(const char **p, const char *ep, const char *delim)
{
	const char *cp = *p, *q, *r;

	if (ep < cp)
		/* End of string */
		return (NULL);

	for (q = cp; q < ep; q++)
		for (r = delim; *r != *q; r++)
			if (*r == 0)
				goto done;

done:
	*p = q;
	return (cp);
}

static Search_Path *
_rtld_find_path(Search_Path *path, const char *pathstr, size_t pathlen)
{

	for (; path != NULL; path = path->sp_next) {
		if (pathlen == path->sp_pathlen &&
		    memcmp(path->sp_path, pathstr, pathlen) == 0)
			return path;
	}
	return NULL;
}

static Search_Path **
_rtld_append_path(Search_Path **head_p, Search_Path **path_p,
    const char *execname, const char *bp, const char *ep)
{
	Search_Path *path;
	char epath[MAXPATHLEN];
	size_t len;

	len = _rtld_expand_path(epath, sizeof(epath), execname, bp, ep);
	if (len == 0)
		return path_p;

	if (_rtld_find_path(*head_p, bp, ep - bp) != NULL)
		return path_p;

	path = NEW(Search_Path);
	path->sp_pathlen = len;
	path->sp_path = exstrdup(epath, epath + len);
	path->sp_next = (*path_p);
	(*path_p) = path;
	path_p = &path->sp_next;

	dbg((" added path \"%s\"", path->sp_path));
	return path_p;
}

void
_rtld_add_paths(const char *execname, Search_Path **path_p, const char *pathstr)
{
	Search_Path **head_p = path_p;

	if (pathstr == NULL)
		return;

	if (pathstr[0] == ':') {
		/*
		 * Leading colon means append to current path
		 */
		while ((*path_p) != NULL)
			path_p = &(*path_p)->sp_next;
		pathstr++;
	}

	for (;;) {
		const char *bp = pathstr;
		const char *ep = strchr(bp, ':');
		if (ep == NULL)
			ep = &pathstr[strlen(pathstr)];

		path_p = _rtld_append_path(head_p, path_p, execname, bp, ep);

		if (ep[0] == '\0')
			break;
		pathstr = ep + 1;
	}
}

void
_rtld_process_hints(const char *execname, Search_Path **path_p, const char *fname)
{
	int fd;
	char *buf, small[128];
	const char *b, *ep, *ptr;
	struct stat st;
	ssize_t sz;
	Search_Path **head_p = path_p;

	if ((fd = open(fname, O_RDONLY)) == -1) {
		/* Don't complain */
		return;
	}

	/* Try to avoid mmap/stat on the file. */
	buf = small;
	buf[0] = '\0';
	sz = read(fd, buf, sizeof(small));
	if (sz == -1) {
		xwarn("read: %s", fname);
		(void)close(fd);
		return;
	}
	if (sz >= (ssize_t)sizeof(small)) {
		if (fstat(fd, &st) == -1) {
			/* Complain */
			xwarn("fstat: %s", fname);
			(void)close(fd);
			return;
		}

		sz = (ssize_t) st.st_size;

		buf = mmap(0, sz, PROT_READ, MAP_SHARED, fd, 0);
		if (buf == MAP_FAILED) {
			xwarn("mmap: %s", fname);
			(void)close(fd);
			return;
		}
	}
	(void)close(fd);

	while ((*path_p) != NULL)
		path_p = &(*path_p)->sp_next;

	for (b = buf, ep = buf + sz; b < ep; b++) {
		(void)getcstr(&b, ep, WS);
		if (b == ep)
			break;

		ptr = getstr(&b, ep, "\n#");
		if (*ptr == '/') {
			/*
			 * Since '/' != '\n' and != '#', we know ptr <
			 * b.  And we will stop when b[-1] == '/'.
			 */
			while (b[-1] == ' ' || b[-1] == '\t')
				b--;
			path_p = _rtld_append_path(head_p, path_p, execname,
			    ptr, b);
		}

		/*
		 * b points one of ' ', \t, \n, # or equal to ep.  So,
		 * make sure we are at newline or end of string.
		 */
		(void)getstr(&b, ep, "\n");
	}

	if (buf != small)
		(void)munmap(buf, sz);
}
