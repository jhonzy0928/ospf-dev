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

#ident "$XORP: xorp/pim/pim_proto_graft.cc,v 1.12 2007/02/16 22:46:49 pavlin Exp $"


//
// PIM PIM_GRAFT messages processing.
//


#include "pim_module.h"
#include "libxorp/xorp.h"
#include "libxorp/xlog.h"
#include "libxorp/debug.h"
#include "libxorp/ipvx.hh"

#include "pim_vif.hh"


//
// Exported variables
//

//
// Local constants definitions
//

//
// Local structures/classes, typedefs and macros
//

//
// Local variables
//

//
// Local functions prototypes
//


/**
 * PimVif::pim_graft_recv:
 * @pim_nbr: The PIM neighbor message originator (or NULL if not a neighbor).
 * @src: The message source address.
 * @dst: The message destination address.
 * @buffer: The buffer with the message.
 * 
 * Receive PIM_GRAFT message.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimVif::pim_graft_recv(PimNbr *pim_nbr,
		       const IPvX& src,
		       const IPvX& , // dst
		       buffer_t *buffer)
{
    int ret_value;
    buffer_t *buffer2;
    string dummy_error_msg;
    
    //
    // Must unicast back a Graft-Ack to the originator of this Graft.
    //
    buffer2 = buffer_send_prepare();
    BUFFER_PUT_DATA(BUFFER_DATA_HEAD(buffer), buffer2,
		    BUFFER_DATA_SIZE(buffer));
    ret_value = pim_send(domain_wide_addr(), src, PIM_GRAFT_ACK, buffer2,
			 dummy_error_msg);
    
    UNUSED(pim_nbr);
    // UNUSED(dst);
    
    return (ret_value);
    
    // Various error processing
 buflen_error:
    XLOG_UNREACHABLE();
    dummy_error_msg = c_format("TX %s from %s to %s: "
			       "packet cannot fit into sending buffer",
			       PIMTYPE2ASCII(PIM_GRAFT_ACK),
			       cstring(domain_wide_addr()), cstring(src));
    XLOG_ERROR("%s", dummy_error_msg.c_str());
    return (XORP_ERROR);
}

#if 0		// TODO: XXX: implement/use it
int
PimVif::pim_graft_send(const IPvX& dst, buffer_t *buffer)
{
    int ret_value;
    IPvX src = dst.is_unicast()? domain_wide_addr() : primary_addr();
    
    ret_value = pim_send(src, dst, PIM_GRAFT, buffer);
    
    return (ret_value);

    // Various error processing
 buflen_error:
    XLOG_UNREACHABLE();
    XLOG_ERROR("TX %s from %s to %s: "
	       "packet cannot fit into sending buffer",
	       PIMTYPE2ASCII(PIM_GRAFT),
	       cstring(src), cstring(dst));
}
#endif /* 0 */
