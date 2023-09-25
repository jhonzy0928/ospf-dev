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

#ident "$XORP: xorp/pim/pim_node.cc,v 1.82 2007/02/16 22:46:48 pavlin Exp $"


//
// Protocol Independent Multicast (both PIM-SM and PIM-DM)
// node implementation (common part).
// PIM-SMv2 (draft-ietf-pim-sm-new-*), PIM-DM (new draft pending).
//


#include "pim_module.h"
#include "libxorp/xorp.h"
#include "libxorp/xlog.h"
#include "libxorp/debug.h"
#include "libxorp/ipvx.hh"

#include "fea/mfea_kernel_messages.hh"		// TODO: XXX: yuck!

#include "pim_mre.hh"
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


/**
 * PimNode::PimNode:
 * @family: The address family (%AF_INET or %AF_INET6
 * for IPv4 and IPv6 respectively).
 * @module_id: The module ID (must be either %XORP_MODULE_PIMSM
 * or %XORP_MODULE_PIMDM).
 * TODO: XXX: XORP_MODULE_PIMDM is not implemented yet.
 * @eventloop: The event loop.
 * 
 * PIM node constructor.
 **/
PimNode::PimNode(int family, xorp_module_id module_id,
		 EventLoop& eventloop)
    : ProtoNode<PimVif>(family, module_id, eventloop),
      _pim_mrt(*this),
      _pim_mrib_table(*this),
      _rp_table(*this),
      _pim_scope_zone_table(*this),
      _pim_bsr(*this),
      _is_switch_to_spt_enabled(false),	// XXX: disabled by defailt
      _switch_to_spt_threshold_interval_sec(0),
      _switch_to_spt_threshold_bytes(0),
      _default_ip_tos(IPTOS_PREC_INTERNETCONTROL),
#ifndef QUAGGA_MULTICAST
      _is_log_trace(false)
#else
      _log_flags(0)
#endif	// QUAGGA_MULTICAST
{
    // TODO: XXX: PIMDM not implemented yet
    XLOG_ASSERT(module_id == XORP_MODULE_PIMSM);
    XLOG_ASSERT((module_id == XORP_MODULE_PIMSM)
		|| (module_id == XORP_MODULE_PIMDM));
    if ((module_id != XORP_MODULE_PIMSM) && (module_id != XORP_MODULE_PIMDM)) {
	XLOG_FATAL("Invalid module ID = %d (must be 'XORP_MODULE_PIMSM' = %d "
		   "or 'XORP_MODULE_PIMDM' = %d",
		   module_id, XORP_MODULE_PIMSM, XORP_MODULE_PIMDM);
    }
    
    _pim_register_vif_index = Vif::VIF_INDEX_INVALID;
    _register_source_vif_index = Vif::VIF_INDEX_INVALID;
    
    _buffer_recv = BUFFER_MALLOC(BUF_SIZE_DEFAULT);

    //
    // Set the node status
    //
    ProtoNode<PimVif>::set_node_status(PROC_STARTUP);

    //
    // Set myself as an observer when the node status changes
    //
    set_observer(this);
}

/**
 * PimNode::~PimNode:
 * @: 
 * 
 * PIM node destructor.
 * 
 **/
PimNode::~PimNode()
{
    //
    // Unset myself as an observer when the node status changes
    //
    unset_observer(this);

    stop();

    //
    // XXX: Explicitly clear the PimBsr and RpTable now to avoid
    // cross-referencing of lists that may be deleted prematurely
    // at the end of the PimNode destructor.
    //
    _pim_bsr.clear();
    _rp_table.clear();

    //
    // XXX: explicitly clear the PimMrt table now, because PimMrt may utilize
    // some lists in the PimNode class (e.g., _processing_pim_nbr_list) that
    // may be deleted prematurely at the end of the PimNode destructor
    // (depending on the declaration ordering).
    //
    _pim_mrt.clear();
    
    ProtoNode<PimVif>::set_node_status(PROC_NULL);

    delete_all_vifs();
    
    BUFFER_FREE(_buffer_recv);
}

/**
 * PimNode::start:
 * @: 
 * 
 * Start the PIM protocol.
 * TODO: This function should not start the protocol operation on the
 * interfaces. The interfaces must be activated separately.
 * After the startup operations are completed,
 * PimNode::final_start() is called to complete the job.
 * 
 * Return value: %XORP_OK on success, otherwize %XORP_ERROR.
 **/
int
PimNode::start()
{
    if (! is_enabled())
	return (XORP_OK);

    //
    // Test the service status
    //
    if ((ServiceBase::status() == SERVICE_STARTING)
	|| (ServiceBase::status() == SERVICE_RUNNING)) {
	return (XORP_OK);
    }
    if (ServiceBase::status() != SERVICE_READY) {
	return (XORP_ERROR);
    }

    if (ProtoNode<PimVif>::pending_start() < 0)
	return (XORP_ERROR);

    //
    // Register with the MFEA
    //
    mfea_register_startup();

#ifndef QUAGGA_MULTICAST
    //
    // Register with the RIB
    //
    rib_register_startup();
#endif	// !QUAGGA_MULTICAST

    //
    // Set the node status
    //
    ProtoNode<PimVif>::set_node_status(PROC_STARTUP);

    //
    // Update the node status
    //
    update_status();

    return (XORP_OK);
}

/**
 * PimNode::final_start:
 * @: 
 * 
 * Complete the start-up of the PIM protocol.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::final_start()
{
#if 0	// TODO: XXX: PAVPAVPAV
    if (! is_pending_up())
	return (XORP_ERROR);
#endif

    if (ProtoNode<PimVif>::start() < 0) {
	ProtoNode<PimVif>::stop();
	return (XORP_ERROR);
    }

    // Start the pim_vifs
    start_all_vifs();
    
    // Start the BSR module
    if (_pim_bsr.start() < 0)
	return (XORP_ERROR);

    XLOG_INFO(is_log_info(), "Protocol started");

    return (XORP_OK);
}

/**
 * PimNode::stop:
 * @: 
 * 
 * Gracefully stop the PIM protocol.
 * XXX: The graceful stop will attempt to send Join/Prune, Assert, etc.
 * messages for all multicast routing entries to gracefully clean-up
 * state with neighbors.
 * XXX: After the multicast routing entries cleanup is completed,
 * PimNode::final_stop() is called to complete the job.
 * XXX: This function, unlike start(), will stop the protocol
 * operation on all interfaces.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::stop()
{
    //
    // Test the service status
    //
    if ((ServiceBase::status() == SERVICE_SHUTDOWN)
	|| (ServiceBase::status() == SERVICE_SHUTTING_DOWN)
	|| (ServiceBase::status() == SERVICE_FAILED)) {
	return (XORP_OK);
    }
    if ((ServiceBase::status() != SERVICE_RUNNING)
	&& (ServiceBase::status() != SERVICE_STARTING)
	&& (ServiceBase::status() != SERVICE_PAUSING)
	&& (ServiceBase::status() != SERVICE_PAUSED)
	&& (ServiceBase::status() != SERVICE_RESUMING)) {
	return (XORP_ERROR);
    }

    if (ProtoNode<PimVif>::pending_stop() < 0)
	return (XORP_ERROR);

    //
    // Perform misc. PIM-specific stop operations
    //
    _pim_bsr.stop();
    
    // Stop the vifs
    stop_all_vifs();
    
    //
    // Set the node status
    //
    ProtoNode<PimVif>::set_node_status(PROC_SHUTDOWN);

    //
    // Update the node status
    //
    update_status();

    return (XORP_OK);
}

/**
 * PimNode::final_stop:
 * @: 
 * 
 * Completely stop the PIM protocol.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::final_stop()
{
    if (! (is_up() || is_pending_up() || is_pending_down()))
	return (XORP_ERROR);

    if (ProtoNode<PimVif>::stop() < 0)
	return (XORP_ERROR);

    XLOG_INFO(is_log_info(), "Protocol stopped");

    return (XORP_OK);
}

/**
 * Enable the node operation.
 * 
 * If an unit is not enabled, it cannot be start, or pending-start.
 */
void
PimNode::enable()
{
    ProtoUnit::enable();

    XLOG_INFO(is_log_info(), "Protocol enabled");
}

/**
 * Disable the node operation.
 * 
 * If an unit is disabled, it cannot be start or pending-start.
 * If the unit was runnning, it will be stop first.
 */
void
PimNode::disable()
{
    stop();
    ProtoUnit::disable();

    XLOG_INFO(is_log_info(), "Protocol disabled");
}

