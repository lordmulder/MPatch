/* md5.c - an implementation of the MD5 algorithm, based on RFC 1321.
*
* Copyright: 2007-2012 Aleksey Kravchenko <rhash.admin@gmail.com>
*
* Permission is hereby granted,  free of charge,  to any person  obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction,  including without limitation
* the rights to  use, copy, modify,  merge, publish, distribute, sublicense,
* and/or sell copies  of  the Software,  and to permit  persons  to whom the
* Software is furnished to do so.
*
* This program  is  distributed  in  the  hope  that it will be useful,  but
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
* or FITNESS FOR A PARTICULAR PURPOSE.  Use this program  at  your own risk!
*/

#ifndef _INC_RHASH_MD5_H
#define _INC_RHASH_MD5_H

#include <stdint.h>

#define md5_block_size 64U
#define md5_hash_size  16U

/* algorithm context */
typedef struct
{
	uint32_t message[md5_block_size / 4]; /* 512-bit buffer for leftovers */
	uint64_t length;   /* number of processed bytes */
	uint32_t hash[4];  /* 128-bit algorithm internal hashing state */
}
md5_ctx;

/* hash functions */

void mpatch_md5_init(md5_ctx *const ctx);
void mpatch_md5_update(md5_ctx *const ctx, const uint8_t *msg, uint_fast32_t size);
void mpatch_md5_final(md5_ctx *const ctx, uint_fast8_t *const result);
void mpatch_md5_digest(const uint_fast8_t *const msg, const uint_fast32_t size, uint8_t *const result);

#endif /*_INC_RHASH_MD5_H*/
