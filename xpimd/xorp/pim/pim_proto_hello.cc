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

#ident "$XORP: xorp/pim/pim_proto_hello.cc,v 1.26 2007/02/16 22:46:49 pavlin Exp $"


//
// PIM PIM_HELLO messages processing.
//


#include "pim_module.h"
#include "libxorp/xorp.h"
#include "libxorp/xlog.h"
#include "libxorp/debug.h"
#include "libxorp/ipvx.hh"

#include "mrt/random.h"
#include "pim_node.hh"
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
static bool	pim_dr_is_better(PimNbr *pim_nbr1, PimNbr *pim_nbr2,
				 bool consider_dr_priority);


void
PimVif::pim_hello_start()
{
    // Generate a new Gen-ID
    genid().set(RANDOM(0xffffffffU));
    
    // On startup, I will become the PIM Designated Router
    pim_dr_elect();
    
    // Schedule the first PIM_HELLO message at random in the
    // interval [0, hello_triggered_delay)
    hello_timer_start_random(hello_triggered_delay().get(), 0);
}

void
PimVif::pim_hello_stop()
{
    uint16_t save_holdtime = hello_holdtime().get();
    string dummy_error_msg;
    
    hello_holdtime().set(0);		// XXX: timeout immediately
    pim_hello_send(dummy_error_msg);
    hello_holdtime().set(save_holdtime);
}