void
PimNode::status_change(ServiceBase*  service,
		       ServiceStatus old_status,
		       ServiceStatus new_status)
{
    XLOG_ASSERT(this == service);

    if ((old_status == SERVICE_STARTING)
	&& (new_status == SERVICE_RUNNING)) {
	// The startup process has completed
	if (final_start() < 0) {
	    XLOG_ERROR("Cannot complete the startup process; "
		       "current state is %s",
		       ProtoNode<PimVif>::state_str().c_str());
	    return;
	}
	ProtoNode<PimVif>::set_node_status(PROC_READY);
	return;
    }

    if ((old_status == SERVICE_SHUTTING_DOWN)
	&& (new_status == SERVICE_SHUTDOWN)) {
	// The shutdown process has completed
	final_stop();
	// Set the node status
	ProtoNode<PimVif>::set_node_status(PROC_DONE);
	return;
    }

    //
    // TODO: check if there was an error
    //
}

/**
 * PimNode::add_vif:
 * @vif: Information about the new PimVif to install.
 * @error_msg: The error message (if error).
 * 
 * Install a new PIM vif.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::add_vif(const Vif& vif, string& error_msg)
{
    //
    // Create a new PimVif
    //
    PimVif *pim_vif = new PimVif(*this, vif);
    
    if (ProtoNode<PimVif>::add_vif(pim_vif) != XORP_OK) {
	// Cannot add this new vif
	error_msg = c_format("Cannot add vif %s: internal error",
			     vif.name().c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	
	delete pim_vif;
	return (XORP_ERROR);
    }
    
    // Set the PIM Register vif index if needed
    if (pim_vif->is_pim_register())
	_pim_register_vif_index = pim_vif->vif_index();

    //
    // Resolve all destination prefixes whose next-hop vif name was not
    // resolved earlier (e.g., the vif was unknown).
    //
    _pim_mrib_table.resolve_prefixes_by_vif_name(pim_vif->name(),
						 pim_vif->vif_index());

    //
    // Update and check the primary and domain-wide addresses
    //
    do {
	if (pim_vif->update_primary_and_domain_wide_address(error_msg)
	    == XORP_OK) {
	    break;
	}
	if (pim_vif->addr_ptr() == NULL) {
	    // XXX: don't print an error if the vif has no addresses
	    break;
	}
	if (pim_vif->is_loopback()) {
	    // XXX: don't print an error if this is a loopback interface
	    break;
	}
	XLOG_ERROR("Error updating primary and domain-wide addresses "
		   "for vif %s: %s",
		   pim_vif->name().c_str(), error_msg.c_str());
	return (XORP_ERROR);
    } while (false);

    XLOG_INFO(is_log_info(), "Interface added: %s", pim_vif->str().c_str());
    
    return (XORP_OK);
}

/**
 * PimNode::add_vif:
 * @vif_name: The name of the new vif.
 * @vif_index: The vif index of the new vif.
 * @error_msg: The error message (if error).
 * 
 * Install a new PIM vif. If the vif exists, nothing is installed.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::add_vif(const string& vif_name, uint32_t vif_index, string& error_msg)
{
    PimVif *pim_vif = vif_find_by_vif_index(vif_index);
    
    if ((pim_vif != NULL) && (pim_vif->name() == vif_name)) {
	return (XORP_OK);		// Already have this vif
    }

    //
    // Create a new Vif
    //
    Vif vif(vif_name);
    vif.set_vif_index(vif_index);
    if (add_vif(vif, error_msg) != XORP_OK) {
	return (XORP_ERROR);
    }
    
    return (XORP_OK);
}

/**
 * PimNode::delete_vif:
 * @vif_name: The name of the vif to delete.
 * @error_msg: The error message (if error).
 * 
 * Delete an existing PIM vif.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::delete_vif(const string& vif_name, string& error_msg)
{
    PimVif *pim_vif = vif_find_by_name(vif_name);
    if (pim_vif == NULL) {
	error_msg = c_format("Cannot delete vif %s: no such vif",
			     vif_name.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    if (ProtoNode<PimVif>::delete_vif(pim_vif) != XORP_OK) {
	error_msg = c_format("Cannot delete vif %s: internal error",
			     vif_name.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	delete pim_vif;
	return (XORP_ERROR);
    }
    
    // Reset the PIM Register vif index if needed
    if (_pim_register_vif_index == pim_vif->vif_index())
	_pim_register_vif_index = Vif::VIF_INDEX_INVALID;
    
    if (_register_source_vif_index == pim_vif->vif_index())
	_register_source_vif_index = Vif::VIF_INDEX_INVALID;

    delete pim_vif;
    
    XLOG_INFO(is_log_info(), "Interface deleted: %s", vif_name.c_str());
    
    return (XORP_OK);
}

int
PimNode::set_vif_flags(const string& vif_name,
		       bool is_pim_register, bool is_p2p,
		       bool is_loopback, bool is_multicast,
		       bool is_broadcast, bool is_up, uint32_t mtu,
		       string& error_msg)
{
    bool is_changed = false;
    
    PimVif *pim_vif = vif_find_by_name(vif_name);
    if (pim_vif == NULL) {
	error_msg = c_format("Cannot set flags vif %s: no such vif",
			     vif_name.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    if (pim_vif->is_pim_register() != is_pim_register) {
	pim_vif->set_pim_register(is_pim_register);
	is_changed = true;
    }
    if (pim_vif->is_p2p() != is_p2p) {
	pim_vif->set_p2p(is_p2p);
	is_changed = true;
    }
    if (pim_vif->is_loopback() != is_loopback) {
	pim_vif->set_loopback(is_loopback);
	is_changed = true;
    }
    if (pim_vif->is_multicast_capable() != is_multicast) {
	pim_vif->set_multicast_capable(is_multicast);
	is_changed = true;
    }
    if (pim_vif->is_broadcast_capable() != is_broadcast) {
	pim_vif->set_broadcast_capable(is_broadcast);
	is_changed = true;
    }
    if (pim_vif->is_underlying_vif_up() != is_up) {
	pim_vif->set_underlying_vif_up(is_up);
	is_changed = true;
    }
    if (pim_vif->mtu() != mtu) {
	pim_vif->set_mtu(mtu);
	is_changed = true;
    }
    
    // Set the PIM Register vif index if needed
    if (pim_vif->is_pim_register())
	_pim_register_vif_index = pim_vif->vif_index();
    
    if (is_changed)
	XLOG_INFO(is_log_info(), "Interface flags changed: %s",
		  pim_vif->str().c_str());
    
    return (XORP_OK);
}

int
PimNode::add_vif_addr(const string& vif_name,
		      const IPvX& addr,
		      const IPvXNet& subnet_addr,
		      const IPvX& broadcast_addr,
		      const IPvX& peer_addr,
		      bool& should_send_hello,
		      string &error_msg)
{
    PimVif *pim_vif = vif_find_by_name(vif_name);

    should_send_hello = false;

    if (pim_vif == NULL) {
	error_msg = c_format("Cannot add address on vif %s: no such vif",
			     vif_name.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    const VifAddr vif_addr(addr, subnet_addr, broadcast_addr, peer_addr);
    
    //
    // Check the arguments
    //
    if (! addr.is_unicast()) {
	error_msg = c_format("Cannot add address on vif %s: "
			     "invalid unicast address: %s",
			     vif_name.c_str(), addr.str().c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    if ((addr.af() != family())
	|| (subnet_addr.af() != family())
	|| (broadcast_addr.af() != family())
	|| (peer_addr.af() != family())) {
	error_msg = c_format("Cannot add address on vif %s: "
			     "invalid address family: %s ",
			     vif_name.c_str(), vif_addr.str().c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    VifAddr* node_vif_addr = pim_vif->find_address(addr);

    if ((node_vif_addr != NULL) && (*node_vif_addr == vif_addr))
	return (XORP_OK);		// Already have this address

    //
    // Spec:
    // "Before an interface goes down or changes primary IP address, a Hello
    // message with a zero HoldTime should be sent immediately (with the old IP
    // address if the IP address changed)."
    //
    // However, by adding or updating an existing address we cannot
    // change a valid primary address, hence we do nothing here.
    //

    if (node_vif_addr != NULL) {
	// Update the address
	XLOG_INFO(is_log_info(), "Updated existing address on interface %s: "
		  "old is %s new is %s",
		  pim_vif->name().c_str(), node_vif_addr->str().c_str(),
		  vif_addr.str().c_str());
	*node_vif_addr = vif_addr;
    } else {
	// Add a new address
	pim_vif->add_address(vif_addr);
	
	XLOG_INFO(is_log_info(), "Added new address to interface %s: %s",
		  pim_vif->name().c_str(), vif_addr.str().c_str());
    }

    //
    // Update and check the primary and domain-wide addresses
    //
    do {
	if (pim_vif->update_primary_and_domain_wide_address(error_msg)
	    == XORP_OK) {
	    break;
	}
	if (! (pim_vif->is_up() || pim_vif->is_pending_up())) {
	    // XXX: print an error only if the interface is UP or PENDING_UP
	    break;
	}
	if (pim_vif->is_loopback()) {
	    // XXX: don't print an error if this is a loopback interface
	    break;
	}
	XLOG_ERROR("Error updating primary and domain-wide addresses "
		   "for vif %s: %s",
		   pim_vif->name().c_str(), error_msg.c_str());
	return (XORP_ERROR);
    } while (false);

    //
    // Spec:
    // "If an interface changes one of its secondary IP addresses,
    // a Hello message with an updated Address_List option and a
    // non-zero HoldTime should be sent immediately."
    //
    if (pim_vif->is_up()) {
	// pim_vif->pim_hello_send();
	should_send_hello = true;
    }

    // Schedule the dependency-tracking tasks
    pim_mrt().add_task_my_ip_address(pim_vif->vif_index());
    pim_mrt().add_task_my_ip_subnet_address(pim_vif->vif_index());

    //
    // Inform the BSR about the change
    //
    pim_bsr().add_vif_addr(pim_vif->vif_index(), addr);
    
    return (XORP_OK);
}

int
PimNode::delete_vif_addr(const string& vif_name,
			 const IPvX& addr,
			 bool& should_send_hello,
			 string& error_msg)
{
    PimVif *pim_vif = vif_find_by_name(vif_name);

    should_send_hello = false;

    if (pim_vif == NULL) {
	error_msg = c_format("Cannot delete address on vif %s: no such vif",
			     vif_name.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    const VifAddr *tmp_vif_addr = pim_vif->find_address(addr);
    if (tmp_vif_addr == NULL) {
	error_msg = c_format("Cannot delete address on vif %s: "
			     "invalid address %s",
			     vif_name.c_str(), addr.str().c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }

    VifAddr vif_addr = *tmp_vif_addr;	// Get a copy

    //
    // Spec:
    // "Before an interface goes down or changes primary IP address, a Hello
    // message with a zero HoldTime should be sent immediately (with the old IP
    // address if the IP address changed)."
    //
    if (pim_vif->is_up()) {
	if (pim_vif->primary_addr() == addr) {
	    pim_vif->pim_hello_stop();
	}
    }

    if (pim_vif->delete_address(addr) != XORP_OK) {
	XLOG_UNREACHABLE();
	return (XORP_ERROR);
    }
    
    XLOG_INFO(is_log_info(), "Deleted address on interface %s: %s",
	      pim_vif->name().c_str(), vif_addr.str().c_str());

    //
    // Update the primary and domain-wide addresses.
    // If the vif has no more primary or a domain-wide address, then stop it.
    //
    do {
	if (pim_vif->update_primary_and_domain_wide_address(error_msg)
	    == XORP_OK) {
	    break;
	}
	if (! (pim_vif->is_up() || pim_vif->is_pending_up())) {
	    // XXX: don't do anything if the interface is not UP or PENDING_UP
	    break;
	}
	if (pim_vif->is_loopback()) {
	    // XXX: don't do anything if this is a loopback interface
	    break;
	}
	XLOG_ERROR("Error updating primary and domain-wide addresses "
		   "for vif %s: %s",
		   pim_vif->name().c_str(), error_msg.c_str());
	pim_vif->stop(error_msg);
    } while (false);

    //
    // Spec:
    // "If an interface changes one of its secondary IP addresses,
    // a Hello message with an updated Address_List option and a
    // non-zero HoldTime should be sent immediately."
    //
    if (pim_vif->is_up()) {
	// pim_vif->pim_hello_send();
	should_send_hello = true;
    }
    
    // Schedule the dependency-tracking tasks
    pim_mrt().add_task_my_ip_address(pim_vif->vif_index());
    pim_mrt().add_task_my_ip_subnet_address(pim_vif->vif_index());
    
    //
    // Inform the BSR about the change
    //
    pim_bsr().delete_vif_addr(pim_vif->vif_index(), addr);

    return (XORP_OK);
}

/**
 * PimNode::enable_vif:
 * @vif_name: The name of the vif to enable.
 * @error_msg: The error message (if error).
 * 
 * Enable an existing PIM vif.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::enable_vif(const string& vif_name, string& error_msg)
{
    PimVif *pim_vif = vif_find_by_name(vif_name);
    if (pim_vif == NULL) {
	error_msg = c_format("Cannot enable vif %s: no such vif",
			     vif_name.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    pim_vif->enable();
    
    return (XORP_OK);
}

/**
 * PimNode::disable_vif:
 * @vif_name: The name of the vif to disable.
 * @error_msg: The error message (if error).
 * 
 * Disable an existing PIM vif.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::disable_vif(const string& vif_name, string& error_msg)
{
    PimVif *pim_vif = vif_find_by_name(vif_name);
    if (pim_vif == NULL) {
	error_msg = c_format("Cannot disable vif %s: no such vif",
			     vif_name.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    pim_vif->disable();
    
    return (XORP_OK);
}

/**
 * PimNode::start_vif:
 * @vif_name: The name of the vif to start.
 * @error_msg: The error message (if error).
 * 
 * Start an existing PIM vif.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::start_vif(const string& vif_name, string& error_msg)
{
    PimVif *pim_vif = vif_find_by_name(vif_name);
    if (pim_vif == NULL) {
	error_msg = c_format("Cannot start vif %s: no such vif",
			     vif_name.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    if (pim_vif->start(error_msg) != XORP_OK) {
	error_msg = c_format("Cannot start vif %s: %s",
			     vif_name.c_str(), error_msg.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    return (XORP_OK);
}

/**
 * PimNode::stop_vif:
 * @vif_name: The name of the vif to stop.
 * @error_msg: The error message (if error).
 * 
 * Stop an existing PIM vif.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::stop_vif(const string& vif_name, string& error_msg)
{
    PimVif *pim_vif = vif_find_by_name(vif_name);
    if (pim_vif == NULL) {
	error_msg = c_format("Cannot stop vif %s: no such vif",
			     vif_name.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    if (pim_vif->stop(error_msg) != XORP_OK) {
	error_msg = c_format("Cannot stop vif %s: %s",
			     vif_name.c_str(), error_msg.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    return (XORP_OK);
}

/**
 * PimNode::start_all_vifs:
 * @: 
 * 
 * Start PIM on all enabled interfaces.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::start_all_vifs()
{
    vector<PimVif *>::iterator iter;
    string error_msg;
    int ret_value = XORP_OK;
    
    for (iter = proto_vifs().begin(); iter != proto_vifs().end(); ++iter) {
	PimVif *pim_vif = (*iter);
	if (pim_vif == NULL)
	    continue;
	if (start_vif(pim_vif->name(), error_msg) != XORP_OK)
	    ret_value = XORP_ERROR;
    }
    
    return (ret_value);
}

/**
 * PimNode::stop_all_vifs:
 * @: 
 * 
 * Stop PIM on all interfaces it was running on.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::stop_all_vifs()
{
    vector<PimVif *>::iterator iter;
    string error_msg;
    int ret_value = XORP_OK;
    
    for (iter = proto_vifs().begin(); iter != proto_vifs().end(); ++iter) {
	PimVif *pim_vif = (*iter);
	if (pim_vif == NULL)
	    continue;
	if (stop_vif(pim_vif->name(), error_msg) != XORP_OK)
	    ret_value = XORP_ERROR;
    }
    
    return (ret_value);
}

/**
 * PimNode::enable_all_vifs:
 * @: 
 * 
 * Enable PIM on all interfaces.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::enable_all_vifs()
{
    vector<PimVif *>::iterator iter;
    string error_msg;
    int ret_value = XORP_OK;
    
    for (iter = proto_vifs().begin(); iter != proto_vifs().end(); ++iter) {
	PimVif *pim_vif = (*iter);
	if (pim_vif == NULL)
	    continue;
	if (enable_vif(pim_vif->name(), error_msg) != XORP_OK)
	    ret_value = XORP_ERROR;
    }
    
    return (ret_value);
}

/**
 * PimNode::disable_all_vifs:
 * @: 
 * 
 * Disable PIM on all interfaces. All running interfaces are stopped first.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::disable_all_vifs()
{
    vector<PimVif *>::iterator iter;
    string error_msg;
    int ret_value = XORP_OK;
    
    for (iter = proto_vifs().begin(); iter != proto_vifs().end(); ++iter) {
	PimVif *pim_vif = (*iter);
	if (pim_vif == NULL)
	    continue;
	if (disable_vif(pim_vif->name(), error_msg) != XORP_OK)
	    ret_value = XORP_ERROR;
    }
    
    return (ret_value);
}

/**
 * PimNode::delete_all_vifs:
 * @: 
 * 
 * Delete all PIM vifs.
 **/
