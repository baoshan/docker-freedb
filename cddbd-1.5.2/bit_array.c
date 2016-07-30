/*
 *   cddbd - CD Database Protocol Server
 *
 *   Copyright (C) 2003-2004  Starox (starox@free.fr)
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

#ifndef LINT
static char *const _bit_array_c_ident_ = "@(#)$Id: bit_array.c,v 1.4 2004/08/30 01:05:36 joerg78 Exp $";
#endif 

#include "bit_array.h"
#include <string.h>

void bit_uint2char(unsigned int n, unsigned int width, unsigned int ti[], unsigned char tc[]) {
	unsigned index_dst=0,i;
	unsigned mask = ( 1 << width) - 1 ;
	memset(tc,0,bit_size(n,width)); /* sanity */
	
	for ( i=0; i < n ;i++) {
		unsigned int number = ti[i] & mask;
		int index_number = width;

		while ( index_number > 0 ) {
			unsigned int car_rest = 8 - ( index_dst & 7 );
			unsigned int tmp ;
			if ( index_number > car_rest ) {
				tmp = number >> ( index_number - car_rest );
				tc[index_dst / 8] |= tmp;
				index_number -= car_rest ;
				index_dst += car_rest ;
			} else {
				tmp = number << ( car_rest - index_number );
				tc[index_dst / 8] |= tmp;
				index_dst+=index_number;	
				index_number=0;
			}
		}
	}
}

void bit_char2uint(unsigned int n, unsigned int width, unsigned int ti[], unsigned char tc[]) {
	unsigned int index_src=0; /* bits read in tc */
	unsigned int i=0; /* integer read */
	
	for ( i=0; i<n; i++ ) {
		unsigned int index_number=width; /* bits written in number */
		unsigned int number=0;

		while ( index_number > 0 ) {
			unsigned char car;
			unsigned int car_rest;
			unsigned int tmp;

			car=tc[ index_src / 8 ];
			car_rest = 8 - ( index_src & 0x7 ) ;
			
			if ( index_number <= car_rest ) {
				tmp = car >> ( car_rest - index_number );
				tmp &= ( 1 << index_number )  -1;
				number |= tmp;
				index_src+=index_number;
				index_number=0;
			} else {
				tmp = car & ( ( 1 << car_rest )  - 1 ) ;
				number |= tmp << ( index_number - car_rest ) ;
				index_src+=car_rest;
				index_number-=car_rest;
			}
		}
		ti[i]=number;
	}
}