/**
 * PimVif::pim_hello_recv:
 * @pim_nbr: The PIM neighbor message originator (or NULL if not a neighbor).
 * @src: The message source address.
 * @dst: The message destination address.
 * @buffer: The buffer with the message.
 * @nbr_proto_version: The protocol version from this heighbor.
 * 
 * Receive PIM_HELLO message.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimVif::pim_hello_recv(PimNbr *pim_nbr,
		       const IPvX& src,
		       const IPvX& dst,
		       buffer_t *buffer,
		       int nbr_proto_version)
{
    bool	holdtime_rcvd = false;
    bool	lan_prune_delay_rcvd = false;
    bool	dr_priority_rcvd = false;
    bool	genid_rcvd = false;
    bool	is_genid_changed = false;
    uint16_t	option_type, option_length, option_length_spec;
    uint16_t	holdtime = 0;
    uint16_t	propagation_delay_tbit = 0;
    uint16_t	lan_prune_delay_tbit = 0;
    uint16_t	override_interval = 0;
    uint32_t	dr_priority = 0;
    uint32_t	genid = 0;
    bool	new_nbr_flag = false;
    list<IPvX>	secondary_addr_list;
    list<IPvX>::iterator addr_list_iter;
    int		rcvd_family;
    
    //
    // Parse the message
    //
    while (BUFFER_DATA_SIZE(buffer) > 0) {
	BUFFER_GET_HOST_16(option_type, buffer);
	BUFFER_GET_HOST_16(option_length, buffer);
	if (BUFFER_DATA_SIZE(buffer) < option_length)
	    goto rcvlen_error;
	switch (option_type) {
	    
	case PIM_HELLO_HOLDTIME_OPTION:
	    // Holdtime option
	    option_length_spec = PIM_HELLO_HOLDTIME_LENGTH;
	    if (option_length < option_length_spec) {
		BUFFER_GET_SKIP(option_length, buffer);
		continue;
	    }
	    BUFFER_GET_HOST_16(holdtime, buffer);
	    BUFFER_GET_SKIP(option_length - option_length_spec, buffer);
	    holdtime_rcvd = true;
	    break;
	    
	case PIM_HELLO_LAN_PRUNE_DELAY_OPTION:
	    // LAN Prune Delay option
	    option_length_spec = PIM_HELLO_LAN_PRUNE_DELAY_LENGTH;
	    if (option_length < option_length_spec) {
		BUFFER_GET_SKIP(option_length, buffer);
		continue;
	    }
	    BUFFER_GET_HOST_16(propagation_delay_tbit, buffer);
	    BUFFER_GET_HOST_16(override_interval, buffer);
	    lan_prune_delay_tbit
		= (propagation_delay_tbit & PIM_HELLO_LAN_PRUNE_DELAY_TBIT) ?
		true : false;
	    propagation_delay_tbit &= ~PIM_HELLO_LAN_PRUNE_DELAY_TBIT;
	    BUFFER_GET_SKIP(option_length - option_length_spec, buffer);
	    lan_prune_delay_rcvd = true;
	    break;
	    
	case PIM_HELLO_DR_PRIORITY_OPTION:
	    // DR_Priority option
	    option_length_spec = PIM_HELLO_DR_PRIORITY_LENGTH;
	    if (option_length < option_length_spec) {
		BUFFER_GET_SKIP(option_length, buffer);
		continue;
	    }
	    BUFFER_GET_HOST_32(dr_priority, buffer);
	    BUFFER_GET_SKIP(option_length - option_length_spec, buffer);
	    dr_priority_rcvd = true;
	    break;
	    
	case PIM_HELLO_GENID_OPTION:
	    // GenID option
	    option_length_spec = PIM_HELLO_GENID_LENGTH;
	    if (option_length < option_length_spec) {
		BUFFER_GET_SKIP(option_length, buffer);
		continue;
	    }
	    BUFFER_GET_HOST_32(genid, buffer);
	    BUFFER_GET_SKIP(option_length - option_length_spec, buffer);
	    genid_rcvd = true;
	    break;

	case PIM_HELLO_ADDRESS_LIST_OPTION:
	    // Address List option
	    while (option_length >= ENCODED_UNICAST_ADDR_SIZE(family())) {
		IPvX secondary_addr(family());
		GET_ENCODED_UNICAST_ADDR(rcvd_family, secondary_addr, buffer);
		secondary_addr_list.push_back(secondary_addr);
		option_length -= ENCODED_UNICAST_ADDR_SIZE(family());
	    }
	    if (option_length > 0)
		BUFFER_GET_SKIP(option_length, buffer);
	    break;

	default:
	    // XXX: skip unrecognized options
	    BUFFER_GET_SKIP(option_length, buffer);
	    ++_pimstat_rx_unknown_hello_option;
	    break;
	}
    }
    
    if ((pim_nbr != NULL) && genid_rcvd) {
	if ( (! pim_nbr->is_genid_present())
	     || (pim_nbr->is_genid_present() && genid != pim_nbr->genid())) {
	    //
	    // This neighbor has just restarted (or something similar).
	    //
	    is_genid_changed = true;
	    
	    //
	    // Reset any old Hello information about this neighbor.
	    //
	    pim_nbr->reset_received_options();
	}
    }
    
    if (pim_nbr == NULL) {
	// A new neighbor
	pim_nbr = new PimNbr(*this, src, nbr_proto_version);
	add_pim_nbr(pim_nbr);
	new_nbr_flag = true;
	XLOG_TRACE(pim_node().is_log_nbr() || pim_node().is_log_trace(),
		   "Added new neighbor %s on vif %s",
		   cstring(pim_nbr->primary_addr()), name().c_str());
    }
    
    // Set the protocol version for this neighbor
    pim_nbr->set_proto_version(nbr_proto_version);
    
    //
    // XXX: pim_hello_holdtime_process() is called with default value
    // even if no Holdtime option is received.
    //
    if (! holdtime_rcvd)
	holdtime = PIM_HELLO_HELLO_HOLDTIME_DEFAULT;
    pim_nbr->pim_hello_holdtime_process(holdtime);
    
    if (lan_prune_delay_rcvd)
	pim_nbr->pim_hello_lan_prune_delay_process(lan_prune_delay_tbit,
						   propagation_delay_tbit,
						   override_interval);
    
    if (dr_priority_rcvd)
	pim_nbr->pim_hello_dr_priority_process(dr_priority);
    
    if (genid_rcvd)
	pim_nbr->pim_hello_genid_process(genid);
    
    //
    // Add the secondary addresses
    //
    pim_nbr->clear_secondary_addr_list();    // First clear the old addresses
    for (addr_list_iter = secondary_addr_list.begin();
	 addr_list_iter != secondary_addr_list.end();
	 ++addr_list_iter) {
	IPvX& secondary_addr = *addr_list_iter;
	if (pim_nbr->primary_addr() == secondary_addr) {
	    // The primary address is in the secondary addresses list. Ignore.
	    continue;
	}
	if (pim_nbr->has_secondary_addr(secondary_addr)) {
	    XLOG_WARNING("RX %s from %s to %s: "
			 "duplicated secondary address %s",
			 PIMTYPE2ASCII(PIM_HELLO),
			 cstring(src), cstring(dst),
			 cstring(secondary_addr));
	    continue;
	}
	pim_nbr->add_secondary_addr(secondary_addr);

	//
	// Check if the same secondary address was advertised
	// by other neighbors.
	//
	list<PimNbr *>::iterator nbr_iter;
	for (nbr_iter = pim_nbrs().begin();
	     nbr_iter != pim_nbrs().end();
	     ++nbr_iter) {
	    PimNbr *tmp_pim_nbr = *nbr_iter;
	    if (tmp_pim_nbr == pim_nbr)
		continue;
	    if (tmp_pim_nbr->has_secondary_addr(secondary_addr)) {
		XLOG_WARNING("RX %s from %s to %s: "
			     "overriding secondary address %s that was "
			     "advertised previously by neighbor %s",
			     PIMTYPE2ASCII(PIM_HELLO),
			     cstring(src), cstring(dst),
			     cstring(secondary_addr),
			     cstring(tmp_pim_nbr->primary_addr()));
		tmp_pim_nbr->delete_secondary_addr(secondary_addr);
	    }
	}
    }
    
    if (new_nbr_flag || is_genid_changed) {
	if (i_am_dr() || (is_genid_changed && i_may_become_dr(src))) {
	    // Schedule to send an unicast Bootstrap message
	    add_send_unicast_bootstrap_nbr(src);
	}
	
	//
	// Set the flag that we must send first a Hello message before
	// any other control messages.
	//
	set_should_send_pim_hello(true);
	
	// Schedule a PIM_HELLO message at random in the
	// interval [0, hello_triggered_delay)
	// XXX: this message should not affect the periodic `hello_timer'.
	TimeVal tv(hello_triggered_delay().get(), 0);
	tv = random_uniform(tv);
	_hello_once_timer =
	    pim_node().eventloop().new_oneoff_after(
		tv,
		callback(this, &PimVif::hello_once_timer_timeout));
	
	//
	// Add the task that will process all PimMre entries that have no
	// neighbor.
	//
	if (new_nbr_flag) {
	    pim_node().pim_mrt().add_task_pim_nbr_changed(
		Vif::VIF_INDEX_INVALID,
		IPvX::ZERO(family()));
	}
	
	//
	// Add the task that will process all PimMre entries and take
	// action because the GenID of this neighbor changed.
	//
	if (is_genid_changed) {
	    pim_node().pim_mrt().add_task_pim_nbr_gen_id_changed(
		vif_index(),
		pim_nbr->primary_addr());
	}
    }
    
    // (Re)elect the DR
    pim_dr_elect();
    
    return (XORP_OK);
    
 rcvlen_error:
    XLOG_WARNING("RX %s from %s to %s: "
		 "invalid message length",
		 PIMTYPE2ASCII(PIM_HELLO),
		 cstring(src), cstring(dst));
    ++_pimstat_rx_malformed_packet;
    return (XORP_ERROR);

 rcvd_family_error:
    XLOG_WARNING("RX %s from %s to %s: "
		 "invalid address family inside = %d",
		 PIMTYPE2ASCII(PIM_HELLO),
		 cstring(src), cstring(dst), rcvd_family);
    return (XORP_ERROR);
    
    // UNUSED(dst);
}

/**
 * PimNbr::pim_hello_holdtime_process:
 * @holdtime: The holdtime for the neighbor (in seconds).
 * 
 * Process PIM_HELLO Holdtime option.
 * XXX: if no Holdtime option is received, this function is still
 * called with the default holdtime value of %PIM_HELLO_HELLO_HOLDTIME_DEFAULT.
 **/
