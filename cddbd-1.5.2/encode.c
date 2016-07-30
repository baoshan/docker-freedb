/*
 *   cddbd - CD Database Protocol Server
 *
 *   Copyright (C) 1996-1997  Steve Scherf (steve@moonsoft.com)
 *   Portions Copyright (C) 2001  by various authors
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
 *   $Id: encode.c,v 1.6 2003/12/02 02:30:51 joerg78 Exp $
 *
 */
 
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include "cddbd.h"


/* Preprocessor definitions. */

#define MAXLINELEN	75

#define INS_NEWLINE(p, l) \
	(l)++; \
	if((l) >= MAXLINELEN) { \
		(p)++; \
		*(p) = '\n'; \
		(l) = 0; \
	}


/* Global variables. */

encode_t encoding_types[] = {
	{ "quoted-printable",	rfc_1521_qp_encode,	rfc_1521_qp_decode },
	{ "base64",		rfc_1521_base64_encode,	rfc_1521_base64_decode },
	{ "7bit",			0,			0 },
	{ "8bit",			0,			0 },
	{ "binary",		0,			0 },
	{ 0 }
};

int b2amap[] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, 52, 53, 54,
	55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3,
	4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
	21, 22, 23, 24, 25, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31,
	32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
	49, 50, 51, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};


char a2bmap[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
	'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b',
	'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
	'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2', '3',
	'4', '5', '6', '7', '8', '9', '+', '/'
};


/* Prototypes. */

void out_octet(int, int, unsigned char **);
void out_b64(int, int, unsigned char **, int *);


int
rfc_1521_base64_decode(int op, unsigned char *buf, unsigned char **out,
    int *len)
{
	int i;
	int m;
	static int r;
	static int val;
	static int ended;
	static int started = 0;
	static int size = 0;
	static unsigned char *obuf = 0;
	unsigned char *p;

	switch(op)
	{
	case EO_START:
		r = 0;
		val = 0;
		ended = 0;
		started++;

		return EN_OK;

	case EO_END:
		if(!started)
			return EN_ERROR;

		started = 0;

		if(obuf != 0) {
			size = 0;
			obuf = 0;
			free(obuf);
		}

		if(r != 0) {
			r = 0;
			return EN_INPUT;
		}

		return EN_OK;

	case EO_DECODE:
		if(!started)
			return EN_ERROR;

		if(ended || *len == 0) {
			*out = 0;
			*len = 0;
			return EE_OK;
		}

		break;

	default:
		return EN_ERROR;
	}

	if(*len > size) {
		if(obuf != 0)
			free(obuf);

		/* Get twice as much space as we might need. */
		size = ((*len * 2) / CDDBBUFSIZ) + CDDBBUFSIZ;
		obuf = (unsigned char *)malloc(size);

		if(obuf == NULL) {
			size = 0;
			return EN_ERROR;
		}
	}

	for(i = 0, p = obuf; i < *len; i++, buf++) {
		/* Special end-processing. */
		if(*buf == '=') {
			switch(r) {
			case 0:
			case 1:
				/* Input is corrupt. */
				return EN_INPUT;

			case 2:
			case 3:
				out_octet(val, (r - 1), &p);

				r = 0;
				val = 0;
				*out = obuf;
				*len = p - obuf;

				ended++;

				return EN_OK;

			default:
				/* Internal error. */
				return EN_ERROR;
			}
		}

		m = b2amap[*buf];
		if(m == -1)
			continue;

		val |= (m << ((3 - r) * 6));

		if(r == 3) {
			out_octet(val, 3, &p);
			r = 0;
			val = 0;
		}
		else
			r++;
	}

	*out = obuf;
	*len = p - obuf;

	return EN_OK;
}



void
out_octet(int val, int cnt, unsigned char **p)
{
	int i;

	for(i = 0; i < cnt; i++, (*p)++)
		**p = (val >> ((2 - i) * 8)) & 0xFF;
}


void
out_b64(int val, int cnt, unsigned char **p, int *llen)
{
	int i;
	int cc;

	cc = (cnt * 8) / 6;
	if(((cnt * 8) % 6) != 0)
		cc++;

	for(i = 0; i < cc; i++, (*p)++) {
		**p = a2bmap[(val >> ((3 - i) * 6)) & 0x3F];
		INS_NEWLINE(*p, *llen);
	}

	for(i = 0; i < (4 - cc); i++, (*p)++) {
		**p = '=';
		INS_NEWLINE(*p, *llen);
	}

	if(((4 - cc) > 0) && (*llen > 0)) {
		*llen = 0;
		**p = '\n';
		(*p)++;
	}
}


