/*
 *   cddbd - CD Database Protocol Server
 *
 *   Copyright (C) 2004  Starox (starox@free.fr)
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

#ifndef __BIT_ARRAY_H__
#define __BIT_ARRAY_H__

#ifndef LINT
static char *const _bit_array_ident_ = "@(#)$Id: bit_array.h,v 1.3.2.1 2006/04/06 17:20:40 megari Exp $";
#endif 

#define bit_size(n,width) ( ( (n) * (width) + 7 ) / 8 )

void bit_uint2char(unsigned int, unsigned int, unsigned int [], unsigned char []);
void bit_char2uint(unsigned int, unsigned int, unsigned int [], unsigned char []);

#endif /* __BIT_ARRAY_H__ */
