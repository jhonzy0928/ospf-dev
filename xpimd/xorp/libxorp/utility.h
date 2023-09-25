/* -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*- */

/*
 * Copyright (c) 2001-2007 International Computer Science Institute
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software")
 * to deal in the Software without restriction, subject to the conditions
 * listed in the XORP LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the XORP LICENSE file; the license in that file is
 * legally binding.
 */

/*
 * $XORP: xorp/libxorp/utility.h,v 1.14 2007/02/16 22:46:28 pavlin Exp $
 */

#ifndef __LIBXORP_UTILITY_H__
#define __LIBXORP_UTILITY_H__

/*
 * Compile time assertion.
 */
#ifndef static_assert
// +0 is to work around clang bug.
#define static_assert(a) switch ((a) + 0) case 0: case ((a) + 0):
#endif /* static_assert */

/*
 * A macro to avoid compilation warnings about unused functions arguments.
 * XXX: this should be used only in C. In C++ just remove the argument name
 * in the function definition.
 */
#ifdef UNUSED
# undef UNUSED
#endif /* UNUSED */
#define UNUSED(var)	static_assert(sizeof(var) != 0)

#ifdef __cplusplus
#define cstring(s) (s).str().c_str()
#endif

/*
 * XORP code uses typedefs (e.g. uint32_t, int32_t) rather than using
 * the base types, because the 'C' language allows a compiler to use a
 * natural size for base type. The XORP code is internally consistent
 * in this usage, one problem arises with format strings in printf
 * style functions.
 *
 * In order to print a size_t or uint32_t with "%u" it is necessary to
 * cast to an unsigned int. On Mac OS X a size_t is not an unsigned
 * int. On windows uint32_t is not an unsigned int.
 *
 * In order to print a int32_t with a "%d" it is necessary to cast to
 * a signed int. On windows int32_t is not a signed int.
 *
 * The two convenience macros are provided to perform the cast.
 */
#ifdef __cplusplus
#define	XORP_UINT_CAST(x)	static_cast<unsigned int>(x)
#define	XORP_INT_CAST(x)	static_cast<int>(x)
#endif

#define ADD_POINTER(pointer, size, type)				\
	((type)(void *)(((uint8_t *)(pointer)) + (size)))

/*
 * Micro-optimization: byte ordering fix for constants.  htonl uses
 * CPU instructions, whereas the macro below can be handled by the
 * compiler front-end for literal values.
 */
#if defined(WORDS_BIGENDIAN)
#  define htonl_literal(x) (x)
#elif defined(WORDS_SMALLENDIAN)
#  define htonl_literal(x) 						      \
		((((x) & 0x000000ffU) << 24) | (((x) & 0x0000ff00U) << 8) |   \
		 (((x) & 0x00ff0000U) >> 8) | (((x) & 0xff000000U) >> 24))
#else
#  error "Missing endian definition from config.h"
#endif

/*
 * Various ctype(3) wrappers that work properly even if the value of the int
 * argument is not representable as an unsigned char and doesn't have the
 * value of EOF.
 */
#ifdef __cplusplus
extern "C" {
#endif

extern int xorp_isalnum(int c);
extern int xorp_isalpha(int c);
/*
 * TODO: for now comment-out xorp_isblank(), because isblank(3) is introduced
 * with ISO C99, and may not always be available on the system.
 */
/* extern int xorp_isblank(int c); */
extern int xorp_iscntrl(int c);
extern int xorp_isdigit(int c);
extern int xorp_isgraph(int c);
extern int xorp_islower(int c);
extern int xorp_isprint(int c);
extern int xorp_ispunct(int c);
extern int xorp_isspace(int c);
extern int xorp_isupper(int c);
extern int xorp_isxdigit(int c);
extern int xorp_tolower(int c);
extern int xorp_toupper(int c);

/*
 * A strptime(3) wrapper that uses a local implementation of strptime(3)
 * if the operating system doesn't have one. Note that the particular
 * implementation is inside file "libxorp/strptime.c".
 */
extern char *xorp_strptime(const char *buf, const char *fmt, struct tm *tm);

#ifdef __cplusplus
}
#endif

#endif /* __LIBXORP_UTILITY_H__ */
