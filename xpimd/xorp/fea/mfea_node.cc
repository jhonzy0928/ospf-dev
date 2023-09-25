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

#ident "$XORP: xorp/fea/mfea_node.cc,v 1.67 2007/02/16 22:45:46 pavlin Exp $"

//
// MFEA (Multicast Forwarding Engine Abstraction) implementation.
//

#include "mfea_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"
#include "libxorp/debug.h"
#include "libxorp/ipvx.hh"
#include "libxorp/utils.hh"

#include "mrt/max_vifs.h"
#include "mrt/mifset.hh"
#include "mrt/multicast_defs.h"

#include "mfea_mrouter.hh"
#include "mfea_node.hh"
#include "mfea_proto_comm.hh"
#include "mfea_kernel_messages.hh"
#include "mfea_vif.hh"


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
 * MfeaNode::MfeaNode:
 * @family: The address family (%AF_INET or %AF_INET6
 * for IPv4 and IPv6 respectively).
 * @module_id: The module ID (must be %XORP_MODULE_MFEA).
 * @eventloop: The event loop.
 * 
 * MFEA node constructor.
 **/
MfeaNode::MfeaNode(int family, xorp_module_id module_id,
		   EventLoop& eventloop)
    : ProtoNode<MfeaVif>(family, module_id, eventloop),
      _mfea_mrouter(*this),
      _mfea_dft(*this),
#ifndef QUAGGA_MULTICAST
      _is_log_trace(false)
#else
      _log_flags(0)
#endif	// QUAGGA_MULTICAST
{
    XLOG_ASSERT(module_id == XORP_MODULE_MFEA);
    
    if (module_id != XORP_MODULE_MFEA) {
	XLOG_FATAL("Invalid module ID = %d (must be 'XORP_MODULE_MFEA' = %d)",
		   module_id, XORP_MODULE_MFEA);
    }
    
    for (size_t i = 0; i < _proto_comms.size(); i++)
	_proto_comms[i] = NULL;

    //
    // Set the node status
    //
    ProtoNode<MfeaVif>::set_node_status(PROC_STARTUP);

    //
    // Set myself as an observer when the node status changes
    //
    set_observer(this);
}

/**
 * MfeaNode::~MfeaNode:
 * @: 
 * 
 * MFEA node destructor.
 * 
 **/
MfeaNode::~MfeaNode()
{
    //
    // Unset myself as an observer when the node status changes
    //
    unset_observer(this);

    stop();

    ProtoNode<MfeaVif>::set_node_status(PROC_NULL);
    
    delete_all_vifs();
    
    // Delete the ProtoComm entries
    for (size_t i = 0; i < _proto_comms.size(); i++) {
	if (_proto_comms[i] != NULL)
	    delete _proto_comms[i];
	_proto_comms[i] = NULL;
    }
}

/**
 * MfeaNode::start:
 * @: 
 * 
 * Start the MFEA.
 * TODO: This function should not start the operation on the
 * interfaces. The interfaces must be activated separately.
 * After the startup operations are completed,
 * MfeaNode::final_start() is called to complete the job.
 * 
 * Return value: %XORP_OK on success, otherwize %XORP_ERROR.
 **/
int
MfeaNode::start()
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

    if (ProtoNode<MfeaVif>::pending_start() < 0)
	return (XORP_ERROR);

    //
    // Set the node status
    //
    ProtoNode<MfeaVif>::set_node_status(PROC_STARTUP);

#ifndef QUAGGA_MULTICAST
    //
    // Register with the FEA
    //
    fea_register_startup();
#endif	// !QUAGGA_MULTICAST

    // Start the MfeaMrouter
#ifdef QUAGGA_MULTICAST
    int r = _mfea_mrouter.start();
    if (r != XORP_OK)
	return r;
#else
    _mfea_mrouter.start();
#endif	// QUAGGA_MULTICAST
    
    // Start the ProtoComm entries
    for (size_t i = 0; i < _proto_comms.size(); i++) {
	if (_proto_comms[i] != NULL) {
	    _proto_comms[i]->start();
	}
    }

    return (XORP_OK);
}

/**
 * MfeaNode::final_start:
 * @: 
 * 
 * Completely start the node operation.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::final_start()
{
#if 0	// TODO: XXX: PAVPAVPAV
    if (! is_pending_up())
	return (XORP_ERROR);
#endif

    if (ProtoNode<MfeaVif>::start() < 0) {
	ProtoNode<MfeaVif>::stop();
	return (XORP_ERROR);
    }

    // Start the mfea_vifs
    start_all_vifs();

    XLOG_INFO(is_log_info(), "MFEA started");

    return (XORP_OK);
}

/**
 * MfeaNode::stop:
 * @: 
 * 
 * Gracefully stop the MFEA.
 * XXX: After the cleanup is completed,
 * MfeaNode::final_stop() is called to complete the job.
 * XXX: This function, unlike start(), will stop the MFEA
 * operation on all interfaces.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::stop()
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

    if (ProtoNode<MfeaVif>::pending_stop() < 0)
	return (XORP_ERROR);

    //
    // Perform misc. MFEA-specific stop operations
    //
    
    // Stop the vifs
    stop_all_vifs();
    
    // Stop the ProtoComm entries
    for (size_t i = 0; i < _proto_comms.size(); i++) {
	if (_proto_comms[i] != NULL)
	    _proto_comms[i]->stop();
    }

    // Stop the MfeaMrouter
    _mfea_mrouter.stop();

    //
    // Set the node status
    //
    ProtoNode<MfeaVif>::set_node_status(PROC_SHUTDOWN);

    //
    // Update the node status
    //
    update_status();

    return (XORP_OK);
}

/**
 * MfeaNode::final_stop:
 * @: 
 * 
 * Completely stop the MFEA operation.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::final_stop()
{
    if (! (is_up() || is_pending_up() || is_pending_down()))
	return (XORP_ERROR);

    if (ProtoNode<MfeaVif>::stop() < 0)
	return (XORP_ERROR);

    XLOG_INFO(is_log_info(), "MFEA stopped");

    return (XORP_OK);
}

/**
 * Enable the node operation.
 * 
 * If an unit is not enabled, it cannot be start, or pending-start.
 */
void
MfeaNode::enable()
{
    ProtoUnit::enable();

    XLOG_INFO(is_log_info(), "MFEA enabled");
}

/**
 * Disable the node operation.
 * 
 * If an unit is disabled, it cannot be start or pending-start.
 * If the unit was runnning, it will be stop first.
 */
void
MfeaNode::disable()
{
    stop();
    ProtoUnit::disable();

    XLOG_INFO(is_log_info(), "MFEA disabled");
}

void
MfeaNode::status_change(ServiceBase*  service,
			ServiceStatus old_status,
			ServiceStatus new_status)
{
    if (service == this) {
	// My own status has changed
	if ((old_status == SERVICE_STARTING)
	    && (new_status == SERVICE_RUNNING)) {
	    // The startup process has completed
	    if (final_start() < 0) {
		XLOG_ERROR("Cannot complete the startup process; "
			   "current state is %s",
			   ProtoNode<MfeaVif>::state_str().c_str());
		return;
	    }
	    ProtoNode<MfeaVif>::set_node_status(PROC_READY);
	    return;
	}

	if ((old_status == SERVICE_SHUTTING_DOWN)
	    && (new_status == SERVICE_SHUTDOWN)) {
	    // The shutdown process has completed
	    final_stop();
	    // Set the node status
	    ProtoNode<MfeaVif>::set_node_status(PROC_DONE);
	    return;
	}

	//
	// TODO: check if there was an error
	//
	return;
    }

#ifndef QUAGGA_MULTICAST
    if (service == ifmgr_mirror_service_base()) {
	if ((old_status == SERVICE_SHUTTING_DOWN)
	    && (new_status == SERVICE_SHUTDOWN)) {
	    MfeaNode::decr_shutdown_requests_n();
	}
    }
#endif	// !QUAGGA_MULTICAST
}

#ifndef QUAGGA_MULTICAST
void
MfeaNode::tree_complete()
{
    //
    // XXX: we use same actions when the tree is completed or updates are made
    //
    updates_made();

    decr_startup_requests_n();
}