void
PimNbr::pim_hello_holdtime_process(uint16_t holdtime)
{
    _hello_holdtime = holdtime;
    
    switch (holdtime) {
    case PIM_HELLO_HOLDTIME_FOREVER:
	// Never expire this neighbor
	_neighbor_liveness_timer.unschedule();
	break;
    default:
	// Start the Neighbor Liveness Timer
	_neighbor_liveness_timer =
	    pim_node().eventloop().new_oneoff_after(
		TimeVal(holdtime, 0),
		callback(this, &PimNbr::neighbor_liveness_timer_timeout));
	break;
    }
}

void
PimNbr::pim_hello_lan_prune_delay_process(bool lan_prune_delay_tbit,
					  uint16_t propagation_delay,
					  uint16_t override_interval)
{
    _is_lan_prune_delay_present = true;
    _is_tracking_support_disabled = lan_prune_delay_tbit;
    _propagation_delay = propagation_delay;
    _override_interval = override_interval;
}

void
PimNbr::pim_hello_dr_priority_process(uint32_t dr_priority)
{
    _is_dr_priority_present = true;
    _dr_priority = dr_priority;
}

void
PimNbr::pim_hello_genid_process(uint32_t genid)
{
    _is_genid_present = true;
    _genid = genid;
}

void
PimVif::pim_dr_elect()
{
    PimNbr *dr = &pim_nbr_me();
    list<PimNbr *>::iterator iter;
    bool consider_dr_priority = pim_nbr_me().is_dr_priority_present();
    
    for (iter = _pim_nbrs.begin(); iter != _pim_nbrs.end(); ++iter) {
	PimNbr *pim_nbr = *iter;
	if (! pim_nbr->is_dr_priority_present()) {
	    consider_dr_priority = false;
	    break;
	}
    }
    
    for (iter = _pim_nbrs.begin(); iter != _pim_nbrs.end(); ++iter) {
	PimNbr *pim_nbr = *iter;
	if (! pim_dr_is_better(dr, pim_nbr, consider_dr_priority))
	    dr = pim_nbr;
    }
    
    if (dr == NULL) {
	XLOG_FATAL("Cannot elect a DR on interface %s", name().c_str());
	return;
    }
    _dr_addr = dr->primary_addr();
    
    // Set a flag if I am the DR
    if (dr_addr() == primary_addr()) {
	if (! i_am_dr()) {
	    set_i_am_dr(true);
	    // TODO: take the appropriate action
	}
    } else {
	set_i_am_dr(false);
    }
}