void
PimNode::delete_all_vifs()
{
    list<string> vif_names;
    vector<PimVif *>::iterator iter;

    //
    // Create the list of all vif names to delete
    //
    for (iter = proto_vifs().begin(); iter != proto_vifs().end(); ++iter) {
	PimVif *pim_vif = (*iter);
	if (pim_vif != NULL) {
	    string vif_name = pim_vif->name();
	    vif_names.push_back(pim_vif->name());
	}
    }

    //
    // Delete all vifs
    //
    list<string>::iterator vif_names_iter;
    for (vif_names_iter = vif_names.begin();
	 vif_names_iter != vif_names.end();
	 ++vif_names_iter) {
	const string& vif_name = *vif_names_iter;
	string error_msg;
	if (delete_vif(vif_name, error_msg) != XORP_OK) {
	    error_msg = c_format("Cannot delete vif %s: internal error",
				 vif_name.c_str());
	    XLOG_ERROR("%s", error_msg.c_str());
	}
    }
}

/**
 * A method called when a vif has completed its shutdown.
 * 
 * @param vif_name the name of the vif that has completed its shutdown.
 */
void
PimNode::vif_shutdown_completed(const string& vif_name)
{
    vector<PimVif *>::iterator iter;

    //
    // If all vifs have completed the shutdown, then de-register with
    // the RIB and the MFEA.
    //
    for (iter = proto_vifs().begin(); iter != proto_vifs().end(); ++iter) {
	PimVif *pim_vif = *iter;
	if (pim_vif == NULL)
	    continue;
	if (! pim_vif->is_down())
	    return;
    }

    if (ServiceBase::status() == SERVICE_SHUTTING_DOWN) {
#ifndef QUAGGA_MULTICAST
	//
	// De-register with the RIB
	//
	rib_register_shutdown();
#endif	// !QUAGGA_MULTICAST

	//
	// De-register with the MFEA
	//
	mfea_register_shutdown();
    }

    UNUSED(vif_name);
}

