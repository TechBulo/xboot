/*
 * sys-decompress.c
 *
 * Copyright(c) 2007-2018 Jianjun Jiang <8192542@qq.com>
 * Official site: http://xboot.org
 * Mobile phone: +86-18665388956
 * QQ: 8192542
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <xboot.h>

#define expect(expr,value)	(expr)
#define unlikely(expr)		expect((expr) != 0, 0)

#define MINMATCH			(4)
#define WILDCOPYLENGTH		(8)

#define ML_BITS				(4)
#define ML_MASK				((1 << ML_BITS)-1)
#define RUN_BITS			(8 - ML_BITS)
#define RUN_MASK			((1 << RUN_BITS)-1)

static void lz4_write32(void * ptr, uint32_t value)
{
    memcpy(ptr, &value, sizeof(value));
}

static uint16_t lz4_read_le16(void * ptr)
{
    uint8_t * p = (uint8_t *)ptr;
    return (uint16_t)((uint16_t)p[0] + (p[1] << 8));
}

static void lz4_wildcopy(void * dst, void * src, void * edst)
{
    memcpy(dst, src, edst - dst);
}

void sys_decompress(char * dst, char * src, int size)
{
    uint8_t * ip = (uint8_t *)src;
    uint8_t * op = (uint8_t *)dst;
    uint8_t * oend = op + size;
    uint8_t * cpy;
    unsigned char dec32table[8];
    char dec64table[8];
	size_t length;
	uint8_t * match;
	size_t offset;
	unsigned token;
	unsigned s;
	int dec64;
	uint8_t * copylimit;

	dec32table[0] = 0;
	dec32table[1] = 1;
	dec32table[2] = 2;
	dec32table[3] = 1;
	dec32table[4] = 4;
	dec32table[5] = 4;
	dec32table[6] = 4;
	dec32table[7] = 4;

	dec64table[0] = 0;
	dec64table[1] = 0;
	dec64table[2] = 0;
	dec64table[3] = -1;
	dec64table[4] = 0;
	dec64table[5] = 1;
	dec64table[6] = 2;
	dec64table[7] = 3;

	while(1)
	{
		token = *ip++;
		if((length = (token >> ML_BITS)) == RUN_MASK)
		{
			do {
				s = *ip++;
				length += s;
			} while(1 & (s == 255));
		}

		cpy = op + length;
		memcpy(op, ip, length);
		if(op + length > oend - WILDCOPYLENGTH)
			break;
		ip += length;
		op = cpy;

		offset = lz4_read_le16(ip);
		ip += 2;
		match = op - offset;
		lz4_write32(op, (uint32_t) offset);

		length = token & ML_MASK;
		if(length == ML_MASK)
		{
			do {
				s = *ip++;
				length += s;
			} while(s == 255);
		}
		length += MINMATCH;

		cpy = op + length;
		if(unlikely(offset < 8))
		{
			dec64 = dec64table[offset];
			op[0] = match[0];
			op[1] = match[1];
			op[2] = match[2];
			op[3] = match[3];
			match += dec32table[offset];
			memcpy(op + 4, match, 4);
			match -= dec64;
		}
		else
		{
			memcpy(op, match, 8);
			match += 8;
		}
		op += 8;

		if(unlikely(cpy > oend - 12))
		{
			copylimit = oend - (WILDCOPYLENGTH - 1);
			if(op < copylimit)
			{
				lz4_wildcopy(op, match, copylimit);
				match += copylimit - op;
				op = copylimit;
			}
			while(op < cpy)
				*op++ = *match++;
		}
		else
		{
			memcpy(op, match, 8);
			if(length > 16)
				lz4_wildcopy(op + 8, match + 8, cpy);
		}
		op = cpy;
	}
}