/**
 * PimVif::i_may_become_dr:
 * @exclude_addr: The address to exclude in the computation.
 * 
 * Compute if I may become the DR on this interface if @exclude_addr
 * is excluded.
 * 
 * Return value: True if I may become the DR on this interface, otherwise
 * false.
 **/
bool
PimVif::i_may_become_dr(const IPvX& exclude_addr)
{
    PimNbr *dr = &pim_nbr_me();
    list<PimNbr *>::iterator iter;
    bool consider_dr_priority = pim_nbr_me().is_dr_priority_present();
    
    for (iter = _pim_nbrs.begin(); iter != _pim_nbrs.end(); ++iter) {
	PimNbr *pim_nbr = *iter;
	if (! pim_nbr->is_dr_priority_present()) {
	    consider_dr_priority = false;
	    break;
	}
    }
    
    for (iter = _pim_nbrs.begin(); iter != _pim_nbrs.end(); ++iter) {
	PimNbr *pim_nbr = *iter;
	if (pim_nbr->primary_addr() == exclude_addr)
	    continue;
	if (! pim_dr_is_better(dr, pim_nbr, consider_dr_priority))
	    dr = pim_nbr;
    }
    
    if ((dr != NULL) && (dr->primary_addr() == primary_addr()))
	return (true);
    
    return (false);
}

