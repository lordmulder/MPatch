/* ---------------------------------------------------------------------------------------------- */
/* MPatchLib - simple patch and compression library                                               */
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

#ifndef _INC_ERRORS_H
#define _INC_ERRORS_H 

#include <errno.h>

#define MAKE_ERROR_CODE(X) { X, #X }

static const struct
{
	const int value;
	const char *const name;
}
ERRNO_CODES[] =
{
	MAKE_ERROR_CODE(EPERM),
	MAKE_ERROR_CODE(ENOENT),
	MAKE_ERROR_CODE(ESRCH),
	MAKE_ERROR_CODE(EINTR),
	MAKE_ERROR_CODE(EIO),
	MAKE_ERROR_CODE(ENXIO),
	MAKE_ERROR_CODE(E2BIG),
	MAKE_ERROR_CODE(ENOEXEC),
	MAKE_ERROR_CODE(EBADF),
	MAKE_ERROR_CODE(ECHILD),
	MAKE_ERROR_CODE(EAGAIN),
	MAKE_ERROR_CODE(ENOMEM),
	MAKE_ERROR_CODE(EACCES),
	MAKE_ERROR_CODE(EFAULT),
	MAKE_ERROR_CODE(EBUSY),
	MAKE_ERROR_CODE(EEXIST),
	MAKE_ERROR_CODE(EXDEV),
	MAKE_ERROR_CODE(ENODEV),
	MAKE_ERROR_CODE(ENOTDIR),
	MAKE_ERROR_CODE(EISDIR),
	MAKE_ERROR_CODE(EINVAL),
	MAKE_ERROR_CODE(ENFILE),
	MAKE_ERROR_CODE(EMFILE),
	MAKE_ERROR_CODE(ENOTTY),
	MAKE_ERROR_CODE(EFBIG),
	MAKE_ERROR_CODE(ENOSPC),
	MAKE_ERROR_CODE(ESPIPE),
	MAKE_ERROR_CODE(EROFS),
	MAKE_ERROR_CODE(EMLINK),
	MAKE_ERROR_CODE(EPIPE),
	MAKE_ERROR_CODE(EDOM),
	MAKE_ERROR_CODE(ERANGE),
	MAKE_ERROR_CODE(EDEADLK),
	MAKE_ERROR_CODE(ENAMETOOLONG),
	MAKE_ERROR_CODE(ENOLCK),
	MAKE_ERROR_CODE(ENOSYS),
	MAKE_ERROR_CODE(ENOTEMPTY),
	MAKE_ERROR_CODE(EILSEQ),
	MAKE_ERROR_CODE(STRUNCATE),
	{ 0, NULL }
};

#undef MAKE_ERROR_CODE

#endif //_INC_ERRORS_H