/**
 * PimNode::proto_recv:
 * @src_module_instance_name: The module instance name of the module-origin
 * of the message.
 * @src_module_id: The #xorp_module_id of the module-origin of the message.
 * @vif_index: The vif index of the interface used to receive this message.
 * @src: The source address of the message.
 * @dst: The destination address of the message.
 * @ip_ttl: The IP TTL of the message. If it has a negative value,
 * it should be ignored.
 * @ip_tos: The IP TOS of the message. If it has a negative value,
 * it should be ignored.
 * @is_router_alert: If true, the Router Alert IP option for the IP
 * packet of the incoming message was set.
 * @rcvbuf: The data buffer with the received message.
 * @rcvlen: The data length in @rcvbuf.
 * @error_msg: The error message (if error).
 * 
 * Receive a protocol message from the kernel.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::proto_recv(const string&	, // src_module_instance_name,
		    xorp_module_id src_module_id,
		    uint32_t vif_index,
		    const IPvX& src, const IPvX& dst,
		    int ip_ttl, int ip_tos, bool is_router_alert,
		    const uint8_t *rcvbuf, size_t rcvlen,
		    string& error_msg)
{
    PimVif *pim_vif = NULL;
    int ret_value = XORP_ERROR;
    
    debug_msg("Received message from %s to %s on vif_index %d: "
	      "ip_ttl = %d ip_tos = %#x router_alert = %d rcvlen = %u\n",
	      cstring(src), cstring(dst), vif_index,
	      ip_ttl, ip_tos, is_router_alert, XORP_UINT_CAST(rcvlen));
    
    //
    // Check whether the node is up.
    //
    if (! is_up()) {
	error_msg = c_format("PIM node is not UP");
	return (XORP_ERROR);
    }
    
    //
    // Find the vif for that packet
    //
    pim_vif = vif_find_by_vif_index(vif_index);
    if (pim_vif == NULL) {
	error_msg = c_format("Cannot find vif with vif_index = %u", vif_index);
	return (XORP_ERROR);
    }
    
    // Copy the data to the receiving #buffer_t
    BUFFER_RESET(_buffer_recv);
    BUFFER_PUT_DATA(rcvbuf, _buffer_recv, rcvlen);
    
    // Process the data by the vif
    ret_value = pim_vif->pim_recv(src, dst, ip_ttl, ip_tos,
				  is_router_alert,
				  _buffer_recv);
    
    return (ret_value);
    
 buflen_error:
    XLOG_UNREACHABLE();
    return (XORP_ERROR);
    
    UNUSED(src_module_id);
}

/**
 * PimNode::pim_send:
 * @vif_index: The vif index of the interface to send this message.
 * @src: The source address of the message.
 * @dst: The destination address of the message.
 * @ip_ttl: The IP TTL of the message. If it has a negative value,
 * the TTL will be set by the lower layers.
 * @ip_tos: The IP TOS of the message. If it has a negative value,
 * the TOS will be set by the lower layers.
 * @is_router_alert: If true, set the Router Alert IP option for the IP
 * packet of the outgoung message.
 * @buffer: The #buffer_t data buffer with the message to send.
 * @error_msg: The error message (if error).
 * 
 * Send a PIM message.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::pim_send(uint32_t vif_index,
		  const IPvX& src, const IPvX& dst,
		  int ip_ttl, int ip_tos, bool is_router_alert,
		  buffer_t *buffer, string& error_msg)
{
    if (! (is_up() || is_pending_down())) {
	error_msg = c_format("MLD/IGMP node is not UP");
	return (XORP_ERROR);
    }
    
    // TODO: the target name of the MFEA must be configurable.
    if (proto_send(xorp_module_name(family(), XORP_MODULE_MFEA),
		   XORP_MODULE_MFEA,
		   vif_index, src, dst,
		   ip_ttl, ip_tos, is_router_alert,
		   BUFFER_DATA_HEAD(buffer),
		   BUFFER_DATA_SIZE(buffer),
		   error_msg) < 0) {
	return (XORP_ERROR);
    }
    
    return (XORP_OK);
}

/**
 * PimNode::signal_message_recv:
 * @src_module_instance_name: The module instance name of the module-origin
 * of the message.
 * @src_module_id: The #xorp_module_id of the module-origin of the message.
 * @message_type: The message type of the kernel signal.
 * At this moment, one of the following:
 * %MFEA_KERNEL_MESSAGE_NOCACHE (if a cache-miss in the kernel)
 * %MFEA_KERNEL_MESSAGE_WRONGVIF (multicast packet received on wrong vif)
 * %MFEA_KERNEL_MESSAGE_WHOLEPKT (typically, a packet that should be
 * encapsulated as a PIM-Register).
 * @vif_index: The vif index of the related interface (message-specific
 * relation).
 * @src: The source address in the message.
 * @dst: The destination address in the message.
 * @rcvbuf: The data buffer with the additional information in the message.
 * @rcvlen: The data length in @rcvbuf.
 * 
 * Receive a signal from the kernel (e.g., NOCACHE, WRONGVIF, WHOLEPKT).
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::signal_message_recv(const string& src_module_instance_name,
			     xorp_module_id src_module_id,
			     int message_type,
			     uint32_t vif_index,
			     const IPvX& src,
			     const IPvX& dst,
			     const uint8_t *rcvbuf,
			     size_t rcvlen)
{
    int ret_value = XORP_ERROR;
    
    do {
	if (message_type == MFEA_KERNEL_MESSAGE_NOCACHE) {
	    ret_value = pim_mrt().signal_message_nocache_recv(
		src_module_instance_name,
		src_module_id,
		vif_index,
		src,
		dst);
	    break;
	}
	if (message_type == MFEA_KERNEL_MESSAGE_WRONGVIF) {
	    ret_value = pim_mrt().signal_message_wrongvif_recv(
		src_module_instance_name,
		src_module_id,
		vif_index,
		src,
		dst);
	    break;
	}
	if (message_type == MFEA_KERNEL_MESSAGE_WHOLEPKT) {
	    ret_value = pim_mrt().signal_message_wholepkt_recv(
		src_module_instance_name,
		src_module_id,
		vif_index,
		src,
		dst,
		rcvbuf,
		rcvlen);
	    break;
	}
	
	XLOG_WARNING("RX unknown signal from %s: "
		     "vif_index = %d src = %s dst = %s message_type = %d",
		     src_module_instance_name.c_str(),
		     vif_index,
		     cstring(src), cstring(dst),
		     message_type);
	return (XORP_ERROR);
    } while (false);
    
    return (ret_value);
}

/**
 * PimNode::add_membership:
 * @vif_index: The vif_index of the interface to add membership.
 * @source: The source address to add membership for
 * (IPvX::ZERO() for IGMPv1,2 and MLDv1).
 * @group: The group address to add membership for.
 * 
 * Add multicast membership on vif with vif_index of @vif_index for
 * source address of @source and group address of @group.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::add_membership(uint32_t vif_index, const IPvX& source,
			const IPvX& group)
{
    uint32_t lookup_flags = 0;
    uint32_t create_flags = 0;
    PimVif *pim_vif = NULL;
    bool is_ssm = false;

    if (source != IPvX::ZERO(family()))
	is_ssm = true;
    
    //
    // Check the arguments: the vif, source and group addresses.
    //
    pim_vif = vif_find_by_vif_index(vif_index);
    if (pim_vif == NULL)
	return (XORP_ERROR);
    if (! (pim_vif->is_up()
	   || pim_vif->is_pending_up())) {
	return (XORP_ERROR);	// The vif is (going) DOWN
    }
    
    if (source != IPvX::ZERO(family())) {
	if (! source.is_unicast())
	    return (XORP_ERROR);
    }
    if (! group.is_multicast())
	return (XORP_ERROR);
    
    if (group.is_linklocal_multicast()
	|| group.is_interfacelocal_multicast()) {
	// XXX: don't route link or interface-local groups
	return (XORP_OK);
    }
    
    XLOG_TRACE(is_log_trace(), "Add membership for (%s, %s) on vif %s",
	       cstring(source), cstring(group), pim_vif->name().c_str());
    
    //
    // Setup the MRE lookup and create flags
    //
    if (is_ssm)
	lookup_flags |= PIM_MRE_SG;
    else
	lookup_flags |= PIM_MRE_WC;
    create_flags = lookup_flags;
    
    PimMre *pim_mre = pim_mrt().pim_mre_find(source, group, lookup_flags,
					     create_flags);
    
    if (pim_mre == NULL)
	return (XORP_ERROR);
    
    //
    // Modify to the local membership state
    //
    if (is_ssm) {
	//
	// (S, G) Join
	//
	// XXX: If the source was excluded, then don't exclude it anymore.
	// Otherwise, include the source.
	//
	XLOG_ASSERT(pim_mre->is_sg());
	if (pim_mre->local_receiver_exclude_sg().test(vif_index)) {
	    pim_mre->set_local_receiver_exclude(vif_index, false);
	} else {
	    pim_mre->set_local_receiver_include(vif_index, true);
	}
    } else {
	//
	// (*,G) Join
	//
	pim_mre->set_local_receiver_include(vif_index, true);
    }
    
    return (XORP_OK);
}

/**
 * PimNode::delete_membership:
 * @vif_index: The vif_index of the interface to delete membership.
 * @source: The source address to delete membership for
 * (IPvX::ZERO() for IGMPv1,2 and MLDv1).
 * @group: The group address to delete membership for.
 * 
 * Delete multicast membership on vif with vif_index of @vif_index for
 * source address of @source and group address of @group.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimNode::delete_membership(uint32_t vif_index, const IPvX& source,
			   const IPvX& group)
{
    uint32_t lookup_flags = 0;
    uint32_t create_flags = 0;
    PimVif *pim_vif = NULL;
    bool is_ssm = false;

    if (source != IPvX::ZERO(family()))
	is_ssm = true;
    
    //
    // Check the arguments: the vif, source and group addresses.
    //
    pim_vif = vif_find_by_vif_index(vif_index);
    if (pim_vif == NULL)
	return (XORP_ERROR);
    if (! (pim_vif->is_up()
	   || pim_vif->is_pending_down()
	   || pim_vif->is_pending_up())) {
	return (XORP_ERROR);	// The vif is DOWN
    }
    
    if (source != IPvX::ZERO(family())) {
	if (! source.is_unicast())
	    return (XORP_ERROR);
    }
    if (! group.is_multicast())
	return (XORP_ERROR);
    
    if (group.is_linklocal_multicast()
	|| group.is_interfacelocal_multicast()) {
	// XXX: don't route link or interface-local groups
	return (XORP_OK);
    }
    
    XLOG_TRACE(is_log_trace(), "Delete membership for (%s, %s) on vif %s",
	       cstring(source), cstring(group), pim_vif->name().c_str());
    
    //
    // Setup the MRE lookup and create flags
    //
    if (is_ssm) {
	lookup_flags |= PIM_MRE_SG;
	create_flags = lookup_flags;	// XXX: create an entry for (S,G) Prune
    } else {
	lookup_flags |= PIM_MRE_WC;
	create_flags = 0;
    }
    
    PimMre *pim_mre = pim_mrt().pim_mre_find(source, group, lookup_flags,
					     create_flags);
    
    if (pim_mre == NULL)
	return (XORP_ERROR);
    
    //
    // Modify the local membership state
    //
    if (is_ssm) {
	//
	// (S, G) Prune
	//
	// XXX: If the source was included, then don't include it anymore.
	// Otherwise, exclude the source.
	//
	XLOG_ASSERT(pim_mre->is_sg());
	if (pim_mre->local_receiver_include_sg().test(vif_index)) {
	    pim_mre->set_local_receiver_include(vif_index, false);
	} else {
	    pim_mre->set_local_receiver_exclude(vif_index, true);
	}
    } else {
	//
	// (*,G) Prune
	//
	pim_mre->set_local_receiver_include(vif_index, false);
    }
    
    return (XORP_OK);
}

/**
 * PimNode::is_directly_connected:
 * @pim_vif: The virtual interface to test against.
 * @ipaddr_test: The address to test.
 * 
 * Note that the virtual interface the address is directly connected to
 * must be UP.
 * 
 * Return value: True if @ipaddr_test is directly connected to @pim_vif,
 * otherwise false.
 **/