/**
 * pim_dr_is_better:
 * @pim_nbr1: The first neighbor to compare.
 * @pim_nbr2: The second neighbor to compare.
 * @consider_dr_priority: If true, in the comparison we consider
 * the DR priority, otherwise the DR priority is ignored.
 * 
 * Compare two PIM neighbors which of them is a better candidate to be a
 * DR (Designated Router).
 * 
 * Return value: true if @pim_nbr1 is a better candidate to be a DR,
 * otherwise %false.
 **/
static bool
pim_dr_is_better(PimNbr *pim_nbr1, PimNbr *pim_nbr2, bool consider_dr_priority)
{
    if (pim_nbr2 == NULL)
	return (true);
    if (pim_nbr1 == NULL)
	return (false);
    
    if (consider_dr_priority) {
	if (pim_nbr1->dr_priority() > pim_nbr2->dr_priority())
	    return (true);
	if (pim_nbr1->dr_priority() < pim_nbr2->dr_priority())
	    return (false);
    }
    
    // Either the DR priority is same, or we have to ignore it
    if (pim_nbr1->primary_addr() > pim_nbr2->primary_addr())
	return (true);
    
    return (false);
}

void
PimVif::hello_timer_start(uint32_t sec, uint32_t usec)
{
    _hello_timer =
	pim_node().eventloop().new_oneoff_after(
	    TimeVal(sec, usec),
	    callback(this, &PimVif::hello_timer_timeout));
}

// Schedule a PIM_HELLO message at random in the
// interval [0, (sec, usec))
void
PimVif::hello_timer_start_random(uint32_t sec, uint32_t usec)
{
    TimeVal tv(sec, usec);
    
    tv = random_uniform(tv);
    
    _hello_timer =
	pim_node().eventloop().new_oneoff_after(
	    tv,
	    callback(this, &PimVif::hello_timer_timeout));
}

void
PimVif::hello_timer_timeout()
{
    string dummy_error_msg;

    pim_hello_send(dummy_error_msg);
    hello_timer_start(hello_period().get(), 0);
}

//
// XXX: the presumption is that this timeout function is called only
// when we have added a new neighbor. If this is not true, the
// add_task_foo() task scheduling is not needed.
//
void
PimVif::hello_once_timer_timeout()
{
    pim_hello_first_send();
}

int
PimVif::pim_hello_first_send()
{
    string dummy_error_msg;

    pim_hello_send(dummy_error_msg);
    
    //
    // Unicast the Bootstrap message if needed
    //
    if (is_send_unicast_bootstrap()) {
	list<IPvX>::const_iterator nbr_iter;
	for (nbr_iter = send_unicast_bootstrap_nbr_list().begin();
	     nbr_iter != send_unicast_bootstrap_nbr_list().end();
	     ++nbr_iter) {
	    const IPvX& nbr_addr = *nbr_iter;
	    
	    // Unicast the Bootstrap messages
	    pim_node().pim_bsr().unicast_pim_bootstrap(this, nbr_addr);
	}
	
	delete_send_unicast_bootstrap_nbr_list();
    }
    
    _hello_once_timer.unschedule();
    
    return (XORP_OK);
}

