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

#ident "$XORP: xorp/libproto/proto_state.cc,v 1.14 2007/02/16 22:46:03 pavlin Exp $"


//
// Protocol unit generic functionality implementation
//


#include "libproto_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"

#include "proto_state.hh"


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
 * ProtoState::ProtoState:
 * 
 * Proto state default constructor.
 **/
ProtoState::ProtoState()
{
    _flags		= 0;
    _debug_flag		= false;
    _flags &= ~XORP_ENABLED;	// XXX: default is to disable.
}

ProtoState::~ProtoState()
{
    
}

int
ProtoState::start()
{
    if (is_disabled())
	return (XORP_ERROR);
    if (is_up())
	return (XORP_OK);		// Already running

    ProtoState::reset();

    if (ProtoState::startup() != true)
	return (XORP_ERROR);

    ServiceBase::set_status(SERVICE_RUNNING);

    return (XORP_OK);
}

int
ProtoState::stop()
{
    if (is_down())
	return (XORP_OK);		// Already down

    if (ProtoState::shutdown() != true)
	return (XORP_ERROR);

    ServiceBase::set_status(SERVICE_SHUTDOWN);

    return (XORP_OK);
}

int
ProtoState::pending_start()
{
    if (is_disabled())
	return (XORP_ERROR);
    if (is_up())
	return (XORP_OK);		// Already running
    if (is_pending_up())
	return (XORP_OK);		// Already pending UP

    ServiceBase::set_status(SERVICE_STARTING);
    
    return (XORP_OK);
}

int
ProtoState::pending_stop()
{
    if (is_down())
	return (XORP_OK);		// Already down
    if (is_pending_down())
	return (XORP_OK);		// Already pending DOWN

    ServiceBase::set_status(SERVICE_SHUTTING_DOWN);

    return (XORP_OK);
}

bool
ProtoState::startup()
{
    //
    // Test the service status
    //
    if ((ServiceBase::status() == SERVICE_STARTING)
	|| (ServiceBase::status() == SERVICE_RUNNING))
	return true;

    if (ServiceBase::status() != SERVICE_READY)
	return false;

    return true;
}

bool
ProtoState::reset()
{
    if (ServiceBase::status() != SERVICE_READY)
	ServiceBase::set_status(SERVICE_READY);

    return true;
}

bool
ProtoState::shutdown()
{
    //
    // Test the service status
    //
    if ((ServiceBase::status() == SERVICE_SHUTDOWN)
	|| (ServiceBase::status() == SERVICE_SHUTTING_DOWN)
	|| (ServiceBase::status() == SERVICE_FAILED)) {
	return true;
    }

    if ((ServiceBase::status() != SERVICE_RUNNING)
	&& (ServiceBase::status() != SERVICE_STARTING)
	&& (ServiceBase::status() != SERVICE_PAUSING)
	&& (ServiceBase::status() != SERVICE_PAUSED)
	&& (ServiceBase::status() != SERVICE_RESUMING)) {
	return false;
    }

    return true;
}

void
ProtoState::disable()
{
    (void)ProtoState::shutdown();
    _flags &= ~XORP_ENABLED;
}

string
ProtoState::state_str() const
{
    if (is_disabled())
	return ("DISABLED");
    if (is_down())
	return ("DOWN");
    if (is_up())
	return ("UP");
    if (is_pending_up())
	return ("PENDING_UP");
    if (is_pending_down())
	return ("PENDING_DOWN");
    
    return ("UNKNOWN");
}

/**
 * Test if the unit state is UP.
 * 
 * @return true if the unit state is UP.
 */
bool
ProtoState::is_up() const
{
    return (ServiceBase::status() == SERVICE_RUNNING);
}

/**
 * Test if the unit state is DOWN.
 * 
 * @return true if the unit state is DOWN.
 */
bool
ProtoState::is_down() const
{
    return ((ServiceBase::status() == SERVICE_READY)
	    || (ServiceBase::status() == SERVICE_SHUTDOWN));
}

/**
 * Test if the unit state is PENDING-UP.
 * 
 * @return true if the unit state is PENDING-UP.
 */
bool
ProtoState::is_pending_up() const
{
    return (ServiceBase::status() == SERVICE_STARTING);
}

/**
 * Test if the unit state is PENDING-DOWN.
 * 
 * @return true if the unit state is PENDING-DOWN.
 */
bool
ProtoState::is_pending_down() const
{
    return (ServiceBase::status() == SERVICE_SHUTTING_DOWN);
}