bool
PimNode::is_directly_connected(const PimVif& pim_vif,
			       const IPvX& ipaddr_test) const
{
    if (! pim_vif.is_up())
	return (false);

    //
    // Test the alternative subnets
    //
    list<IPvXNet>::const_iterator iter;
    for (iter = pim_vif.alternative_subnet_list().begin();
	 iter != pim_vif.alternative_subnet_list().end();
	 ++iter) {
	const IPvXNet& ipvxnet = *iter;
	if (ipvxnet.contains(ipaddr_test))
	    return true;
    }

    //
    // Test the same subnet addresses, or the P2P addresses
    //
    if (pim_vif.is_same_subnet(ipaddr_test)
	|| pim_vif.is_same_p2p(ipaddr_test))
	return true;

    const Mrib *mrib = _pim_mrib_table.find(ipaddr_test);
    if (mrib != NULL) {
	if (mrib->next_hop_vif_index() == pim_vif.vif_index()
	    && mrib->next_hop_router_addr() == ipaddr_test)
	    return true;
    }

    return false;
}

/**
 * PimNode::vif_find_pim_register:
 * @: 
 * 
 * Return the PIM Register virtual interface.
 * 
 * Return value: The PIM Register virtual interface if exists, otherwise NULL.
 **/
PimVif *
PimNode::vif_find_pim_register() const
{
    return (vif_find_by_vif_index(pim_register_vif_index()));
}

/**
 * PimNode::vif_find_register_source:
 * @:
 *
 * Return the PIM Register source interface.
 *
 * Return value: The PIM Register source interface if exists, otherwise NULL.
 **/
PimVif *
PimNode::vif_find_register_source() const
{
    return (vif_find_by_vif_index(register_source_vif_index()));
}

/**
 * PimNode::set_pim_vifs_dr:
 * @vif_index: The vif index to set/reset the DR flag.
 * @v: true if set the DR flag, otherwise false.
 * 
 * Set/reset the DR flag for vif index @vif_index.
 **/
void
PimNode::set_pim_vifs_dr(uint32_t vif_index, bool v)
{
    if (vif_index >= pim_vifs_dr().size())
	return;			// TODO: return an error instead?
    
    if (pim_vifs_dr().test(vif_index) == v)
	return;			// Nothing changed
    
    if (v)
	pim_vifs_dr().set(vif_index);
    else
	pim_vifs_dr().reset(vif_index);
    
    pim_mrt().add_task_i_am_dr(vif_index);
}

/**
 * PimNode::pim_vif_rpf_find:
 * @dst_addr: The address of the destination to search for.
 * 
 * Find the RPF virtual interface for a destination address.
 * 
 * Return value: The #PimVif entry for the RPF virtual interface to @dst_addr
 * if found, otherwise %NULL.
 **/
PimVif *
PimNode::pim_vif_rpf_find(const IPvX& dst_addr)
{
    Mrib *mrib;
    PimVif *pim_vif;

    //
    // Do the MRIB lookup
    //
    mrib = pim_mrib_table().find(dst_addr);
    if (mrib == NULL)
	return (NULL);

    //
    // Find the vif toward the destination address
    //
    pim_vif = vif_find_by_vif_index(mrib->next_hop_vif_index());

    return (pim_vif);
}