int
rfc_1521_qp_decode(int op, unsigned char *buf, unsigned char **out, int *len)
{
	int c;
	unsigned char *p;
	unsigned char *p2;
	static int cont = 0;
	static int size = 0;
	static int started = 0;
	static unsigned char *obuf = 0;
	
	switch(op)
	{
	case EO_START:
		cont = 0;
		started++;

		return EN_OK;

	case EO_END:
		if(!started)
			return EN_ERROR;

		started = 0;

		if(obuf != 0) {
			size = 0;
			obuf = 0;
			free(obuf);
		}

		if(cont)
			return EN_INPUT;

		return EN_OK;

	case EO_DECODE:
		if(!started)
			return EN_ERROR;

		if(*len == 0) {
			*len = 0;
			*out = 0;
			return EN_OK;
		}

		break;

	default:
		return EN_ERROR;
	}

	cont = 0;

	if(*len > size) {
		if(obuf != 0)
			free(obuf);

		/* Get twice as much space as we might need. */
		size = ((*len * 2) / CDDBBUFSIZ) + CDDBBUFSIZ;
		obuf = (unsigned char *)malloc(size);

		if(obuf == NULL) {
			size = 0;
			return EN_ERROR;
		}
	}

	/* Check for a "=" at the end of the line. */
	for(p = &buf[*len - 1]; p >= buf; p--)
		if(!is_crlf((int)*p))
			break;

	if(p >= buf && *p == '=') {
		*p = '\0';
		cont = 1;
	}

	p = buf;
	p2 = obuf;

	while(*p != '\0') {
		/* If we have a mapping, unmap it and compress. */
		c = octet_to_char((unsigned char *)p, '=');

		if(c >= 0) {
			*p2 = (unsigned char)c;
			p += 3;
		}
		else {
			/* No unencoded '=' characters. */
			if(*p == '=')
				return EN_INPUT;

			*p2 = *p;
			p++;
		}

		p2++;
	}

	*len = p2 - obuf;
	*out = obuf;

	return EN_OK;
}


int
rfc_1521_qp_encode(int op, unsigned char *buf, unsigned char **out, int *len)
{
	int i;
	unsigned char *p;
	unsigned char *p2;
	static int llen;
	static int size = 0;
	static int started = 0;
	static unsigned char *obuf = 0;
	
	switch(op)
	{
	case EO_START:
		llen = 0;
		started++;

		return EN_OK;

	case EO_END:
		if(!started)
			return EN_ERROR;

		*len = 0;
		llen = 0;
		started = 0;

		if(obuf != 0) {
			size = 0;
			obuf = 0;
			free(obuf);
		}

		return EN_OK;

	case EO_ENCODE:
		if(!started)
			return EN_ERROR;

		if(*len == 0) {
			*len = 0;
			*out = 0;
			return EN_OK;
		}

		break;

	default:
		return EN_ERROR;
	}

	if((*len * 4) > size) {
		if(obuf != 0)
			free(obuf);

		/* Get twice as much space as we might need. */
		size = ((*len * 8) / CDDBBUFSIZ) + CDDBBUFSIZ;
		obuf = (unsigned char *)malloc(size);

		if(obuf == NULL) {
			size = 0;
			return EN_ERROR;
		}
	}

	p = buf;
	p2 = obuf;

	for(i = 0; i < *len; i++, p++) {
		if(llen >= MAXLINELEN) {
			strcpy((char *)p2, "=\n");
			p2 += 2;
			llen = 0;
		}
		if(is_crlf((int)*p))
			llen = 0;

		if(is_rfc_1521_print(*p) && !(is_wspace((int)*p) &&
		    ((i + 1) < *len) && is_crlf((int)*(p + 1)))) {
			if((llen + 1) >= MAXLINELEN) {
				strcpy((char *)p2, "=\n");
				p2 += 2;
				llen = 0;
			}

			*p2 = *p;
			p2++;
			llen++;
		}
		else {
			if((llen + 3) >= MAXLINELEN) {
				strcpy((char *)p2, "=\n");
				p2 += 2;
				llen = 0;
			}

			strcpy((char *)p2, char_to_octet(*p, '='));
			p2 += 3;
			llen += 3;
		}
	}

	*p2 = '\0';

	*len = p2 - obuf;
	*out = obuf;

	return EN_OK;
}


