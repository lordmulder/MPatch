/* crc32.c - an implementation of CRC32 hash function
*
* Copyright: 2006-2012 Aleksey Kravchenko <rhash.admin@gmail.com>
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

#ifndef _INC_RHASH_CRC32_H
#define _INC_RHASH_CRC32_H

#include <stdint.h>

void mpatch_crc32_init(uint32_t *const crc32);
void mpatch_crc32_update(uint32_t *const crc32, const uint8_t *const msg, const uint_fast32_t size);
void mpatch_crc32_final(const uint32_t *const crc32, uint8_t *const result);
void mpatch_crc32_compute(const uint8_t *const msg, const uint_fast32_t size, uint8_t *const result);

#endif /*_INC_RHASH_CRC32_H*/