/**
 * PimNode::pim_nbr_rpf_find:
 * @dst_addr: The address of the destination to search for.
 * 
 * Find the RPF PIM neighbor for a destination address.
 * 
 * Return value: The #PimNbr entry for the RPF neighbor to @dst_addr if found,
 * otherwise %NULL.
 **/
PimNbr *
PimNode::pim_nbr_rpf_find(const IPvX& dst_addr)
{
    Mrib *mrib;
    
    //
    // Do the MRIB lookup
    //
    mrib = pim_mrib_table().find(dst_addr);
    
    //
    // Seach for the RPF neighbor router
    //
    return (pim_nbr_rpf_find(dst_addr, mrib));
}

/**
 * PimNode::pim_nbr_rpf_find:
 * @dst_addr: The address of the destination to search for.
 * @mrib: The MRIB information that was lookup already.
 * 
 * Find the RPF PIM neighbor for a destination address by using the
 * information in a pre-lookup MRIB.
 * 
 * Return value: The #PimNbr entry for the RPF neighbor to @dst_addr if found,
 * otherwise %NULL.
 **/
PimNbr *
PimNode::pim_nbr_rpf_find(const IPvX& dst_addr, const Mrib *mrib)
{
    bool is_same_subnet = false;
    PimNbr *pim_nbr = NULL;
    
    //
    // Check the MRIB information
    //
    if (mrib == NULL)
	return (NULL);
    
    //
    // Find the vif toward the destination address
    //
    PimVif *pim_vif = vif_find_by_vif_index(mrib->next_hop_vif_index());

    //
    // Test if the destination is on the same subnet.
    //
    // Note that we need to capture the case if the next-hop router
    // address toward a destination on the same subnet is set to one
    // of the addresses of the interface for that subnet.
    //
    do {
	if (mrib->next_hop_router_addr() == IPvX::ZERO(family())) {
	    is_same_subnet = true;
	    break;
	}
	if ((pim_vif != NULL)
	    && pim_vif->is_my_addr(mrib->next_hop_router_addr())) {
	    is_same_subnet = true;
	    break;
	}
	break;
    } while (false);

    //
    // Search for the neighbor router
    //
    if (is_same_subnet) {
	// A destination on the same subnet
	if (pim_vif != NULL) {
	    pim_nbr = pim_vif->pim_nbr_find(dst_addr);
	} else {
	    //
	    // TODO: XXX: try to remove all calls to pim_nbr_find_global().
	    // The reason we don't want to search for a neighbor across
	    // all network interfaces only by the neighbor's IP address is
	    // because in case of IPv6 the link-local addresses are unique
	    // only per subnet. In other words, there could be more than one
	    // neighbor routers with the same link-local address.
	    // To get rid of PimNode::pim_nbr_find_global(), we have to make
	    // sure that all valid MRIB entries have a valid next-hop vif
	    // index.
	    //
	    pim_nbr = pim_nbr_find_global(dst_addr);
	}
    } else {
	// Not a destination on the same subnet
	if (pim_vif != NULL)
	    pim_nbr = pim_vif->pim_nbr_find(mrib->next_hop_router_addr());
    }

    return (pim_nbr);
}

/**
 * PimNode::pim_nbr_find_global:
 * @nbr_addr: The address of the neighbor to search for.
 * 
 * Find a PIM neighbor by its address.
 * 
 * Note: this method should be used in very limited cases, because
 * in case of IPv6 a neighbor's IP address may not be unique within
 * the PIM neighbor database due to scope issues.
 * 
 * Return value: The #PimNbr entry for the neighbor if found, otherwise %NULL.
 **/
PimNbr *
PimNode::pim_nbr_find_global(const IPvX& nbr_addr)
{
    for (uint32_t i = 0; i < maxvifs(); i++) {
	PimVif *pim_vif = vif_find_by_vif_index(i);
	if (pim_vif == NULL)
	    continue;
	// Exclude the PIM Register vif (as a safe-guard)
	if (pim_vif->is_pim_register())
	    continue;
	PimNbr *pim_nbr = pim_vif->pim_nbr_find(nbr_addr);
	if (pim_nbr != NULL)
	    return (pim_nbr);
    }
    
    return (NULL);
}

//
// Add the PimMre to the dummy PimNbr with primary address of IPvX::ZERO()
//
void
PimNode::add_pim_mre_no_pim_nbr(PimMre *pim_mre)
{
    IPvX ipvx_zero(IPvX::ZERO(family()));
    PimNbr *pim_nbr = NULL;
    
    // Find the dummy PimNbr with primary address of IPvX::ZERO()
    list<PimNbr *>::iterator iter;
    for (iter = processing_pim_nbr_list().begin();
	 iter != processing_pim_nbr_list().end();
	 ++iter) {
	pim_nbr = *iter;
	if (pim_nbr->primary_addr() == ipvx_zero)
	    break;
	else
	    pim_nbr = NULL;
    }
    
    if (pim_nbr == NULL) {
	// Find the first vif. Note that the PIM Register vif is excluded.
	PimVif *pim_vif = NULL;
	for (uint32_t i = 0; i < maxvifs(); i++) {
	    pim_vif = vif_find_by_vif_index(i);
	    if (pim_vif == NULL)
		continue;
	    if (pim_vif->is_pim_register())
		continue;
	    break;
	}
	XLOG_ASSERT(pim_vif != NULL);
	pim_nbr = new PimNbr(*pim_vif, ipvx_zero, PIM_VERSION_DEFAULT);
	processing_pim_nbr_list().push_back(pim_nbr);
    }
    XLOG_ASSERT(pim_nbr != NULL);
    
    pim_nbr->add_pim_mre(pim_mre);
}

//
// Delete the PimMre from the dummy PimNbr with primary address of IPvX::ZERO()
//
void
PimNode::delete_pim_mre_no_pim_nbr(PimMre *pim_mre)
{
    IPvX ipvx_zero(IPvX::ZERO(family()));
    PimNbr *pim_nbr = NULL;
    
    // Find the dummy PimNbr with primary address of IPvX::ZERO()
    list<PimNbr *>::iterator iter;
    for (iter = processing_pim_nbr_list().begin();
	 iter != processing_pim_nbr_list().end();
	 ++iter) {
	pim_nbr = *iter;
	if (pim_nbr->primary_addr() == ipvx_zero)
	    break;
	else
	    pim_nbr = NULL;
    }
    
    if (pim_nbr != NULL)
	pim_nbr->delete_pim_mre(pim_mre);
}

//
// Prepare all PimNbr entries with neighbor address of @pim_nbr_add to
// process their (*,*,RP) PimMre entries.
//
void
PimNode::init_processing_pim_mre_rp(uint32_t vif_index,
				    const IPvX& pim_nbr_addr)
{
    do {
	if (vif_index != Vif::VIF_INDEX_INVALID) {
	    PimVif *pim_vif = vif_find_by_vif_index(vif_index);
	    if (pim_vif == NULL)
		break;
	    PimNbr *pim_nbr = pim_vif->pim_nbr_find(pim_nbr_addr);
	    if (pim_nbr == NULL)
		break;
	    pim_nbr->init_processing_pim_mre_rp();
	    return;
	}
    } while (false);
    
    list<PimNbr *>::iterator iter;
    for (iter = processing_pim_nbr_list().begin();
	 iter != processing_pim_nbr_list().end();
	 ++iter) {
	PimNbr *pim_nbr = *iter;
	if (pim_nbr->primary_addr() == pim_nbr_addr)
	    pim_nbr->init_processing_pim_mre_rp();
    }
}

//
// Prepare all PimNbr entries with neighbor address of @pim_nbr_add to
// process their (*,G) PimMre entries.
//
void
PimNode::init_processing_pim_mre_wc(uint32_t vif_index,
				    const IPvX& pim_nbr_addr)
{
    do {
	if (vif_index != Vif::VIF_INDEX_INVALID) {
	    PimVif *pim_vif = vif_find_by_vif_index(vif_index);
	    if (pim_vif == NULL)
		break;
	    PimNbr *pim_nbr = pim_vif->pim_nbr_find(pim_nbr_addr);
	    if (pim_nbr == NULL)
		break;
	    pim_nbr->init_processing_pim_mre_wc();
	    return;
	}
    } while (false);
    
    list<PimNbr *>::iterator iter;
    for (iter = processing_pim_nbr_list().begin();
	 iter != processing_pim_nbr_list().end();
	 ++iter) {
	PimNbr *pim_nbr = *iter;
	if (pim_nbr->primary_addr() == pim_nbr_addr)
	    pim_nbr->init_processing_pim_mre_wc();
    }
}