int
rfc_1521_base64_encode(int op, unsigned char *buf, unsigned char **out,
    int *len)
{
	int i;
	unsigned char *p;
	static int r;
	static int val;
	static int llen;
	static int size = 0;
	static int started = 0;
	static unsigned char *obuf = 0;
	
	switch(op)
	{
	case EO_START:
		r = 0;
		val = 0;
		llen = 0;
		started++;

		return EN_OK;

	case EO_END:
		if(!started)
			return EN_ERROR;

		started = 0;

		if(r != 0) {
			p = obuf;
			out_b64(val, r, &p, &llen);

			*p = '\0';
			*out = obuf;
			*len = p - obuf;
		}
		else {
			if(llen > 0) {
				*len = 1;
				*out = (unsigned char *)"\n";
			}
			else
				*len = 0;

			if(obuf != 0) {
				size = 0;
				obuf = 0;
				free(obuf);
			}
		}

		llen = 0;

		return EN_OK;

	case EO_ENCODE:
		if(!started)
			return EN_ERROR;

		if(*len == 0) {
			*len = 0;
			*out = 0;
			return EN_OK;
		}

		break;

	default:
		return EN_ERROR;
	}

	if((*len * 2) > size) {
		if(obuf != 0)
			free(obuf);

		/* Get twice as much space as we might need. */
		size = ((*len * 4) / CDDBBUFSIZ) + CDDBBUFSIZ;
		obuf = (unsigned char *)malloc(size);

		if(obuf == NULL) {
			size = 0;
			return EN_ERROR;
		}
	}

	for(i = 0, p = obuf; i < *len; i++, buf++) {
		val |= (*buf << ((2 - r) * 8));

		r++;

		if(r == 3) {
			out_b64(val, r, &p, &llen);
			r = 0;
			val = 0;
		}
	}

	*p = '\0';

	*len = p - obuf;
	*out = obuf;

	return EN_OK;
}


int
octet_to_char(unsigned char *p, char esc)
{
	int val;

	if(*p == (unsigned char)esc && is_qp_hex(p[1]) && is_qp_hex(p[2])) {
		sscanf((char *)(p + 1), "%2x", &val);
		return val;
	}

	return -1;
}


char *
char_to_octet(unsigned char c, char esc)
{
	static char oct[4];

	sprintf(oct, "%c%1X%1X", esc, ((c >> 4) & 0xF), (c & 0xF));
	oct[3] = '\0';

	return oct;
}