void
MfeaNode::updates_made()
{
    map<string, Vif>::iterator mfea_vif_iter;
    string error_msg;

    //
    // Update the local copy of the interface tree
    //
    _iftree = ifmgr_iftree();

    //
    // Add new vifs and update existing ones
    //
    IfMgrIfTree::IfMap::const_iterator ifmgr_iface_iter;
    for (ifmgr_iface_iter = _iftree.ifs().begin();
	 ifmgr_iface_iter != _iftree.ifs().end();
	 ++ifmgr_iface_iter) {
	const IfMgrIfAtom& ifmgr_iface = ifmgr_iface_iter->second;

	IfMgrIfAtom::VifMap::const_iterator ifmgr_vif_iter;
	for (ifmgr_vif_iter = ifmgr_iface.vifs().begin();
	     ifmgr_vif_iter != ifmgr_iface.vifs().end();
	     ++ifmgr_vif_iter) {
	    const IfMgrVifAtom& ifmgr_vif = ifmgr_vif_iter->second;
	    const string& ifmgr_vif_name = ifmgr_vif.name();
	    Vif* node_vif = NULL;
	
	    mfea_vif_iter = configured_vifs().find(ifmgr_vif_name);
	    if (mfea_vif_iter != configured_vifs().end()) {
		node_vif = &(mfea_vif_iter->second);
	    }

	    //
	    // Add a new vif
	    //
	    if (node_vif == NULL) {
		uint32_t vif_index = find_unused_config_vif_index();
		XLOG_ASSERT(vif_index != Vif::VIF_INDEX_INVALID);
		if (add_config_vif(ifmgr_vif_name, vif_index, error_msg) < 0) {
		    XLOG_ERROR("Cannot add vif %s to the set of configured "
			       "vifs: %s",
			       ifmgr_vif_name.c_str(), error_msg.c_str());
		    continue;
		}
		mfea_vif_iter = configured_vifs().find(ifmgr_vif_name);
		XLOG_ASSERT(mfea_vif_iter != configured_vifs().end());
		node_vif = &(mfea_vif_iter->second);
		// FALLTHROUGH
	    }

	    //
	    // Update the pif_index
	    //
	    set_config_pif_index(ifmgr_vif_name,
				 ifmgr_vif.pif_index(),
				 error_msg);
	
	    //
	    // Update the vif flags
	    //
	    bool is_up = ifmgr_iface.enabled();
	    is_up &= (! ifmgr_iface.no_carrier());
	    is_up &= ifmgr_vif.enabled();
	    set_config_vif_flags(ifmgr_vif_name,
				 false,	// is_pim_register
				 ifmgr_vif.p2p_capable(),
				 ifmgr_vif.loopback(),
				 ifmgr_vif.multicast_capable(),
				 ifmgr_vif.broadcast_capable(),
				 is_up,
				 ifmgr_iface.mtu_bytes(),
				 error_msg);
	
	}
    }

    //
    // Add new vif addresses, update existing ones, and remove old addresses
    //
    for (ifmgr_iface_iter = _iftree.ifs().begin();
	 ifmgr_iface_iter != _iftree.ifs().end();
	 ++ifmgr_iface_iter) {
	const IfMgrIfAtom& ifmgr_iface = ifmgr_iface_iter->second;
	const string& ifmgr_iface_name = ifmgr_iface.name();
	IfMgrIfAtom::VifMap::const_iterator ifmgr_vif_iter;

	for (ifmgr_vif_iter = ifmgr_iface.vifs().begin();
	     ifmgr_vif_iter != ifmgr_iface.vifs().end();
	     ++ifmgr_vif_iter) {
	    const IfMgrVifAtom& ifmgr_vif = ifmgr_vif_iter->second;
	    const string& ifmgr_vif_name = ifmgr_vif.name();
	    Vif* node_vif = NULL;

	    //
	    // Add new vif addresses and update existing ones
	    //
	    mfea_vif_iter = configured_vifs().find(ifmgr_vif_name);
	    if (mfea_vif_iter != configured_vifs().end()) {
		node_vif = &(mfea_vif_iter->second);
	    }

	    if (is_ipv4()) {
		IfMgrVifAtom::V4Map::const_iterator a4_iter;

		for (a4_iter = ifmgr_vif.ipv4addrs().begin();
		     a4_iter != ifmgr_vif.ipv4addrs().end();
		     ++a4_iter) {
		    const IfMgrIPv4Atom& a4 = a4_iter->second;
		    VifAddr* node_vif_addr = node_vif->find_address(IPvX(a4.addr()));
		    IPvX addr(a4.addr());
		    IPvXNet subnet_addr(addr, a4.prefix_len());
		    IPvX broadcast_addr(IPvX::ZERO(family()));
		    IPvX peer_addr(IPvX::ZERO(family()));
		    if (a4.has_broadcast())
			broadcast_addr = IPvX(a4.broadcast_addr());
		    if (a4.has_endpoint())
			peer_addr = IPvX(a4.endpoint_addr());

		    if (node_vif_addr == NULL) {
			if (add_config_vif_addr(
				ifmgr_vif_name,
				addr,
				subnet_addr,
				broadcast_addr,
				peer_addr,
				error_msg) < 0) {
			    XLOG_ERROR("Cannot add address %s to vif %s from "
				       "the set of configured vifs: %s",
				       cstring(addr), ifmgr_vif_name.c_str(),
				       error_msg.c_str());
			}
			continue;
		    }
		    if ((addr == node_vif_addr->addr())
			&& (subnet_addr == node_vif_addr->subnet_addr())
			&& (broadcast_addr == node_vif_addr->broadcast_addr())
			&& (peer_addr == node_vif_addr->peer_addr())) {
			continue;	// Nothing changed
		    }

		    // Update the address
		    if (delete_config_vif_addr(ifmgr_vif_name,
					       addr,
					       error_msg) < 0) {
			XLOG_ERROR("Cannot delete address %s from vif %s "
				   "from the set of configured vifs: %s",
				   cstring(addr),
				   ifmgr_vif_name.c_str(),
				   error_msg.c_str());
		    }
		    if (add_config_vif_addr(
			    ifmgr_vif_name,
			    addr,
			    subnet_addr,
			    broadcast_addr,
			    peer_addr,
			    error_msg) < 0) {
			XLOG_ERROR("Cannot add address %s to vif %s from "
				   "the set of configured vifs: %s",
				   cstring(addr), ifmgr_vif_name.c_str(),
				   error_msg.c_str());
		    }
		}
	    }

	    if (is_ipv6()) {
		IfMgrVifAtom::V6Map::const_iterator a6_iter;

		for (a6_iter = ifmgr_vif.ipv6addrs().begin();
		     a6_iter != ifmgr_vif.ipv6addrs().end();
		     ++a6_iter) {
		    const IfMgrIPv6Atom& a6 = a6_iter->second;
		    VifAddr* node_vif_addr = node_vif->find_address(IPvX(a6.addr()));
		    IPvX addr(a6.addr());
		    IPvXNet subnet_addr(addr, a6.prefix_len());
		    IPvX broadcast_addr(IPvX::ZERO(family()));
		    IPvX peer_addr(IPvX::ZERO(family()));
		    if (a6.has_endpoint())
			peer_addr = IPvX(a6.endpoint_addr());

		    if (node_vif_addr == NULL) {
			if (add_config_vif_addr(
				ifmgr_vif_name,
				addr,
				subnet_addr,
				broadcast_addr,
				peer_addr,
				error_msg) < 0) {
			    XLOG_ERROR("Cannot add address %s to vif %s from "
				       "the set of configured vifs: %s",
				       cstring(addr), ifmgr_vif_name.c_str(),
				       error_msg.c_str());
			}
			continue;
		    }
		    if ((addr == node_vif_addr->addr())
			&& (subnet_addr == node_vif_addr->subnet_addr())
			&& (peer_addr == node_vif_addr->peer_addr())) {
			continue;	// Nothing changed
		    }

		    // Update the address
		    if (delete_config_vif_addr(ifmgr_vif_name,
					       addr,
					       error_msg) < 0) {
			XLOG_ERROR("Cannot delete address %s from vif %s "
				   "from the set of configured vifs: %s",
				   cstring(addr),
				   ifmgr_vif_name.c_str(),
				   error_msg.c_str());
		    }
		    if (add_config_vif_addr(
			    ifmgr_vif_name,
			    addr,
			    subnet_addr,
			    broadcast_addr,
			    peer_addr,
			    error_msg) < 0) {
			XLOG_ERROR("Cannot add address %s to vif %s from "
				   "the set of configured vifs: %s",
				   cstring(addr), ifmgr_vif_name.c_str(),
				   error_msg.c_str());
		    }
		}
	    }

	    //
	    // Delete vif addresses that don't exist anymore
	    //
	    {
		list<IPvX> delete_addresses_list;
		list<VifAddr>::const_iterator vif_addr_iter;
		for (vif_addr_iter = node_vif->addr_list().begin();
		     vif_addr_iter != node_vif->addr_list().end();
		     ++vif_addr_iter) {
		    const VifAddr& vif_addr = *vif_addr_iter;
		    if (vif_addr.addr().is_ipv4()
			&& (_iftree.find_addr(ifmgr_iface_name,
					      ifmgr_vif_name,
					      vif_addr.addr().get_ipv4()))
			    == NULL) {
			    delete_addresses_list.push_back(vif_addr.addr());
		    }
		    if (vif_addr.addr().is_ipv6()
			&& (_iftree.find_addr(ifmgr_iface_name,
					      ifmgr_vif_name,
					      vif_addr.addr().get_ipv6()))
			    == NULL) {
			    delete_addresses_list.push_back(vif_addr.addr());
		    }
		}

		// Delete the addresses
		list<IPvX>::iterator ipvx_iter;
		for (ipvx_iter = delete_addresses_list.begin();
		     ipvx_iter != delete_addresses_list.end();
		     ++ipvx_iter) {
		    const IPvX& ipvx = *ipvx_iter;
		    if (delete_config_vif_addr(ifmgr_vif_name, ipvx, error_msg)
			< 0) {
			XLOG_ERROR("Cannot delete address %s from vif %s from "
				   "the set of configured vifs: %s",
				   cstring(ipvx), ifmgr_vif_name.c_str(),
				   error_msg.c_str());
		    }
		}
	    }
	}
    }

    //
    // Remove vifs that don't exist anymore
    //
    list<string> delete_vifs_list;
    for (mfea_vif_iter = configured_vifs().begin();
	 mfea_vif_iter != configured_vifs().end();
	 ++mfea_vif_iter) {
	Vif* node_vif = &mfea_vif_iter->second;
	if (node_vif->is_pim_register())
	    continue;		// XXX: don't delete the PIM Register vif
	if (_iftree.find_vif(node_vif->name(), node_vif->name()) == NULL) {
	    // Add the vif to the list of old interfaces
	    delete_vifs_list.push_back(node_vif->name());
	}
    }
    // Delete the old vifs
    list<string>::iterator vif_name_iter;
    for (vif_name_iter = delete_vifs_list.begin();
	 vif_name_iter != delete_vifs_list.end();
	 ++vif_name_iter) {
	const string& vif_name = *vif_name_iter;
	if (delete_config_vif(vif_name, error_msg) < 0) {
	    XLOG_ERROR("Cannot delete vif %s from the set of configured "
		       "vifs: %s",
		       vif_name.c_str(), error_msg.c_str());
	}
    }
    
    // Done
    set_config_all_vifs_done(error_msg);
}
#endif	// !QUAGGA_MULTICAST