int
PimVif::pim_hello_send(string& error_msg)
{
    list<IPvX> address_list;

    //
    // XXX: note that for PIM Hello messages only we use a separate
    // buffer for sending the data, because the sending of a Hello message
    // can be triggered during the sending of another control message.
    //
    buffer_t *buffer = buffer_send_prepare(_buffer_send_hello);
    uint16_t propagation_delay_tbit;
    
#if 0
    // XXX: enable if for any reason sending Hello messages is not desirable
    set_should_send_pim_hello(false);    
    return (XORP_OK);
#endif
    
    // Holdtime option
    BUFFER_PUT_HOST_16(PIM_HELLO_HOLDTIME_OPTION, buffer);
    BUFFER_PUT_HOST_16(PIM_HELLO_HOLDTIME_LENGTH, buffer);
    BUFFER_PUT_HOST_16(hello_holdtime().get(), buffer);
    
    // LAN Prune Delay option    
    BUFFER_PUT_HOST_16(PIM_HELLO_LAN_PRUNE_DELAY_OPTION, buffer);
    BUFFER_PUT_HOST_16(PIM_HELLO_LAN_PRUNE_DELAY_LENGTH, buffer);
    propagation_delay_tbit = propagation_delay().get();
    if (is_tracking_support_disabled().get())
	propagation_delay_tbit |= PIM_HELLO_LAN_PRUNE_DELAY_TBIT;
    BUFFER_PUT_HOST_16(propagation_delay_tbit, buffer);
    BUFFER_PUT_HOST_16(override_interval().get(), buffer);
    
    // DR_Priority option    
    BUFFER_PUT_HOST_16(PIM_HELLO_DR_PRIORITY_OPTION, buffer);
    BUFFER_PUT_HOST_16(PIM_HELLO_DR_PRIORITY_LENGTH, buffer);
    BUFFER_PUT_HOST_32(dr_priority().get(), buffer);
    
    // GenID option
    BUFFER_PUT_HOST_16(PIM_HELLO_GENID_OPTION, buffer);
    BUFFER_PUT_HOST_16(PIM_HELLO_GENID_LENGTH, buffer);
    BUFFER_PUT_HOST_32(genid().get(), buffer);
    
    // Address List option
    do {
	// Get the list of secondary addresses
	list<VifAddr>::const_iterator iter;
	for (iter = addr_list().begin(); iter != addr_list().end(); ++iter) {
	    const VifAddr& vif_addr = *iter;
	    if (vif_addr.addr() == primary_addr()) {
		// Ignore the primary address
		continue;
	    }
	    address_list.push_back(vif_addr.addr());
	}
    } while (false);
    if (address_list.size() > 0) {
	size_t length;
	list<IPvX>::iterator iter;

	BUFFER_PUT_HOST_16(PIM_HELLO_ADDRESS_LIST_OPTION, buffer);
	length = address_list.size() * ENCODED_UNICAST_ADDR_SIZE(family());
	BUFFER_PUT_HOST_16(length, buffer);
	for (iter = address_list.begin(); iter != address_list.end(); ++iter) {
	    IPvX& addr = *iter;
	    PUT_ENCODED_UNICAST_ADDR(family(), addr, buffer);
	}
    }
    
    return (pim_send(primary_addr(), IPvX::PIM_ROUTERS(family()),
		     PIM_HELLO, buffer, error_msg));
    
 invalid_addr_family_error:
    XLOG_UNREACHABLE();
    error_msg = c_format("TX %s from %s to %s: "
			 "invalid address family error = %d",
			 PIMTYPE2ASCII(PIM_HELLO),
			 cstring(primary_addr()),
			 cstring(IPvX::PIM_ROUTERS(family())),
			 family());
    XLOG_ERROR("%s", error_msg.c_str());
    return (XORP_ERROR);
    
 buflen_error:
    XLOG_UNREACHABLE();
    error_msg = c_format("TX %s from %s to %s: "
			 "packet cannot fit into sending buffer",
			 PIMTYPE2ASCII(PIM_HELLO),
			 cstring(primary_addr()),
			 cstring(IPvX::PIM_ROUTERS(family())));
    XLOG_ERROR("%s", error_msg.c_str());
    return (XORP_ERROR);
}
