/*
 *   cddbd - CD Database Protocol Server
 *
 *   strutil.c: various string utilities
 *   $Id: strutil.c,v 1.7 2003/12/02 02:30:51 joerg78 Exp $
 *
 *   Copyright (C) 2001-2003  by various authors
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "cddbd.h"

int
is_instr(char c, char *str)
{

	if(*str == '\0')
		return 1;

	while(*str != '\0') {
		if(c == *str)
			return 1;
		++str;
	}

	return 0;
}


/* This function removes CR and LF chars at the end
 * of a string by replacing them with NULL chars.
 */
void
strip_crlf(char *buf)
{
	char *p;

	/* Strip out newlines. */
	p = &buf[strlen(buf) - 1];

	while(*p == '\n' || *p == '\r') {
		*p = '\0';

		if(p == buf)
			break;
		p--;
	}
}


/* This function determines if the line in
 * a string contain the ## comment line.
 */
int
is_DblHash(char *buf)
{
	char *p;

	p = &buf[0];
	if (*p == '#' && *(++p) == '#') {
		return 1;
	}
	
	return 0;
}