/**
 * MfeaNode::add_vif:
 * @vif: Vif information about the new MfeaVif to install.
 * @error_msg: The error message (if error).
 * 
 * Install a new MFEA vif.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::add_vif(const Vif& vif, string& error_msg)
{
    //
    // Create a new MfeaVif
    //
    MfeaVif *mfea_vif = new MfeaVif(*this, vif);
    
    if (ProtoNode<MfeaVif>::add_vif(mfea_vif) != XORP_OK) {
	// Cannot add this new vif
	error_msg = c_format("Cannot add vif %s: internal error",
			     vif.name().c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	
	delete mfea_vif;
	return (XORP_ERROR);
    }
    
    XLOG_INFO(is_log_info(), "Interface added: %s", mfea_vif->str().c_str());
    
    return (XORP_OK);
}

/**
 * MfeaNode::add_pim_register_vif:
 * 
 * Install a new MFEA PIM Register vif (if needed).
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::add_pim_register_vif()
{
    string error_msg;

    //
    // Test first whether we have already PIM Register vif
    //
    for (uint32_t i = 0; i < maxvifs(); i++) {
	MfeaVif *mfea_vif = vif_find_by_vif_index(i);
	if (mfea_vif == NULL)
	    continue;
	if (mfea_vif->is_pim_register())
	    return (XORP_OK);		// Found: OK
    }
    
    //
    // Create the PIM Register vif if there is a valid IP address
    // on an interface that is already up and running.
    //
    // TODO: check with Linux, Solaris, etc, if we can
    // use 127.0.0.2 or ::2 as a PIM Register vif address, and use that
    // address instead (otherwise we may always have to keep track
    // whether the underlying address has changed).
    //
    bool mfea_vif_found = false;
    MfeaVif *mfea_vif = NULL;
    for (uint32_t i = 0; i < maxvifs(); i++) {
	mfea_vif = vif_find_by_vif_index(i);
	if (mfea_vif == NULL)
	    continue;
	if (! mfea_vif->is_underlying_vif_up())
	    continue;
	if (! mfea_vif->is_up())
	    continue;
	if (mfea_vif->addr_ptr() == NULL)
	    continue;
	if (mfea_vif->is_pim_register())
	    continue;
	if (mfea_vif->is_loopback())
	    continue;
	if (! mfea_vif->is_multicast_capable())
	    continue;
	// Found appropriate vif
	mfea_vif_found = true;
	break;
    }
    if (mfea_vif_found) {
	// Add the Register vif
	uint32_t vif_index = find_unused_config_vif_index();
	XLOG_ASSERT(vif_index != Vif::VIF_INDEX_INVALID);
	// TODO: XXX: the Register vif name is hardcoded here!
	MfeaVif register_vif(*this, Vif("register_vif"));
	register_vif.set_vif_index(vif_index);
	register_vif.set_pif_index(mfea_vif->pif_index());
	register_vif.set_underlying_vif_up(true); // XXX: 'true' to allow creation
	register_vif.set_pim_register(true);
	register_vif.set_mtu(mfea_vif->mtu());
	// Add all addresses, but ignore subnets, broadcast and p2p addresses
	list<VifAddr>::const_iterator vif_addr_iter;
	for (vif_addr_iter = mfea_vif->addr_list().begin();
	     vif_addr_iter != mfea_vif->addr_list().end();
	     ++vif_addr_iter) {
	    const VifAddr& vif_addr = *vif_addr_iter;
	    const IPvX& ipvx = vif_addr.addr();
	    register_vif.add_address(ipvx, IPvXNet(ipvx, ipvx.addr_bitlen()),
				     ipvx, IPvX::ZERO(family()));
	}
	if (add_vif(register_vif, error_msg) < 0) {
	    XLOG_ERROR("Cannot add Register vif: %s", error_msg.c_str());
	    return (XORP_ERROR);
	}
	
	if (add_config_vif(register_vif, error_msg) < 0) {
	    XLOG_ERROR("Cannot add Register vif to set of configured vifs: %s",
		       error_msg.c_str());
	    return (XORP_ERROR);
	}
    }

    // Done
    set_config_all_vifs_done(error_msg);

    return (XORP_OK);
}

/**
 * MfeaNode::delete_vif:
 * @vif_name: The name of the vif to delete.
 * @error_msg: The error message (if error).
 * 
 * Delete an existing MFEA vif.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::delete_vif(const string& vif_name, string& error_msg)
{
    MfeaVif *mfea_vif = vif_find_by_name(vif_name);
    if (mfea_vif == NULL) {
	error_msg = c_format("Cannot delete vif %s: no such vif",
		       vif_name.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    if (ProtoNode<MfeaVif>::delete_vif(mfea_vif) != XORP_OK) {
	error_msg = c_format("Cannot delete vif %s: internal error",
			     vif_name.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	delete mfea_vif;
	return (XORP_ERROR);
    }
    
    delete mfea_vif;
    
    XLOG_INFO(is_log_info(), "Interface deleted: %s", vif_name.c_str());
    
    return (XORP_OK);
}

/**
 * MfeaNode::enable_vif:
 * @vif_name: The name of the vif to enable.
 * @error_msg: The error message (if error).
 * 
 * Enable an existing MFEA vif.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::enable_vif(const string& vif_name, string& error_msg)
{
    MfeaVif *mfea_vif = vif_find_by_name(vif_name);
    if (mfea_vif == NULL) {
	error_msg = c_format("Cannot enable vif %s: no such vif",
			     vif_name.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    mfea_vif->enable();
    
    return (XORP_OK);
}

/**
 * MfeaNode::disable_vif:
 * @vif_name: The name of the vif to disable.
 * @error_msg: The error message (if error).
 * 
 * Disable an existing MFEA vif.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::disable_vif(const string& vif_name, string& error_msg)
{
    MfeaVif *mfea_vif = vif_find_by_name(vif_name);
    if (mfea_vif == NULL) {
	error_msg = c_format("Cannot disable vif %s: no such vif",
			     vif_name.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    mfea_vif->disable();
    
    return (XORP_OK);
}

/**
 * MfeaNode::start_vif:
 * @vif_name: The name of the vif to start.
 * @error_msg: The error message (if error).
 * 
 * Start an existing MFEA vif.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
#ifdef QUAGGA_MULTICAST
int
MfeaNode::start_vif(const string& vif_name, string& error_msg,
		    bool add_pimreg_vif)
#else
int
MfeaNode::start_vif(const string& vif_name, string& error_msg)
#endif	// QUAGGA_MULTICAST
{
    MfeaVif *mfea_vif = vif_find_by_name(vif_name);
    if (mfea_vif == NULL) {
	error_msg = c_format("Cannot start vif %s: no such vif",
			     vif_name.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    if (mfea_vif->start(error_msg) != XORP_OK) {
	error_msg = c_format("Cannot start vif %s: %s",
			     vif_name.c_str(), error_msg.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
#ifdef QUAGGA_MULTICAST
    if (add_pimreg_vif)
	add_pim_register_vif();	// add PIM Register vif (if needed)
#else
    // XXX: add PIM Register vif (if needed)
    add_pim_register_vif();
#endif	// QUAGGA_MULTICAST

    return (XORP_OK);
}

/**
 * MfeaNode::stop_vif:
 * @vif_name: The name of the vif to stop.
 * @error_msg: The error message (if error).
 * 
 * Stop an existing MFEA vif.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::stop_vif(const string& vif_name, string& error_msg)
{
    MfeaVif *mfea_vif = vif_find_by_name(vif_name);
    if (mfea_vif == NULL) {
	error_msg = c_format("Cannot stop vif %s: no such vif",
			     vif_name.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    if (mfea_vif->stop(error_msg) != XORP_OK) {
	error_msg = c_format("Cannot stop vif %s: %s",
			     vif_name.c_str(), error_msg.c_str());
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::start_all_vifs:
 * @: 
 * 
 * Start MFEA on all enabled interfaces.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::start_all_vifs()
{
    vector<MfeaVif *>::iterator iter;
    string error_msg;
    int ret_value = XORP_OK;
    
    for (iter = proto_vifs().begin(); iter != proto_vifs().end(); ++iter) {
	MfeaVif *mfea_vif = (*iter);
	if (mfea_vif == NULL)
	    continue;
#ifdef QUAGGA_MULTICAST
	if (start_vif(mfea_vif->name(), error_msg, false) != XORP_OK)
	    ret_value = XORP_ERROR;
#else
	if (start_vif(mfea_vif->name(), error_msg) != XORP_OK)
	    ret_value = XORP_ERROR;
#endif	// QUAGGA_MULTICAST
    }
    
#ifdef QUAGGA_MULTICAST
    // add PIM Register vif (if needed)
    add_pim_register_vif();
#endif	// QUAGGA_MULTICAST

    return (ret_value);
}

/**
 * MfeaNode::stop_all_vifs:
 * @: 
 * 
 * Stop MFEA on all interfaces it was running on.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::stop_all_vifs()
{
    vector<MfeaVif *>::iterator iter;
    string error_msg;
    int ret_value = XORP_OK;
    
    for (iter = proto_vifs().begin(); iter != proto_vifs().end(); ++iter) {
	MfeaVif *mfea_vif = (*iter);
	if (mfea_vif == NULL)
	    continue;
	if (stop_vif(mfea_vif->name(), error_msg) != XORP_OK)
	    ret_value = XORP_ERROR;
    }
    
    return (ret_value);
}

/**
 * MfeaNode::enable_all_vifs:
 * @: 
 * 
 * Enable MFEA on all interfaces.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::enable_all_vifs()
{
    vector<MfeaVif *>::iterator iter;
    string error_msg;
    int ret_value = XORP_OK;
    
    for (iter = proto_vifs().begin(); iter != proto_vifs().end(); ++iter) {
	MfeaVif *mfea_vif = (*iter);
	if (mfea_vif == NULL)
	    continue;
	if (enable_vif(mfea_vif->name(), error_msg) != XORP_OK)
	    ret_value = XORP_ERROR;
    }
    
    return (ret_value);
}

/**
 * MfeaNode::disable_all_vifs:
 * @: 
 * 
 * Disable MFEA on all interfaces. All running interfaces are stopped first.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::disable_all_vifs()
{
    vector<MfeaVif *>::iterator iter;
    string error_msg;
    int ret_value = XORP_OK;
    
    for (iter = proto_vifs().begin(); iter != proto_vifs().end(); ++iter) {
	MfeaVif *mfea_vif = (*iter);
	if (mfea_vif == NULL)
	    continue;
	if (disable_vif(mfea_vif->name(), error_msg) != XORP_OK)
	    ret_value = XORP_ERROR;
    }
    
    return (ret_value);
}

/**
 * MfeaNode::delete_all_vifs:
 * @: 
 * 
 * Delete all MFEA vifs.
 **/