//
// Prepare all PimNbr entries with neighbor address of @pim_nbr_add to
// process their (S,G) PimMre entries.
//
void
PimNode::init_processing_pim_mre_sg(uint32_t vif_index,
				    const IPvX& pim_nbr_addr)
{
    do {
	if (vif_index != Vif::VIF_INDEX_INVALID) {
	    PimVif *pim_vif = vif_find_by_vif_index(vif_index);
	    if (pim_vif == NULL)
		break;
	    PimNbr *pim_nbr = pim_vif->pim_nbr_find(pim_nbr_addr);
	    if (pim_nbr == NULL)
		break;
	    pim_nbr->init_processing_pim_mre_sg();
	    return;
	}
    } while (false);
    
    list<PimNbr *>::iterator iter;
    for (iter = processing_pim_nbr_list().begin();
	 iter != processing_pim_nbr_list().end();
	 ++iter) {
	PimNbr *pim_nbr = *iter;
	if (pim_nbr->primary_addr() == pim_nbr_addr)
	    pim_nbr->init_processing_pim_mre_sg();
    }
}

//
// Prepare all PimNbr entries with neighbor address of @pim_nbr_add to
// process their (S,G,rpt) PimMre entries.
//
void
PimNode::init_processing_pim_mre_sg_rpt(uint32_t vif_index,
					const IPvX& pim_nbr_addr)
{
    do {
	if (vif_index != Vif::VIF_INDEX_INVALID) {
	    PimVif *pim_vif = vif_find_by_vif_index(vif_index);
	    if (pim_vif == NULL)
		break;
	    PimNbr *pim_nbr = pim_vif->pim_nbr_find(pim_nbr_addr);
	    if (pim_nbr == NULL)
		break;
	    pim_nbr->init_processing_pim_mre_sg_rpt();
	    return;
	}
    } while (false);
    
    list<PimNbr *>::iterator iter;
    for (iter = processing_pim_nbr_list().begin();
	 iter != processing_pim_nbr_list().end();
	 ++iter) {
	PimNbr *pim_nbr = *iter;
	if (pim_nbr->primary_addr() == pim_nbr_addr)
	    pim_nbr->init_processing_pim_mre_sg_rpt();
    }
}

PimNbr *
PimNode::find_processing_pim_mre_rp(uint32_t vif_index,
				    const IPvX& pim_nbr_addr)
{
    if (vif_index != Vif::VIF_INDEX_INVALID) {
	PimVif *pim_vif = vif_find_by_vif_index(vif_index);
	if (pim_vif == NULL)
	    return (NULL);
	PimNbr *pim_nbr = pim_vif->pim_nbr_find(pim_nbr_addr);
	if (pim_nbr == NULL)
	    return (NULL);
	if (pim_nbr->processing_pim_mre_rp_list().empty())
	    return (NULL);
	return (pim_nbr);
    }
    
    list<PimNbr *>::iterator iter;
    for (iter = processing_pim_nbr_list().begin();
	 iter != processing_pim_nbr_list().end();
	 ++iter) {
	PimNbr *pim_nbr = *iter;
	if (pim_nbr->primary_addr() != pim_nbr_addr)
	    continue;
	if (pim_nbr->processing_pim_mre_rp_list().empty())
	    continue;
	return (pim_nbr);
    }
    
    return (NULL);
}

PimNbr *
PimNode::find_processing_pim_mre_wc(uint32_t vif_index,
				    const IPvX& pim_nbr_addr)
{
    if (vif_index != Vif::VIF_INDEX_INVALID) {
	PimVif *pim_vif = vif_find_by_vif_index(vif_index);
	if (pim_vif == NULL)
	    return (NULL);
	PimNbr *pim_nbr = pim_vif->pim_nbr_find(pim_nbr_addr);
	if (pim_nbr == NULL)
	    return (NULL);
	if (pim_nbr->processing_pim_mre_wc_list().empty())
	    return (NULL);
	return (pim_nbr);
    }
    
    list<PimNbr *>::iterator iter;
    for (iter = processing_pim_nbr_list().begin();
	 iter != processing_pim_nbr_list().end();
	 ++iter) {
	PimNbr *pim_nbr = *iter;
	if (pim_nbr->primary_addr() != pim_nbr_addr)
	    continue;
	if (pim_nbr->processing_pim_mre_wc_list().empty())
	    continue;
	return (pim_nbr);
    }
    
    return (NULL);
}

PimNbr *
PimNode::find_processing_pim_mre_sg(uint32_t vif_index,
				    const IPvX& pim_nbr_addr)
{
    if (vif_index != Vif::VIF_INDEX_INVALID) {
	PimVif *pim_vif = vif_find_by_vif_index(vif_index);
	if (pim_vif == NULL)
	    return (NULL);
	PimNbr *pim_nbr = pim_vif->pim_nbr_find(pim_nbr_addr);
	if (pim_nbr == NULL)
	    return (NULL);
	if (pim_nbr->processing_pim_mre_sg_list().empty())
	    return (NULL);
	return (pim_nbr);
    }
    
    list<PimNbr *>::iterator iter;
    for (iter = processing_pim_nbr_list().begin();
	 iter != processing_pim_nbr_list().end();
	 ++iter) {
	PimNbr *pim_nbr = *iter;
	if (pim_nbr->primary_addr() != pim_nbr_addr)
	    continue;
	if (pim_nbr->processing_pim_mre_sg_list().empty())
	    continue;
	return (pim_nbr);
    }
    
    return (NULL);
}

PimNbr *
PimNode::find_processing_pim_mre_sg_rpt(uint32_t vif_index,
					const IPvX& pim_nbr_addr)
{
    if (vif_index != Vif::VIF_INDEX_INVALID) {
	PimVif *pim_vif = vif_find_by_vif_index(vif_index);
	if (pim_vif == NULL)
	    return (NULL);
	PimNbr *pim_nbr = pim_vif->pim_nbr_find(pim_nbr_addr);
	if (pim_nbr == NULL)
	    return (NULL);
	if (pim_nbr->processing_pim_mre_sg_rpt_list().empty())
	    return (NULL);
	return (pim_nbr);
    }
    
    list<PimNbr *>::iterator iter;
    for (iter = processing_pim_nbr_list().begin();
	 iter != processing_pim_nbr_list().end();
	 ++iter) {
	PimNbr *pim_nbr = *iter;
	if (pim_nbr->primary_addr() != pim_nbr_addr)
	    continue;
	if (pim_nbr->processing_pim_mre_sg_rpt_list().empty())
	    continue;
	return (pim_nbr);
    }
    
    return (NULL);
}

//
// Statistics-related counters and values
//
void
PimNode::clear_pim_statistics()
{
    for (uint32_t i = 0; i < maxvifs(); i++) {
	PimVif *pim_vif = vif_find_by_vif_index(i);
	if (pim_vif == NULL)
	    continue;
	pim_vif->clear_pim_statistics();
    }
}

int
PimNode::clear_pim_statistics_per_vif(const string& vif_name,
				      string& error_msg)
{
    PimVif *pim_vif = vif_find_by_name(vif_name);
    if (pim_vif == NULL) {
	error_msg = c_format("Cannot get statistics for vif %s: no such vif",
			     vif_name.c_str());
	return (XORP_ERROR);
    }
    
    pim_vif->clear_pim_statistics();
    
    return (XORP_OK);
}

#define GET_PIMSTAT_PER_NODE(stat_name)				\
uint32_t							\
PimNode::pimstat_##stat_name() const				\
{								\
    uint32_t sum = 0;						\
								\
    for (uint32_t i = 0; i < maxvifs(); i++) {			\
	PimVif *pim_vif = vif_find_by_vif_index(i);		\
	if (pim_vif == NULL)					\
	    continue;						\
	sum += pim_vif->pimstat_##stat_name();			\
    }								\
								\
    return (sum);						\
}

