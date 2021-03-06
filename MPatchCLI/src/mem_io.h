/* ---------------------------------------------------------------------------------------------- */
/* MPatch - simple patch and compression utility                                                  */
/* Copyright(c) 2018 LoRd_MuldeR <mulder2@gmx.de>                                                 */
/*                                                                                                */
/* Permission is hereby granted, free of charge, to any person obtaining a copy of this software  */
/* and associated documentation files (the "Software"), to deal in the Software without           */
/* restriction, including without limitation the rights to use, copy, modify, merge, publish,     */
/* distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the  */
/* Software is furnished to do so, subject to the following conditions:                           */
/*                                                                                                */
/* The above copyright notice and this permission notice shall be included in all copies or       */
/* substantial portions of the Software.                                                          */
/*                                                                                                */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING  */
/* BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND     */
/* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   */
/* DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, */
/* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.        */
/* ---------------------------------------------------------------------------------------------- */

#include <stdint.h>

#ifndef _INC_MEM_IO_H
#define _INC_MEM_IO_H 

typedef enum
{
	IO_SUCCESS        = 0,
	IO_FILE_NOT_FOUND = 1,
	IO_ACCESS_DENIED  = 2,
	IO_OUT_OF_MEMORY  = 3,
	IO_FILE_TOO_LARGE = 4,
	IO_FILE_IS_EMPTY  = 5,
	IO_FAILED         = 6
}
io_error_t;

typedef struct
{
	const uint8_t *data_ptr;
	uint32_t size;
}
rd_view_t;

typedef struct
{
	uint8_t *data_ptr;
	uint32_t size;
}
wr_view_t;

io_error_t map_file_rd(rd_view_t **const view, const wchar_t *const fileName);
io_error_t map_file_wr(wr_view_t **const view, const wchar_t *const fileName, const uint32_t size);

io_error_t unmap_file_rd(rd_view_t **const view);
io_error_t unmap_file_wr(wr_view_t **const view);

#endif //_INC_MEM_IO_H 
