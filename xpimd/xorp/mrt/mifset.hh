// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright (c) 2001-2007 International Computer Science Institute
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software")
// to deal in the Software without restriction, subject to the conditions
// listed in the XORP LICENSE file. These conditions include: you must
// preserve this copyright notice, and you cannot mention the copyright
// holders in advertising related to the Software without their permission.
// The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
// notice is a summary of the XORP LICENSE file; the license in that file is
// legally binding.

// $XORP: xorp/mrt/mifset.hh,v 1.8 2007/02/16 22:46:38 pavlin Exp $


#ifndef __MRT_MIFSET_HH__
#define __MRT_MIFSET_HH__


//
// Multicast interface bitmap-based classes.
//


#include <sys/types.h>

#include <bitset>
#include <vector>

#include "libxorp/xorp.h"
#include "max_vifs.h"



//
// Constants definitions
//

//
// Structures/classes, typedefs and macros
//

// Interface array bitmask

typedef bitset<MAX_VIFS> Mifset;


//
// Global variables
//

//
// Global functions prototypes
//
void mifset_to_array(const Mifset& mifset, uint8_t *array);
void array_to_mifset(const uint8_t *array, Mifset& mifset);
void mifset_to_vector(const Mifset& mifset, vector<uint8_t>& vector);
void vector_to_mifset(const vector<uint8_t>& vector, Mifset& mifset);

#endif // __MRT_MIFSET_HH__