int
is_qp_hex(unsigned char c)
{
	return((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
	    (c >= 'a' && c <= 'f'));
}


int
is_rfc_1521_print(unsigned char c)
{
	return((c >= '!' && c <= '<') || (c >= '>' && c <= '~') ||
	    is_wspace((int)c) || is_crlf((int)c));
}


int
is_rfc_1521_mappable(unsigned char *p, int eq, int sp)
{
	int len;
	int space;

	len = 0;
	space = 0;

	while(*p != '\0') {
		if(*p == '=') {
			if(!eq)
				return 1;
		}
		else {
			if(!is_rfc_1521_print(*p))
				return 1;
		}

		if(is_crlf((int)*p) && space)
			return 1;

		if(!sp && is_wspace((int)*p))
			space++;
		else
			space = 0;

		p++;
		len++;

		if(len >= MAXLINELEN)
			return 1;
	}

	if(space)
		return 1;

	return 0;
}


/* Wheeler and Needham's TEA encoding. */

void
cddbd_tea(int op, ct_data_t *v, ct_key_t *k)
{
	int n;
	uint32_t y;
	uint32_t z;
	uint32_t a;
	uint32_t b;
	uint32_t c;
	uint32_t d;
	uint32_t sum;

	switch(op) {
	case EO_ENCODE:
	case EO_DECODE:
		break;

	default:
		return;
	}

	y = v->cd_data[0];
	z = v->cd_data[1];

	a = k->ck_key[0];
	b = k->ck_key[1];
	c = k->ck_key[2];
	d = k->ck_key[3];

	if(op == EO_ENCODE) {
		sum = 0;

		for(n = 0; n < CT_PASSES; n++) {
			sum += CT_DELTA;
			y += ((z << 4) + a) ^ (z + sum) ^ ((z >> 5) + b);
			z += ((y << 4) + c) ^ (y + sum) ^ ((y >> 5) + d);
		}
	}
	else {
		sum = (uint32_t)CT_SUM;

		for(n = 0; n < CT_PASSES; n++) {
			z -= ((y << 4) + c) ^ (y + sum) ^ ((y >> 5) + d);
			y -= ((z << 4) + a) ^ (z + sum) ^ ((z >> 5) + b);
			sum -= CT_DELTA;
		}
	}

	v->cd_data[0] = y;
	v->cd_data[1] = z;
}


ct_key_t *
strtokey(char *str)
{
	int i;
	static ct_key_t key;

	if(strlen(str) != CT_PASSWD_LEN)
		return 0;

	for(i = 0; i < CT_KEY_LEN; i++) {
		sscanf(str, "%08x", &key.ck_key[i]);
		str += CDDBUINTLEN;
	}

	return(&key);
}


char *
keytostr(ct_key_t *key)
{
	int i;
	char *p;
	static char str[CT_PASSWD_LEN + 1];

	for(p = str, i = 0; i < CT_KEY_LEN; i++) {
		sprintf(p, "%08X", key->ck_key[i]);
		p += CDDBUINTLEN;
	}

	return str;
}


char *
crctostr(uint32_t crc, uint32_t salt, ct_key_t *key)
{
	int i;
	char *p;
	ct_key_t tkey;
	ct_data_t data;
	static char str[CDDBXCRCLEN + 1];

	if(key == 0) {
		key = &tkey;
		for(i = 0; i < CT_KEY_LEN; i++)
			tkey.ck_key[i] = CT_PASSWD;
	}

	data.cd_data[0] = crc ^ salt ^ key->ck_key[0];
	data.cd_data[1] = (uint32_t)cddbd_rand();

	cddbd_tea(EO_ENCODE, &data, key);

	for(p = str, i = 0; i < CT_DATA_LEN; i++) {
		sprintf(p, "%08X", data.cd_data[i]);
		p += CDDBUINTLEN;
	}

	return str;
}


uint32_t
strtocrc(char *str, uint32_t salt, ct_key_t *key)
{
	int i;
	uint32_t crc;
	ct_key_t tkey;
	ct_data_t data;

	if(key == 0) {
		key = &tkey;
		for(i = 0; i < CT_KEY_LEN; i++)
			tkey.ck_key[i] = CT_PASSWD;
	}

	for(i = 0; i < CT_DATA_LEN; i++) {
		sscanf(str, "%08x", &data.cd_data[i]);
		str += CDDBUINTLEN;
	}

	cddbd_tea(EO_DECODE, &data, key);

	crc = data.cd_data[0] ^ salt ^ key->ck_key[0];

	return crc;
}


void
generate_key(void)
{
	int i;

	printf("New key: ");

	for(i = 0; i < CT_KEY_LEN; i++)
		printf("%08X", (uint32_t)cddbd_rand());

	printf("\n");
}


void
generate_pwd(void)
{
	ct_key_t *key;
	uint32_t data;
	uint32_t salt;
	char buf[CDDBBUFSIZ];

	printf("Input key: ");
	if(fgets(buf, sizeof(buf), stdin) == NULL)
		return;
	strip_crlf(buf);

	if((key = strtokey(buf)) == NULL) {
		printf("Illegal key.\n");
		return;
	}

	printf("Input salt: ");
	if(fgets(buf, sizeof(buf), stdin) == NULL)
		return;
	strip_crlf(buf);

	if(strlen(buf) != CDDBUINTLEN || sscanf(buf, "%08x", &salt) != 1) {
		printf("Illegal salt.\n");
		return;
	}

	printf("Input data: ");
	if(fgets(buf, sizeof(buf), stdin) == NULL)
		return;
	strip_crlf(buf);

	if(strlen(buf) != CDDBUINTLEN || sscanf(buf, "%08x", &data) != 1) {
		printf("Illegal salt.\n");
		return;
	}

	printf("Password: %s\n", crctostr(data, salt, key));
}
