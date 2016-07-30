/*
 *   cddbd - CD Database Protocol Server
 *
 *   utf8.h: Tables for transcribing from UTF-8 to ISO-8859-1.
 *   Later, this file might be automatically generated.
 *   $Id: utf8.h,v 1.3.2.1 2006/04/06 17:20:40 megari Exp $
 *
 *   Copyright (C) 2001  Edmund Grimley Evans <edmundo@rano.org>
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

#ifndef __UTF8_H__
#define __UTF8_H__

#define MAXTRANS 3

#define UTF8TAB 1

static char *page01[] = {
	"A", "a", "A", "a", "A", "a", "\264C", "\264c",
	"^C", "^c", "C", "c", "C", "c", "D", "d",
	"D", "d", "E", "e", "E", "e", "E", "e",
	"E", "e", "E", "e", "^G", "^g", "G", "g",
	"G", "g", "G", "g", "^H", "^h", "H", "h",
	"~I", "~i", "I", "i", "I", "i", "I", "i",
	"I", "i", "IJ", "ij", "^J", "^j", "K", "k",
	0, "L", "l", "L", "l", "L", "l", "L",
	"l", "L", "l", "\264N", "\264n", "N", "n", "N",
	"n", "'n", 0, 0, "O", "o", "O", "o",
	"\"O", "\"o", "OE", "oe", "\264R", "\264r", "R", "r",
	"R", "r", "\264S", "\264s", "^S", "^s", "S", "s",
	"S", "s", "T", "t", "T", "t", "T", "t",
	"~U", "~u", "U", "u", "U", "u", "U", "u",
	"\"U", "\"u", "U", "u", "^W", "^w", "^Y", "^y",
	"\"Y", "\264Z", "\264z", "Z", "z", "Z", "z", "s",
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, "f", 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, "LJ",
	"Lj", "lj", "NJ", "Nj", "nj", 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, "DZ", "Dz", "dz", 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
};

static char **utf8tab[UTF8TAB] = {
	page01
};
#endif
