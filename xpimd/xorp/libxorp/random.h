/* -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-
 * vim:set sts=4 ts=8:
 *
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
 * $XORP: xorp/libxorp/random.h,v 1.5 2007/02/16 22:46:22 pavlin Exp $
 */

#ifndef __LIBXORP_RANDOM_H__
#define __LIBXORP_RANDOM_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Define the maximum bound of the random() function
 * as per 4.2BSD / SUSV2:
 * http://www.opengroup.org/onlinepubs/007908799/xsh/initstate.html
 */
#ifndef RANDOM_MAX
#define RANDOM_MAX	0x7FFFFFFF  /* 2^31 - 1 in 2's complement */
#endif

#ifndef HAVE_RANDOM
long	random(void);
void	srandom(unsigned long);
char*	initstate(unsigned long, char *, long);
char*	setstate(char *);
void	srandomdev(void);
#endif /* HAVE_RANDOM */

#ifdef __cplusplus
}
#endif

#endif /* __LIBXORP_RANDOM_H__ */