void
MfeaNode::delete_all_vifs()
{
    list<string> vif_names;
    vector<MfeaVif *>::iterator iter;

    //
    // Create the list of all vif names to delete
    //
    for (iter = proto_vifs().begin(); iter != proto_vifs().end(); ++iter) {
	MfeaVif *mfea_vif = (*iter);
	if (mfea_vif != NULL) {
	    string vif_name = mfea_vif->name();
	    vif_names.push_back(mfea_vif->name());
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
MfeaNode::vif_shutdown_completed(const string& vif_name)
{
    vector<MfeaVif *>::iterator iter;

    UNUSED(vif_name);

    //
    // If all vifs have completed the shutdown, then de-register with
    // the MFEA.
    //
    for (iter = proto_vifs().begin(); iter != proto_vifs().end(); ++iter) {
	MfeaVif *mfea_vif = *iter;
	if (mfea_vif == NULL)
	    continue;
	if (! mfea_vif->is_down())
	    return;
    }

#ifndef QUAGGA_MULTICAST
    //
    // De-register with the FEA
    //
    fea_register_shutdown();
#endif	// !QUAGGA_MULTICAST
}

/**
 * MfeaNode::add_protocol:
 * @module_instance_name: The module instance name of the protocol to add.
 * @module_id: The #xorp_module_id of the protocol to add.
 * 
 * A method used by a protocol instance to register with this #MfeaNode.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::add_protocol(const string& module_instance_name,
		       xorp_module_id module_id)
{
    ProtoComm *proto_comm;
    int ip_protocol;
    size_t i;
    
    // Add the state
    if (_proto_register.add_protocol(module_instance_name, module_id) < 0) {
	XLOG_ERROR("Cannot add protocol instance %s with module_id = %d",
		   module_instance_name.c_str(), module_id);
	return (XORP_ERROR);	// Already added
    }
    
    // Test if we have already the appropriate ProtoComm
    if (proto_comm_find_by_module_id(module_id) != NULL)
	return (XORP_OK);
    
    //
    // Get the IP protocol number (IPPROTO_*)
    //
    ip_protocol = -1;
    switch (module_id) {
    case XORP_MODULE_MLD6IGMP:
	switch (family()) {
	case AF_INET:
	    ip_protocol = IPPROTO_IGMP;
	    break;
#ifdef HAVE_IPV6
	case AF_INET6:
	    ip_protocol = IPPROTO_ICMPV6;
	    break;
#endif // HAVE_IPV6
	default:
	    XLOG_UNREACHABLE();
	    _proto_register.delete_protocol(module_instance_name, module_id);
	    return (XORP_ERROR);
	}
	break;
    case XORP_MODULE_PIMSM:
    case XORP_MODULE_PIMDM:
	ip_protocol = IPPROTO_PIM;
	break;
    default:
	XLOG_UNREACHABLE();
	_proto_register.delete_protocol(module_instance_name, module_id);
	return (XORP_ERROR);
    }
    
    proto_comm = new ProtoComm(*this, ip_protocol, module_id);
    
    // Add the new entry
    for (i = 0; i < _proto_comms.size(); i++) {
	if (_proto_comms[i] == NULL)
	    break;
    }
    if (i < _proto_comms.size()) {
	_proto_comms[i] = proto_comm;
    } else {
	_proto_comms.push_back(proto_comm);
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::delete_protocol:
 * @module_instance_name: The module instance name of the protocol to delete.
 * @module_id: The #xorp_module_id of the protocol to delete.
 * 
 * A method used by a protocol instance to deregister with this #MfeaNode.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::delete_protocol(const string& module_instance_name,
			  xorp_module_id module_id)
{
    vector<MfeaVif *>::iterator iter;

    // Explicitly stop the protocol on all vifs
    for (iter = proto_vifs().begin(); iter != proto_vifs().end(); ++iter) {
	MfeaVif *mfea_vif = (*iter);
	if (mfea_vif == NULL)
	    continue;
	// TODO: XXX: PAVPAVPAV: shall we ignore the disabled vifs??
	mfea_vif->stop_protocol(module_instance_name, module_id);
    }
    
    // Delete kernel signal registration
    if (_kernel_signal_messages_register.is_registered(module_instance_name,
						       module_id)) {
	delete_allow_kernel_signal_messages(module_instance_name, module_id);
    }
    
    // Delete the state
    if (_proto_register.delete_protocol(module_instance_name, module_id) < 0) {
	XLOG_ERROR("Cannot delete protocol instance %s with module_id = %d",
		   module_instance_name.c_str(), module_id);
	return (XORP_ERROR);	// Probably not added before
    }
    
    if (! _proto_register.is_registered(module_id)) {
	//
	// The last registered protocol instance
	//
	ProtoComm *proto_comm = proto_comm_find_by_module_id(module_id);
	
	if (proto_comm == NULL)
	    return (XORP_ERROR);
	
	// Remove the pointer storage for this ProtoComm entry
	for (size_t i = 0; i < _proto_comms.size(); i++) {
	    if (_proto_comms[i] == proto_comm) {
		_proto_comms[i] = NULL;
		break;
	    }
	}
	if (_proto_comms[_proto_comms.size() - 1] == NULL) {
	    // Remove the last entry, if not used anymore
	    _proto_comms.pop_back();
	}
	
	delete proto_comm;
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::start_protocol:
 * @module_id: The #xorp_module_id of the protocol to start.
 * 
 * Start operation for protocol with #xorp_module_id of @module_id.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::start_protocol(xorp_module_id module_id)
{
    ProtoComm *proto_comm = proto_comm_find_by_module_id(module_id);
    
    if (proto_comm == NULL)
	return (XORP_ERROR);
    
    if (proto_comm->is_up())
	return (XORP_OK);		// Already running
    
    if (proto_comm->start() < 0)
	return (XORP_ERROR);
    
    return (XORP_OK);
}

/**
 * MfeaNode::stop_protocol:
 * @module_id: The #xorp_module_id of the protocol to stop.
 * 
 * Stop operation for protocol with #xorp_module_id of @module_id.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::stop_protocol(xorp_module_id module_id)
{
    ProtoComm *proto_comm = proto_comm_find_by_module_id(module_id);
    
    if (proto_comm == NULL)
	return (XORP_ERROR);
    
    if (proto_comm->stop() < 0)
	return (XORP_ERROR);
    
    return (XORP_OK);
}

/**
 * MfeaNode::start_protocol_vif:
 * @module_instance_name: The module instance name of the protocol to start
 * on vif with vif_index of @vif_index.
 * @module_id: The #xorp_module_id of the protocol to start on vif with
 * vif_index of @vif_index.
 * @vif_index: The index of the vif the protocol start to apply to.
 * 
 * Start a protocol on an interface with vif_index of @vif_index.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::start_protocol_vif(const string& module_instance_name,
			     xorp_module_id module_id,
			     uint32_t vif_index)
{
    MfeaVif *mfea_vif = vif_find_by_vif_index(vif_index);
    
    if (mfea_vif == NULL) {
	XLOG_ERROR("Cannot start protocol instance %s on vif_index %d: "
		   "no such vif",
		   module_instance_name.c_str(), vif_index);
	return (XORP_ERROR);
    }
    
    if (mfea_vif->start_protocol(module_instance_name, module_id) < 0)
	return (XORP_ERROR);
    
    return (XORP_OK);
}

/**
 * MfeaNode::stop_protocol_vif:
 * @module_instance_name: The module instance name of the protocol to stop
 * on vif with vif_index of @vif_index.
 * @module_id: The #xorp_module_id of the protocol to stop on vif with
 * vif_index of @vif_index.
 * @vif_index: The index of the vif the protocol stop to apply to.
 * 
 * Stop a protocol on an interface with vif_index of @vif_index.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::stop_protocol_vif(const string& module_instance_name,
			    xorp_module_id module_id,
			    uint32_t vif_index)
{
    MfeaVif *mfea_vif = vif_find_by_vif_index(vif_index);
    
    if (mfea_vif == NULL) {
	XLOG_ERROR("Cannot stop protocol instance %s on vif_index %d: "
		   "no such vif",
		   module_instance_name.c_str(), vif_index);
	return (XORP_ERROR);
    }
    
    if (mfea_vif->stop_protocol(module_instance_name, module_id) < 0)
	return (XORP_ERROR);
    
    return (XORP_OK);
}

/**
 * MfeaNode::add_allow_kernel_signal_messages:
 * @module_instance_name: The module instance name of the protocol to add.
 * @module_id: The #xorp_module_id of the protocol to add to receive kernel
 * signal messages.
 * 
 * Add a protocol to the set of protocols that are interested in
 * receiving kernel signal messages.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::add_allow_kernel_signal_messages(const string& module_instance_name,
					   xorp_module_id module_id)
{
    // Add the state
    if (_kernel_signal_messages_register.add_protocol(module_instance_name,
						      module_id)
	< 0) {
	XLOG_ERROR("Cannot add protocol instance %s with module_id = %d "
		   "to receive kernel signal messages",
		   module_instance_name.c_str(), module_id);
	return (XORP_ERROR);	// Already added
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::delete_allow_kernel_signal_messages:
 * @module_instance_name: The module instance name of the protocol to delete.
 * @module_id: The #xorp_module_id of the protocol to delete from receiving
 * kernel signal messages.
 * 
 * Delete a protocol from the set of protocols that are interested in
 * receiving kernel signal messages.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::delete_allow_kernel_signal_messages(const string& module_instance_name,
					      xorp_module_id module_id)
{
    // Delete the state
    if (_kernel_signal_messages_register.delete_protocol(module_instance_name,
							 module_id)
	< 0) {
	XLOG_ERROR("Cannot delete protocol instance %s with module_id = %d "
		   "from receiving kernel signal messages",
		   module_instance_name.c_str(), module_id);
	return (XORP_ERROR);	// Probably not added before
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::proto_recv:
 * @src_module_instance_name: The module instance name of the module-origin
 * of the message.
 * @src_module_id: The #xorp_module_id of the module-origin of the message.
 * @vif_index: The vif index of the interface to use to send this message.
 * @src: The source address of the message.
 * @dst: The destination address of the message.
 * @ip_ttl: The IP TTL of the message. If it has a negative value,
 * it should be ignored.
 * @ip_tos: The IP TOS of the message. If it has a negative value,
 * it should be ignored.
 * @is_router_alert: If true, set the Router Alert IP option for the IP
 * packet of the outgoung message.
 * @rcvbuf: The data buffer with the message to send.
 * @rcvlen: The data length in @rcvbuf.
 * @error_msg: The error message (if error).
 * 
 * Receive a protocol message from a user-level process, and send it
 * through the kernel.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::proto_recv(const string&	, // src_module_instance_name,
		     xorp_module_id src_module_id,
		     uint32_t vif_index,
		     const IPvX& src, const IPvX& dst,
		     int ip_ttl, int ip_tos, bool is_router_alert,
		     const uint8_t *rcvbuf, size_t rcvlen, string& error_msg)
{
    ProtoComm *proto_comm;
    
    if (! is_up()) {
	error_msg = c_format("MFEA node is not UP");
	return (XORP_ERROR);
    }
    
    // TODO: for now @src_module_id that comes by the
    // upper-layer protocol is used to find-out the ProtoComm entry.
    proto_comm = proto_comm_find_by_module_id(src_module_id);
    if (proto_comm == NULL) {
	error_msg = c_format("Protocol with module ID %u is not registered",
			     XORP_UINT_CAST(src_module_id));
	return (XORP_ERROR);
    }
    
    if (proto_comm->proto_socket_write(vif_index,
				       src, dst,
				       ip_ttl,
				       ip_tos,
				       is_router_alert,
				       rcvbuf,
				       rcvlen,
				       error_msg) < 0) {
	return (XORP_ERROR);
    }
    
    return (XORP_OK);
}

// The function to process incoming messages from the kernel
int
MfeaNode::proto_comm_recv(xorp_module_id dst_module_id,
			  uint32_t vif_index,
			  const IPvX& src, const IPvX& dst,
			  int ip_ttl, int ip_tos, bool is_router_alert,
			  const uint8_t *rcvbuf, size_t rcvlen)
{
    string error_msg;

    XLOG_TRACE(false & is_log_trace(),	// XXX: unconditionally disabled
	       "RX packet for dst_module_name %s: "
	       "vif_index = %d src = %s dst = %s ttl = %d tos = %#x "
	       "router_alert = %d rcvbuf = %p rcvlen = %u",
	       xorp_module_name(family(), dst_module_id), vif_index,
	       cstring(src), cstring(dst), ip_ttl, ip_tos, is_router_alert,
	       rcvbuf, XORP_UINT_CAST(rcvlen));
    
    //
    // Test if we should accept or drop the message
    //
    MfeaVif *mfea_vif = vif_find_by_vif_index(vif_index);
    if (mfea_vif == NULL)
	return (XORP_ERROR);
    ProtoRegister& pr = mfea_vif->proto_register();
    if (! pr.is_registered(dst_module_id))
	return (XORP_ERROR);	// The message is not expected
    
    if (! is_up())
	return (XORP_ERROR);
    
    //
    // Send the message to all interested protocol instances
    //

    list<string>::const_iterator iter;
    for (iter = pr.module_instance_name_list(dst_module_id).begin();
	 iter != pr.module_instance_name_list(dst_module_id).end();
	 ++iter) {
	const string& dst_module_instance_name = *iter;
	proto_send(dst_module_instance_name,
		   dst_module_id,
		   vif_index,
		   src, dst,
		   ip_ttl,
		   ip_tos,
		   is_router_alert,
		   rcvbuf,
		   rcvlen,
		   error_msg);
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::signal_message_recv:
 * @src_module_instance_name: Unused.
 * @src_module_id: The #xorp_module_id module ID of the associated #ProtoComm
 * entry. XXX: in the future it may become irrelevant.
 * @message_type: The message type of the kernel signal
 * (%IGMPMSG_* or %MRT6MSG_*)
 * @vif_index: The vif index of the related interface (message-specific).
 * @src: The source address in the message.
 * @dst: The destination address in the message.
 * @rcvbuf: The data buffer with the additional information in the message.
 * @rcvlen: The data length in @rcvbuf.
 * 
 * Process NOCACHE, WRONGVIF/WRONGMIF, WHOLEPKT, BW_UPCALL signals from the
 * kernel. The signal is sent to all user-level protocols that expect it.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::signal_message_recv(const string&	, // src_module_instance_name,
			      xorp_module_id src_module_id,
			      int message_type,
			      uint32_t vif_index,
			      const IPvX& src, const IPvX& dst,
			      const uint8_t *rcvbuf, size_t rcvlen)
{
    XLOG_TRACE(is_log_trace(),
	       "RX kernel signal: "
	       "message_type = %d vif_index = %d src = %s dst = %s",
	       message_type, vif_index,
	       cstring(src), cstring(dst));

    UNUSED(src_module_id);
    
    if (! is_up())
	return (XORP_ERROR);
    
    //
    // If it is a bandwidth upcall message, parse it now
    //
    if (message_type == MFEA_KERNEL_MESSAGE_BW_UPCALL) {
	//
	// XXX: do we need to check if the kernel supports the
	// bandwidth-upcall mechanism?
	//
	
	//
	// Do the job
	//
	switch (family()) {
	case AF_INET:
	{
#if defined(MRT_ADD_BW_UPCALL) && defined(ENABLE_ADVANCED_MULTICAST_API)
	    size_t from = 0;
	    struct bw_upcall bw_upcall;
	    IPvX src(family()), dst(family());
	    bool is_threshold_in_packets, is_threshold_in_bytes;
	    bool is_geq_upcall, is_leq_upcall;
	    
	    while (rcvlen >= sizeof(bw_upcall)) {
		memcpy(&bw_upcall, rcvbuf + from, sizeof(bw_upcall));
		rcvlen -= sizeof(bw_upcall);
		from += sizeof(bw_upcall);
		
		src.copy_in(bw_upcall.bu_src);
		dst.copy_in(bw_upcall.bu_dst);
		is_threshold_in_packets
		    = bw_upcall.bu_flags & BW_UPCALL_UNIT_PACKETS;
		is_threshold_in_bytes
		    = bw_upcall.bu_flags & BW_UPCALL_UNIT_BYTES;
		is_geq_upcall = bw_upcall.bu_flags & BW_UPCALL_GEQ;
		is_leq_upcall = bw_upcall.bu_flags & BW_UPCALL_LEQ;
		signal_dataflow_message_recv(
		    src,
		    dst,
		    TimeVal(bw_upcall.bu_threshold.b_time),
		    TimeVal(bw_upcall.bu_measured.b_time),
		    bw_upcall.bu_threshold.b_packets,
		    bw_upcall.bu_threshold.b_bytes,
		    bw_upcall.bu_measured.b_packets,
		    bw_upcall.bu_measured.b_bytes,
		    is_threshold_in_packets,
		    is_threshold_in_bytes,
		    is_geq_upcall,
		    is_leq_upcall);
	    }
#endif // MRT_ADD_BW_UPCALL && ENABLE_ADVANCED_MULTICAST_API
	}
	break;
	
#ifdef HAVE_IPV6
	case AF_INET6:
	{
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	XLOG_ERROR("signal_message_recv() failed: "
		   "IPv6 multicast routing not supported");
	return (XORP_ERROR);
#else
	
#if defined(MRT6_ADD_BW_UPCALL) && defined(ENABLE_ADVANCED_MULTICAST_API)
	    size_t from = 0;
	    struct bw6_upcall bw_upcall;
	    IPvX src(family()), dst(family());
	    bool is_threshold_in_packets, is_threshold_in_bytes;
	    bool is_geq_upcall, is_leq_upcall;
	    
	    while (rcvlen >= sizeof(bw_upcall)) {
		memcpy(&bw_upcall, rcvbuf + from, sizeof(bw_upcall));
		rcvlen -= sizeof(bw_upcall);
		from += sizeof(bw_upcall);
		
		src.copy_in(bw_upcall.bu6_src);
		dst.copy_in(bw_upcall.bu6_dsr);
		is_threshold_in_packets
		    = bw_upcall.bu6_flags & BW_UPCALL_UNIT_PACKETS;
		is_threshold_in_bytes
		    = bw_upcall.bu6_flags & BW_UPCALL_UNIT_BYTES;
		is_geq_upcall = bw_upcall.bu6_flags & BW_UPCALL_GEQ;
		is_leq_upcall = bw_upcall.bu6_flags & BW_UPCALL_LEQ;
		signal_dataflow_message_recv(
		    src,
		    dst,
		    TimeVal(bw_upcall.bu6_threshold.b_time),
		    TimeVal(bw_upcall.bu6_measured.b_time),
		    bw_upcall.bu6_threshold.b_packets,
		    bw_upcall.bu6_threshold.b_bytes,
		    bw_upcall.bu6_measured.b_packets,
		    bw_upcall.bu6_measured.b_bytes,
		    is_threshold_in_packets,
		    is_threshold_in_bytes,
		    is_geq_upcall,
		    is_leq_upcall);
	    }
#endif // MRT6_ADD_BW_UPCALL && ENABLE_ADVANCED_MULTICAST_API
#endif // HAVE_IPV6_MULTICAST_ROUTING
	}
	break;
#endif // HAVE_IPV6
	
	default:
	    XLOG_UNREACHABLE();
	    return (XORP_ERROR);
	}
	return (XORP_OK);
    }
    
    //
    // Test if we should accept or drop the message
    //
    MfeaVif *mfea_vif = vif_find_by_vif_index(vif_index);
    if (mfea_vif == NULL)
	return (XORP_ERROR);
    
    //
    // Send the signal to all upper-layer protocols that expect it.
    //
    ProtoRegister& pr = _kernel_signal_messages_register;
    const list<pair<string, xorp_module_id> >& module_list = pr.all_module_instance_name_list();
    
    list<pair<string, xorp_module_id> >::const_iterator iter;
    for (iter = module_list.begin(); iter != module_list.end(); ++iter) {
	const string& dst_module_instance_name = (*iter).first;
	xorp_module_id dst_module_id = (*iter).second;
	signal_message_send(dst_module_instance_name,
			    dst_module_id,
			    message_type,
			    vif_index,
			    src, dst,
			    rcvbuf,
			    rcvlen);
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::signal_dataflow_message_recv:
 * @source: The source address.
 * @group: The group address.
 * @threshold_interval: The dataflow threshold interval.
 * @measured_interval: The dataflow measured interval.
 * @threshold_packets: The threshold (in number of packets) to compare against.
 * @threshold_bytes: The threshold (in number of bytes) to compare against.
 * @measured_packets: The number of packets measured within
 * the @measured_interval.
 * @measured_bytes: The number of bytes measured within
 * the @measured_interval.
 * @is_threshold_in_packets: If true, @threshold_packets is valid.
 * @is_threshold_in_bytes: If true, @threshold_bytes is valid.
 * @is_geq_upcall: If true, the operation for comparison is ">=".
 * @is_leq_upcall: If true, the operation for comparison is "<=".
 * 
 * Process a dataflow upcall from the kernel or from the MFEA internal
 * bandwidth-estimation mechanism (i.e., periodic reading of the kernel
 * multicast forwarding statistics).
 * The signal is sent to all user-level protocols that expect it.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::signal_dataflow_message_recv(const IPvX& source, const IPvX& group,
				       const TimeVal& threshold_interval,
				       const TimeVal& measured_interval,
				       uint32_t threshold_packets,
				       uint32_t threshold_bytes,
				       uint32_t measured_packets,
				       uint32_t measured_bytes,
				       bool is_threshold_in_packets,
				       bool is_threshold_in_bytes,
				       bool is_geq_upcall,
				       bool is_leq_upcall)
{
    XLOG_TRACE(is_log_trace(),
	       "RX dataflow message: "
	       "src = %s dst = %s",
	       cstring(source), cstring(group));
    
    if (! is_up())
	return (XORP_ERROR);
    
    //
    // Send the signal to all upper-layer protocols that expect it.
    //
    ProtoRegister& pr = _kernel_signal_messages_register;
    const list<pair<string, xorp_module_id> >& module_list = pr.all_module_instance_name_list();
    
    list<pair<string, xorp_module_id> >::const_iterator iter;
    for (iter = module_list.begin(); iter != module_list.end(); ++iter) {
	const string& dst_module_instance_name = (*iter).first;
	xorp_module_id dst_module_id = (*iter).second;
	
	dataflow_signal_send(dst_module_instance_name,
			     dst_module_id,
			     source,
			     group,
			     threshold_interval.sec(),
			     threshold_interval.usec(),
			     measured_interval.sec(),
			     measured_interval.usec(),
			     threshold_packets,
			     threshold_bytes,
			     measured_packets,
			     measured_bytes,
			     is_threshold_in_packets,
			     is_threshold_in_bytes,
			     is_geq_upcall,
			     is_leq_upcall);
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::join_multicast_group:
 * @module_instance_name: The module instance name of the protocol to join the
 * multicast group.
 * @module_id: The #xorp_module_id of the protocol to join the multicast
 * group.
 * @vif_index: The vif index of the interface to join.
 * @group: The multicast group to join.
 * 
 * Join a multicast group.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::join_multicast_group(const string& module_instance_name,
			       xorp_module_id module_id,
			       uint32_t vif_index,
			       const IPvX& group)
{
    ProtoComm *proto_comm = proto_comm_find_by_module_id(module_id);
    MfeaVif *mfea_vif = vif_find_by_vif_index(vif_index);
    
    if ((proto_comm == NULL) || (mfea_vif == NULL))
	return (XORP_ERROR);
    
    bool has_group = mfea_vif->has_multicast_group(group);
    
    // Add the state for the group
    if (mfea_vif->add_multicast_group(module_instance_name,
				      module_id,
				      group) < 0) {
	return (XORP_ERROR);
    }
    
    if (! has_group) {
	if (proto_comm->join_multicast_group(vif_index, group) < 0) {
	    mfea_vif->delete_multicast_group(module_instance_name,
					     module_id,
					     group);
	    return (XORP_ERROR);
	}
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::leave_multicast_group:
 * @module_instance_name: The module instance name of the protocol to leave the
 * multicast group.
 * @module_id: The #xorp_module_id of the protocol to leave the multicast
 * group.
 * @vif_index: The vif index of the interface to leave.
 * @group: The multicast group to leave.
 * 
 * Leave a multicast group.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::leave_multicast_group(const string& module_instance_name,
				xorp_module_id module_id,
				uint32_t vif_index,
				const IPvX& group)
{
    ProtoComm *proto_comm = proto_comm_find_by_module_id(module_id);
    MfeaVif *mfea_vif = vif_find_by_vif_index(vif_index);
    
    if ((proto_comm == NULL) || (mfea_vif == NULL))
	return (XORP_ERROR);
    
    // Delete the state for the group
    if (mfea_vif->delete_multicast_group(module_instance_name,
					 module_id,
					 group) < 0) {
	return (XORP_ERROR);
    }
    
    if (! mfea_vif->has_multicast_group(group)) {
	if (proto_comm->leave_multicast_group(vif_index, group) < 0) {
	    return (XORP_ERROR);
	}
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::add_mfc:
 * @module_instance_name: The module instance name of the protocol that adds
 * the MFC.
 * @source: The source address.
 * @group: The group address.
 * @iif_vif_index: The vif index of the incoming interface.
 * @oiflist: The bitset with the outgoing interfaces.
 * @oiflist_disable_wrongvif: The bitset with the outgoing interfaces to
 * disable the WRONGVIF signal.
 * @max_vifs_oiflist: The number of vifs covered by @oiflist
 * or @oiflist_disable_wrongvif.
 * @rp_addr: The RP address.
 * 
 * Add Multicast Forwarding Cache (MFC) to the kernel.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::add_mfc(const string& , // module_instance_name,
		  const IPvX& source, const IPvX& group,
		  uint32_t iif_vif_index, const Mifset& oiflist,
		  const Mifset& oiflist_disable_wrongvif,
		  uint32_t max_vifs_oiflist,
		  const IPvX& rp_addr)
{
    uint8_t oifs_ttl[MAX_VIFS];
    uint8_t oifs_flags[MAX_VIFS];
    
    if (max_vifs_oiflist > MAX_VIFS)
	return (XORP_ERROR);
    
    // Check the iif
    if (iif_vif_index == Vif::VIF_INDEX_INVALID)
	return (XORP_ERROR);
    if (iif_vif_index >= max_vifs_oiflist)
	return (XORP_ERROR);
    
    //
    // Reset the initial values
    //
    for (size_t i = 0; i < MAX_VIFS; i++) {
	oifs_ttl[i] = 0;
	oifs_flags[i] = 0;
    }
    
    //
    // Set the minimum required TTL for each outgoing interface,
    // and the optional flags.
    //
    // TODO: XXX: PAVPAVPAV: the TTL should be configurable per vif.
    for (size_t i = 0; i < max_vifs_oiflist; i++) {
	// Set the TTL
	if (oiflist.test(i))
	    oifs_ttl[i] = MINTTL;
	else
	    oifs_ttl[i] = 0;
	
	// Set the flags
	oifs_flags[i] = 0;
	
	if (oiflist_disable_wrongvif.test(i)) {
	    switch (family()) {
	    case AF_INET:
#if defined(MRT_MFC_FLAGS_DISABLE_WRONGVIF) && defined(ENABLE_ADVANCED_MULTICAST_API)
		oifs_flags[i] |= MRT_MFC_FLAGS_DISABLE_WRONGVIF;
#endif
		break;
		
#ifdef HAVE_IPV6
	    case AF_INET6:
	    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
		XLOG_ERROR("add_mfc() failed: "
			   "IPv6 multicast routing not supported");
		return (XORP_ERROR);
#else
#if defined(MRT6_MFC_FLAGS_DISABLE_WRONGVIF) && defined(ENABLE_ADVANCED_MULTICAST_API)
		oifs_flags[i] |= MRT6_MFC_FLAGS_DISABLE_WRONGVIF;
#endif
#endif // HAVE_IPV6_MULTICAST_ROUTING
	    }
	    break;
#endif // HAVE_IPV6
	    
	    default:
		XLOG_UNREACHABLE();
		return (XORP_ERROR);
	    }
	}
    }
    
    if (_mfea_mrouter.add_mfc(source, group, iif_vif_index, oifs_ttl,
			      oifs_flags, rp_addr)
	< 0) {
	return (XORP_ERROR);
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::delete_mfc:
 * @module_instance_name: The module instance name of the protocol that deletes
 * the MFC.
 * @source: The source address.
 * @group: The group address.
 * 
 * Delete Multicast Forwarding Cache (MFC) from the kernel.
 * XXX: All corresponding dataflow entries are also removed.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::delete_mfc(const string& , // module_instance_name,
		     const IPvX& source, const IPvX& group)
{
    if (_mfea_mrouter.delete_mfc(source, group) < 0) {
	return (XORP_ERROR);
    }
    
    //
    // XXX: Remove all corresponding dataflow entries
    //
    mfea_dft().delete_entry(source, group);
    
    return (XORP_OK);
}

/**
 * MfeaNode::add_dataflow_monitor:
 * @module_instance_name: The module instance name of the protocol that adds
 * the dataflow monitor entry.
 * @source: The source address.
 * @group: The group address.
 * @threshold_interval: The dataflow threshold interval.
 * @threshold_packets: The threshold (in number of packets) to compare against.
 * @threshold_bytes: The threshold (in number of bytes) to compare against.
 * @is_threshold_in_packets: If true, @threshold_packets is valid.
 * @is_threshold_in_bytes: If true, @threshold_bytes is valid.
 * @is_geq_upcall: If true, the operation for comparison is ">=".
 * @is_leq_upcall: If true, the operation for comparison is "<=".
 * @error_msg: The error message (if error).
 * 
 * Add a dataflow monitor entry.
 * Note: either @is_threshold_in_packets or @is_threshold_in_bytes (or both)
 * must be true.
 * Note: either @is_geq_upcall or @is_leq_upcall (but not both) must be true.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::add_dataflow_monitor(const string& ,	// module_instance_name,
			       const IPvX& source, const IPvX& group,
			       const TimeVal& threshold_interval,
			       uint32_t threshold_packets,
			       uint32_t threshold_bytes,
			       bool is_threshold_in_packets,
			       bool is_threshold_in_bytes,
			       bool is_geq_upcall,
			       bool is_leq_upcall,
			       bool rolling,
			       string& error_msg)
{
    // XXX: flags is_geq_upcall and is_leq_upcall are mutually exclusive
    if (! (is_geq_upcall ^ is_leq_upcall)) {
	error_msg = c_format("Cannot add dataflow monitor for (%s, %s): "
			     "the GEQ and LEQ flags are mutually exclusive "
			     "(GEQ = %s; LEQ = %s)",
			     cstring(source), cstring(group),
			     (is_geq_upcall)? "true" : "false",
			     (is_leq_upcall)? "true" : "false");
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);		// Invalid arguments
    }
    // XXX: at least one of the threshold flags must be set
    if (! (is_threshold_in_packets || is_threshold_in_bytes)) {
	error_msg = c_format("Cannot add dataflow monitor for (%s, %s): "
			     "invalid threshold flags "
			     "(is_threshold_in_packets = %s; "
			     "is_threshold_in_bytes = %s)",
			     cstring(source), cstring(group),
			     (is_threshold_in_packets)? "true" : "false",
			     (is_threshold_in_bytes)? "true" : "false");
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);		// Invalid arguments
    }
    
    //
    // If the kernel supports bandwidth-related upcalls, use it
    //
    if (_mfea_mrouter.mrt_api_mrt_mfc_bw_upcall()) {
	if (_mfea_mrouter.add_bw_upcall(source, group,
					threshold_interval,
					threshold_packets,
					threshold_bytes,
					is_threshold_in_packets,
					is_threshold_in_bytes,
					is_geq_upcall,
					is_leq_upcall,
					error_msg)
	    < 0) {
	    return (XORP_ERROR);
	}
	return (XORP_OK);
    }
    
    //
    // The kernel doesn't support bandwidth-related upcalls, hence use
    // a work-around mechanism (periodic quering).
    //
    if (mfea_dft().add_entry(source, group,
			     threshold_interval,
			     threshold_packets,
			     threshold_bytes,
			     is_threshold_in_packets,
			     is_threshold_in_bytes,
			     is_geq_upcall,
			     is_leq_upcall,
			     rolling,
			     error_msg)
	< 0) {
	return (XORP_ERROR);
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::delete_dataflow_monitor:
 * @module_instance_name: The module instance name of the protocol that deletes
 * the dataflow monitor entry.
 * @source: The source address.
 * @group: The group address.
 * @threshold_interval: The dataflow threshold interval.
 * @threshold_packets: The threshold (in number of packets) to compare against.
 * @threshold_bytes: The threshold (in number of bytes) to compare against.
 * @is_threshold_in_packets: If true, @threshold_packets is valid.
 * @is_threshold_in_bytes: If true, @threshold_bytes is valid.
 * @is_geq_upcall: If true, the operation for comparison is ">=".
 * @is_leq_upcall: If true, the operation for comparison is "<=".
 * @error_msg: The error message (if error).
 * 
 * Delete a dataflow monitor entry.
 * Note: either @is_threshold_in_packets or @is_threshold_in_bytes (or both)
 * must be true.
 * Note: either @is_geq_upcall or @is_leq_upcall (but not both) must be true.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::delete_dataflow_monitor(const string& , // module_instance_name,
				  const IPvX& source, const IPvX& group,
				  const TimeVal& threshold_interval,
				  uint32_t threshold_packets,
				  uint32_t threshold_bytes,
				  bool is_threshold_in_packets,
				  bool is_threshold_in_bytes,
				  bool is_geq_upcall,
				  bool is_leq_upcall,
				  bool rolling,
				  string& error_msg)
{
    // XXX: flags is_geq_upcall and is_leq_upcall are mutually exclusive
    if (! (is_geq_upcall ^ is_leq_upcall)) {
	error_msg = c_format("Cannot delete dataflow monitor for (%s, %s): "
			     "the GEQ and LEQ flags are mutually exclusive "
			     "(GEQ = %s; LEQ = %s)",
			     cstring(source), cstring(group),
			     (is_geq_upcall)? "true" : "false",
			     (is_leq_upcall)? "true" : "false");
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);		// Invalid arguments
    }
    // XXX: at least one of the threshold flags must be set
    if (! (is_threshold_in_packets || is_threshold_in_bytes)) {
	error_msg = c_format("Cannot delete dataflow monitor for (%s, %s): "
			     "invalid threshold flags "
			     "(is_threshold_in_packets = %s; "
			     "is_threshold_in_bytes = %s)",
			     cstring(source), cstring(group),
			     (is_threshold_in_packets)? "true" : "false",
			     (is_threshold_in_bytes)? "true" : "false");
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);		// Invalid arguments
    }
    
    //
    // If the kernel supports bandwidth-related upcalls, use it
    //
    if (_mfea_mrouter.mrt_api_mrt_mfc_bw_upcall()) {
	if (_mfea_mrouter.delete_bw_upcall(source, group,
					   threshold_interval,
					   threshold_packets,
					   threshold_bytes,
					   is_threshold_in_packets,
					   is_threshold_in_bytes,
					   is_geq_upcall,
					   is_leq_upcall,
					   error_msg)
	    < 0) {
	    return (XORP_ERROR);
	}
	return (XORP_OK);
    }
    
    //
    // The kernel doesn't support bandwidth-related upcalls, hence use
    // a work-around mechanism (periodic quering).
    //
    if (mfea_dft().delete_entry(source, group,
				threshold_interval,
				threshold_packets,
				threshold_bytes,
				is_threshold_in_packets,
				is_threshold_in_bytes,
				is_geq_upcall,
				is_leq_upcall,
				rolling,
				error_msg)
	< 0) {
	return (XORP_ERROR);
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::delete_all_dataflow_monitor:
 * @module_instance_name: The module instance name of the protocol that deletes
 * the dataflow monitor entry.
 * @source: The source address.
 * @group: The group address.
 * @error_msg: The error message (if error).
 * 
 * Delete all dataflow monitor entries for a given @source and @group address.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::delete_all_dataflow_monitor(const string& , // module_instance_name,
				      const IPvX& source, const IPvX& group,
				      string& error_msg)
{
    //
    // If the kernel supports bandwidth-related upcalls, use it
    //
    if (_mfea_mrouter.mrt_api_mrt_mfc_bw_upcall()) {
	if (_mfea_mrouter.delete_all_bw_upcall(source, group, error_msg) < 0) {
	    return (XORP_ERROR);
	}
	return (XORP_OK);
    }
    
    //
    // The kernel doesn't support bandwidth-related upcalls, hence use
    // a work-around mechanism (periodic quering).
    //
    if (mfea_dft().delete_entry(source, group) < 0) {
	error_msg = c_format("Cannot delete dataflow monitor for (%s, %s): "
			     "no such entry",
			     cstring(source), cstring(group));
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::add_multicast_vif:
 * @vif_index: The vif index of the interface to add.
 * 
 * Add a multicast vif to the kernel.
 * 
 * Return value: %XORP_OK on success, othewise %XORP_ERROR.
 **/
int
MfeaNode::add_multicast_vif(uint32_t vif_index)
{
    if (_mfea_mrouter.add_multicast_vif(vif_index) < 0) {
	return (XORP_ERROR);
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::delete_multicast_vif:
 * @vif_index: The vif index of the interface to delete.
 * 
 * Delete a multicast vif from the kernel.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::delete_multicast_vif(uint32_t vif_index)
{
    if (_mfea_mrouter.delete_multicast_vif(vif_index) < 0) {
	return (XORP_ERROR);
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::get_sg_count:
 * @source: The MFC source address.
 * @group: The MFC group address.
 * @sg_count: A reference to a #SgCount class to place the result: the
 * number of packets and bytes forwarded by the particular MFC entry, and the
 * number of packets arrived on a wrong interface.
 * 
 * Get the number of packets and bytes forwarded by a particular
 * Multicast Forwarding Cache (MFC) entry in the kernel, and the number
 * of packets arrived on wrong interface for that entry.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::get_sg_count(const IPvX& source, const IPvX& group,
		       SgCount& sg_count)
{
    if (_mfea_mrouter.get_sg_count(source, group, sg_count) < 0) {
	return (XORP_ERROR);
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::get_vif_count:
 * @vif_index: The vif index of the virtual multicast interface whose
 * statistics we need.
 * @vif_count: A reference to a #VifCount class to store the result.
 * 
 * Get the number of packets and bytes received on, or forwarded on
 * a particular multicast interface.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaNode::get_vif_count(uint32_t vif_index, VifCount& vif_count)
{
    if (_mfea_mrouter.get_vif_count(vif_index, vif_count) < 0) {
	return (XORP_ERROR);
    }
    
    return (XORP_OK);
}

/**
 * MfeaNode::proto_comm_find_by_module_id:
 * @module_id: The #xorp_module_id to search for.
 * 
 * Return the #ProtoComm entry that corresponds to @module_id.
 * 
 * Return value: The corresponding #ProtoComm entry if found, otherwise NULL.
 **/
ProtoComm *
MfeaNode::proto_comm_find_by_module_id(xorp_module_id module_id) const
{
    for (size_t i = 0; i < _proto_comms.size(); i++) {
	if (_proto_comms[i] != NULL) {
	    if (_proto_comms[i]->module_id() == module_id)
		return (_proto_comms[i]);
	}
    }
    
    return (NULL);
}

/**
 * MfeaNode::proto_comm_find_by_ip_protocol:
 * @ip_protocol: The IP protocol number to search for.
 * 
 * Return the #ProtoComm entry that corresponds to @ip_protocol IP protocol
 * number.
 * 
 * Return value: The corresponding #ProtoComm entry if found, otherwise NULL.
 **/
ProtoComm *
MfeaNode::proto_comm_find_by_ip_protocol(int ip_protocol) const
{
    for (size_t i = 0; i < _proto_comms.size(); i++) {
	if (_proto_comms[i] != NULL) {
	    if (_proto_comms[i]->ip_protocol() == ip_protocol)
		return (_proto_comms[i]);
	}
    }
    
    return (NULL);
}
