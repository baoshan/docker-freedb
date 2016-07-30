/*
 * Copyright (C) 2001  Edmund Grimley Evans <edmundo@rano.org>
 * Portions Copyright (C) 2003  by various authors
 *
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *
 *  $Id: charset.c,v 1.11 2004/01/10 14:59:24 joerg78 Exp $
 */

#include <string.h>
#include <stdlib.h>
#include "cddbd.h"

#include "utf8.h"

void *xrealloc(void *ptr, size_t size)
{
	void *p = realloc(ptr, size);
	if (!p && size) {
		cddbd_log(LOG_ERR, "Realloc failed.");
		quit(QUIT_ERR);
	}
	return p;
}

/*
 * If there is a valid UTF-8 char starting at *ps,
 * return its value and increment *ps. Otherwise
 * return -1 and leave *ps unchanged. All 2^31
 * possible UCS chars are accepted.
 */
int parse_utf8(const char **ps)
{
	unsigned char *s = (unsigned char *)*ps;
	int wc, n, i;

	if (*s < 0x80) {
		*ps = (char *)s + 1;
		return *s;
	}

	if (*s < 0xc2)
		return -1;

	else if (*s < 0xe0) {
		if ((s[1] & 0xc0) != 0x80)
			return -1;
		*ps = (char *)s + 2;
		return ((s[0] & 0x1f) << 6) | (s[1] & 0x3f);
	}

	else if (*s < 0xf0)
		n = 3;

	else if (*s < 0xf8)
		n = 4;

	else if (*s < 0xfc)
		n = 5;

	else if (*s < 0xfe)
		n = 6;

	else
		return -1;

	wc = *s++ & ((1 << (7 - n)) - 1);
	for (i = 1; i < n; i++) {
		if ((*s & 0xc0) != 0x80)
			return -1;
		wc = (wc << 6) | (*s++ & 0x3f);
	}
	if (wc < (1 << (5 * n - 4)))
		return -1;
	*ps = (char *)s;
	return wc;
}

void charset_latin1_utf8(const char *s, char **ps)
{
	char *buf, *p;

	p = buf = xrealloc(0, strlen(s) * 2 + 1);
	for (; *s; s++)
		if ((*s & 0x80) == 0)
			*p++ = *s;
		else {
			*p++ = 0xc0 | ((*s >> 6) & 0x03);
			*p++ = 0x80 | (*s & 0x3f);
		}
	*p++ = '\0';
	*ps = xrealloc(buf, p - buf);
}
				
int charset_utf8_latin1(const char *s, char **ps)
{
	const char *t = s;
	int inexact = 0;
	char *p, *buf;
	int n;

	p = buf = xrealloc(0, strlen(s) / 2 * MAXTRANS + 2);
	while ((n = parse_utf8(&t)))
		if (n == -1)
			return -1;
		else if (n < 0x100)
			*p++ = n;
		else {
			int i, j;

			inexact = 1;
			i = (n >> 8) - 1;
			j = n & 0xff;
			if (i < UTF8TAB && utf8tab[i] && utf8tab[i][j]) {
				strcpy(p, utf8tab[i][j]);
				p += strlen(utf8tab[i][j]);
			}
			else
				*p++ = '?';
		}
	*p++ = '\0';
	*ps = xrealloc(buf, p - buf);
	return inexact;
}
	
int charset_is_utf8(const char *s)
{
	const char *t = s;
	int n;

	while ((n = parse_utf8(&t)))
		if (n == -1)
			return 0;
	return 1;
}

/*
 * Here we define the acceptable characters in a DB entry:
 * - ASCII control chars: 9 (tab), 10 (lf), 13 (cr)
 * - all printable ASCII: 32..127
 * - everything from U+00A0 upwards except:
 *   - surrogates: U+D800..U+DFFF
 *   - BOM and reverse BOM: U+FEFF, U+FFFE
 *   - U+FFFF
 * These code points are excluded because they are likely to
 * arise through misconversions and may cause problems.
 * There are lots of other weird UCS characters which would not
 * be exactly welcome in a DB entry, but it's best to keep the
 * rule simple and consistent.
 */
#define GOOD_ASCII(c) \
	((32 <= (c) && (c) < 127) || \
	 (c) == 9 || (c) == 10 || (c) == 13)
#define GOOD_UCS(c) \
	(GOOD_ASCII(c) || \
	 ((c) >= 160 && ((c) & ~0x3ff) != 0xd800 && \
	  (c) != 0xfeff && (c) != 0xfffe && (c) != 0xffff))
#define GOOD_LATIN1(c) \
	(GOOD_ASCII(c) || \
	 ((c) >= 160))

int charset_is_valid_utf8(const char *s)
{
	const char *t = s;
	int n;

	while ((n = parse_utf8(&t))) {
		if (!GOOD_UCS(n))
			return 0;
	}
	return 1;
}

int charset_is_valid_ascii(const char *s)
{
	for (; *s; s++) {
		if (!GOOD_ASCII(*s))
			return 0;
	}
	return 1;
}

int charset_is_valid_latin1(const char *s)
{
	for (; *s; s++) {
		if (!GOOD_LATIN1((unsigned char)*s))
			return 0;
	}
	return 1;
}