GET_PIMSTAT_PER_NODE(hello_messages_received)
GET_PIMSTAT_PER_NODE(hello_messages_sent)
GET_PIMSTAT_PER_NODE(hello_messages_rx_errors)
GET_PIMSTAT_PER_NODE(register_messages_received)
GET_PIMSTAT_PER_NODE(register_messages_sent)
GET_PIMSTAT_PER_NODE(register_messages_rx_errors)
GET_PIMSTAT_PER_NODE(register_stop_messages_received)
GET_PIMSTAT_PER_NODE(register_stop_messages_sent)
GET_PIMSTAT_PER_NODE(register_stop_messages_rx_errors)
GET_PIMSTAT_PER_NODE(join_prune_messages_received)
GET_PIMSTAT_PER_NODE(join_prune_messages_sent)
GET_PIMSTAT_PER_NODE(join_prune_messages_rx_errors)
GET_PIMSTAT_PER_NODE(bootstrap_messages_received)
GET_PIMSTAT_PER_NODE(bootstrap_messages_sent)
GET_PIMSTAT_PER_NODE(bootstrap_messages_rx_errors)
GET_PIMSTAT_PER_NODE(assert_messages_received)
GET_PIMSTAT_PER_NODE(assert_messages_sent)
GET_PIMSTAT_PER_NODE(assert_messages_rx_errors)
GET_PIMSTAT_PER_NODE(graft_messages_received)
GET_PIMSTAT_PER_NODE(graft_messages_sent)
GET_PIMSTAT_PER_NODE(graft_messages_rx_errors)
GET_PIMSTAT_PER_NODE(graft_ack_messages_received)
GET_PIMSTAT_PER_NODE(graft_ack_messages_sent)
GET_PIMSTAT_PER_NODE(graft_ack_messages_rx_errors)
GET_PIMSTAT_PER_NODE(candidate_rp_messages_received)
GET_PIMSTAT_PER_NODE(candidate_rp_messages_sent)
GET_PIMSTAT_PER_NODE(candidate_rp_messages_rx_errors)
//
GET_PIMSTAT_PER_NODE(unknown_type_messages)
GET_PIMSTAT_PER_NODE(unknown_version_messages)
GET_PIMSTAT_PER_NODE(neighbor_unknown_messages)
GET_PIMSTAT_PER_NODE(bad_length_messages)
GET_PIMSTAT_PER_NODE(bad_checksum_messages)
GET_PIMSTAT_PER_NODE(bad_receive_interface_messages)
GET_PIMSTAT_PER_NODE(rx_interface_disabled_messages)
GET_PIMSTAT_PER_NODE(rx_register_not_rp)
GET_PIMSTAT_PER_NODE(rp_filtered_source)
GET_PIMSTAT_PER_NODE(unknown_register_stop)
GET_PIMSTAT_PER_NODE(rx_join_prune_no_state)
GET_PIMSTAT_PER_NODE(rx_graft_graft_ack_no_state)
GET_PIMSTAT_PER_NODE(rx_graft_on_upstream_interface)
GET_PIMSTAT_PER_NODE(rx_candidate_rp_not_bsr)
GET_PIMSTAT_PER_NODE(rx_bsr_when_bsr)
GET_PIMSTAT_PER_NODE(rx_bsr_not_rpf_interface)
GET_PIMSTAT_PER_NODE(rx_unknown_hello_option)
GET_PIMSTAT_PER_NODE(rx_data_no_state)
GET_PIMSTAT_PER_NODE(rx_rp_no_state)
GET_PIMSTAT_PER_NODE(rx_aggregate)
GET_PIMSTAT_PER_NODE(rx_malformed_packet)
GET_PIMSTAT_PER_NODE(no_rp)
GET_PIMSTAT_PER_NODE(no_route_upstream)
GET_PIMSTAT_PER_NODE(rp_mismatch)
GET_PIMSTAT_PER_NODE(rpf_neighbor_unknown)
//
GET_PIMSTAT_PER_NODE(rx_join_rp)
GET_PIMSTAT_PER_NODE(rx_prune_rp)
GET_PIMSTAT_PER_NODE(rx_join_wc)
GET_PIMSTAT_PER_NODE(rx_prune_wc)
GET_PIMSTAT_PER_NODE(rx_join_sg)
GET_PIMSTAT_PER_NODE(rx_prune_sg)
GET_PIMSTAT_PER_NODE(rx_join_sg_rpt)
GET_PIMSTAT_PER_NODE(rx_prune_sg_rpt)

#undef GET_PIMSTAT_PER_NODE


#define GET_PIMSTAT_PER_VIF(stat_name)				\
int								\
PimNode::pimstat_##stat_name##_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const \
{								\
    result = 0;							\
								\
    PimVif *pim_vif = vif_find_by_name(vif_name);		\
    if (pim_vif == NULL) {					\
	error_msg = c_format("Cannot get statistics for vif %s: no such vif", \
			     vif_name.c_str());			\
	return (XORP_ERROR);					\
    }								\
								\
    result = pim_vif->pimstat_##stat_name();				\
    return (XORP_OK);						\
}

GET_PIMSTAT_PER_VIF(hello_messages_received)
GET_PIMSTAT_PER_VIF(hello_messages_sent)
GET_PIMSTAT_PER_VIF(hello_messages_rx_errors)
GET_PIMSTAT_PER_VIF(register_messages_received)
GET_PIMSTAT_PER_VIF(register_messages_sent)
GET_PIMSTAT_PER_VIF(register_messages_rx_errors)
GET_PIMSTAT_PER_VIF(register_stop_messages_received)
GET_PIMSTAT_PER_VIF(register_stop_messages_sent)
GET_PIMSTAT_PER_VIF(register_stop_messages_rx_errors)
GET_PIMSTAT_PER_VIF(join_prune_messages_received)
GET_PIMSTAT_PER_VIF(join_prune_messages_sent)
GET_PIMSTAT_PER_VIF(join_prune_messages_rx_errors)
GET_PIMSTAT_PER_VIF(bootstrap_messages_received)
GET_PIMSTAT_PER_VIF(bootstrap_messages_sent)
GET_PIMSTAT_PER_VIF(bootstrap_messages_rx_errors)
GET_PIMSTAT_PER_VIF(assert_messages_received)
GET_PIMSTAT_PER_VIF(assert_messages_sent)
GET_PIMSTAT_PER_VIF(assert_messages_rx_errors)
GET_PIMSTAT_PER_VIF(graft_messages_received)
GET_PIMSTAT_PER_VIF(graft_messages_sent)
GET_PIMSTAT_PER_VIF(graft_messages_rx_errors)
GET_PIMSTAT_PER_VIF(graft_ack_messages_received)
GET_PIMSTAT_PER_VIF(graft_ack_messages_sent)
GET_PIMSTAT_PER_VIF(graft_ack_messages_rx_errors)
GET_PIMSTAT_PER_VIF(candidate_rp_messages_received)
GET_PIMSTAT_PER_VIF(candidate_rp_messages_sent)
GET_PIMSTAT_PER_VIF(candidate_rp_messages_rx_errors)
//
GET_PIMSTAT_PER_VIF(unknown_type_messages)
GET_PIMSTAT_PER_VIF(unknown_version_messages)
GET_PIMSTAT_PER_VIF(neighbor_unknown_messages)
GET_PIMSTAT_PER_VIF(bad_length_messages)
GET_PIMSTAT_PER_VIF(bad_checksum_messages)
GET_PIMSTAT_PER_VIF(bad_receive_interface_messages)
GET_PIMSTAT_PER_VIF(rx_interface_disabled_messages)
GET_PIMSTAT_PER_VIF(rx_register_not_rp)
GET_PIMSTAT_PER_VIF(rp_filtered_source)
GET_PIMSTAT_PER_VIF(unknown_register_stop)
GET_PIMSTAT_PER_VIF(rx_join_prune_no_state)
GET_PIMSTAT_PER_VIF(rx_graft_graft_ack_no_state)
GET_PIMSTAT_PER_VIF(rx_graft_on_upstream_interface)
GET_PIMSTAT_PER_VIF(rx_candidate_rp_not_bsr)
GET_PIMSTAT_PER_VIF(rx_bsr_when_bsr)
GET_PIMSTAT_PER_VIF(rx_bsr_not_rpf_interface)
GET_PIMSTAT_PER_VIF(rx_unknown_hello_option)
GET_PIMSTAT_PER_VIF(rx_data_no_state)
GET_PIMSTAT_PER_VIF(rx_rp_no_state)
GET_PIMSTAT_PER_VIF(rx_aggregate)
GET_PIMSTAT_PER_VIF(rx_malformed_packet)
GET_PIMSTAT_PER_VIF(no_rp)
GET_PIMSTAT_PER_VIF(no_route_upstream)
GET_PIMSTAT_PER_VIF(rp_mismatch)
GET_PIMSTAT_PER_VIF(rpf_neighbor_unknown)
//
GET_PIMSTAT_PER_VIF(rx_join_rp)
GET_PIMSTAT_PER_VIF(rx_prune_rp)
GET_PIMSTAT_PER_VIF(rx_join_wc)
GET_PIMSTAT_PER_VIF(rx_prune_wc)
GET_PIMSTAT_PER_VIF(rx_join_sg)
GET_PIMSTAT_PER_VIF(rx_prune_sg)
GET_PIMSTAT_PER_VIF(rx_join_sg_rpt)
GET_PIMSTAT_PER_VIF(rx_prune_sg_rpt)

#undef GET_PIMSTAT_PER_VIF
