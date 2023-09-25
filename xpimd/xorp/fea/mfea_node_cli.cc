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

#ident "$XORP: xorp/fea/mfea_node_cli.cc,v 1.22 2007/02/16 22:45:46 pavlin Exp $"

//
// MFEA (Multicast Forwarding Engine Abstraction) CLI implementation
//

#include "mfea_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"
#include "libxorp/debug.h"
#include "libxorp/ipvx.hh"

#include "mfea_node.hh"
#include "mfea_node_cli.hh"
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
 * MfeaNodeCli::MfeaNodeCli:
 * @mfea_node: The MFEA node to use.
 * 
 * MfeaNodeCli constructor.
 **/
MfeaNodeCli::MfeaNodeCli(MfeaNode& mfea_node)
    : ProtoNodeCli(mfea_node.family(), mfea_node.module_id()),
      _mfea_node(mfea_node)
{
    
}

MfeaNodeCli::~MfeaNodeCli()
{
    stop();
}

int
MfeaNodeCli::start()
{
    if (! is_enabled())
	return (XORP_OK);

    if (is_up() || is_pending_up())
	return (XORP_OK);

    if (ProtoUnit::start() < 0)
	return (XORP_ERROR);

    if (add_all_cli_commands() < 0)
	return (XORP_ERROR);

    XLOG_INFO(mfea_node().is_log_info(), "CLI started");

    return (XORP_OK);
}

int
MfeaNodeCli::stop()
{
    int ret_code = XORP_OK;

    if (is_down())
	return (XORP_OK);

    if (ProtoUnit::stop() < 0)
	return (XORP_ERROR);

    if (delete_all_cli_commands() < 0)
	ret_code = XORP_ERROR;

    XLOG_INFO(mfea_node().is_log_info(), "CLI stopped");

    return (ret_code);
}

/**
 * Enable the node operation.
 * 
 * If an unit is not enabled, it cannot be start, or pending-start.
 */
void
MfeaNodeCli::enable()
{
    ProtoUnit::enable();

    XLOG_INFO(mfea_node().is_log_info(), "CLI enabled");
}

/**
 * Disable the node operation.
 * 
 * If an unit is disabled, it cannot be start or pending-start.
 * If the unit was runnning, it will be stop first.
 */
void
MfeaNodeCli::disable()
{
    stop();
    ProtoUnit::disable();

    XLOG_INFO(mfea_node().is_log_info(), "CLI disabled");
}

int
MfeaNodeCli::add_all_cli_commands()
{
    // XXX: command "show" must have been installed by the CLI itself.

    if (mfea_node().is_ipv4()) {
	add_cli_dir_command("show mfea",
			    "Display information about IPv4 MFEA");

	add_cli_command("show mfea dataflow",
			"Display information about MFEA IPv4 dataflow filters",
			callback(this, &MfeaNodeCli::cli_show_mfea_dataflow));

	add_cli_command("show mfea interface",
			"Display information about MFEA IPv4 interfaces",
			callback(this, &MfeaNodeCli::cli_show_mfea_interface));

	add_cli_command("show mfea interface address",
			"Display information about addresses of MFEA IPv4 interfaces",
			callback(this, &MfeaNodeCli::cli_show_mfea_interface_address));
    }

    if (mfea_node().is_ipv6()) {
	add_cli_dir_command("show mfea6",
			    "Display information about IPv6 MFEA");

	add_cli_command("show mfea6 dataflow",
			"Display information about MFEA IPv6 dataflow filters",
			callback(this, &MfeaNodeCli::cli_show_mfea_dataflow));

	add_cli_command("show mfea6 interface",
			"Display information about MFEA IPv6 interfaces",
			callback(this, &MfeaNodeCli::cli_show_mfea_interface));

	add_cli_command("show mfea6 interface address",
			"Display information about addresses of MFEA IPv6 interfaces",
			callback(this, &MfeaNodeCli::cli_show_mfea_interface_address));
    }

    return (XORP_OK);
}

//
// CLI COMMAND: "show mfea dataflow [group | group-range]"
// CLI COMMAND: "show mfea6 dataflow [group | group-range]"
//
// Display information about the dataflow filters the MFEA knows about.
//
int
MfeaNodeCli::cli_show_mfea_dataflow(const vector<string>& argv)
{
    IPvXNet group_range = IPvXNet::ip_multicast_base_prefix(family());
    
    // Check the optional argument
    if (argv.size()) {
	try {
	    group_range = IPvXNet(argv[0].c_str());
	} catch (InvalidString) {
	    try {
		group_range = IPvXNet(IPvX(argv[0].c_str()),
				      IPvX::addr_bitlen(family()));
	    } catch (InvalidString) {
		cli_print(c_format("ERROR: Invalid group range address: %s\n",
				   argv[0].c_str()));
		return (XORP_ERROR);
	    } catch (InvalidNetmaskLength) {
		XLOG_UNREACHABLE();
		return (XORP_ERROR);
	    }
	} catch (InvalidNetmaskLength) {
	    cli_print(c_format("ERROR: Invalid group range netmask length: %s\n",
			       argv[0].c_str()));
	    return (XORP_ERROR);
	}
	if (! group_range.is_multicast()) {
	    cli_print(c_format("ERROR: Group range is not multicast: %s\n",
			       cstring(group_range)));
	    return (XORP_ERROR);
	}
    }
    
    cli_print(c_format("%-39s %-39s\n",
		       "Group", "Source"));
    
    //
    // The MfeaDfe entries
    //
    MfeaDft::const_gs_iterator iter_dft, iter_begin_dft, iter_end_dft;
    iter_begin_dft = mfea_node().mfea_dft().group_by_prefix_begin(group_range);
    iter_end_dft = mfea_node().mfea_dft().group_by_prefix_end(group_range);
    TimeVal now;
    
    mfea_node().eventloop().current_time(now);
    
    for (iter_dft = iter_begin_dft; iter_dft != iter_end_dft; ++iter_dft) {
	MfeaDfeLookup *mfea_dfe_lookup = iter_dft->second;
	list<MfeaDfe *>::const_iterator iter;
	
	cli_print(c_format("%-39s %-39s\n",
			   cstring(mfea_dfe_lookup->group_addr()),
			   cstring(mfea_dfe_lookup->source_addr())));
	cli_print(c_format("  %-29s %-4s %-30s %-6s\n",
			   "Measured(Start|Packets|Bytes)",
			   "Type",
			   "Thresh(Interval|Packets|Bytes)",
			   "Remain"));
	
	for (iter = mfea_dfe_lookup->mfea_dfe_list().begin();
	     iter != mfea_dfe_lookup->mfea_dfe_list().end();
	     ++iter) {
	    string s1, s2;
	    string measured_s, type_s, thresh_s, remain_s;
	    MfeaDfe *mfea_dfe = *iter;
	    TimeVal start_time, threshold_interval, end, delta;
	    
	    start_time = mfea_dfe->start_time();
	    threshold_interval = mfea_dfe->threshold_interval();
	    
	    // The measured values
	    if (mfea_dfe->is_threshold_in_packets()) {
		s1 = c_format("%u",
			      XORP_UINT_CAST(mfea_dfe->measured_packets()));
	    } else {
		s1 = "?";
	    }
	    if (mfea_dfe->is_threshold_in_bytes()) {
		s2 = c_format("%u",
			      XORP_UINT_CAST(mfea_dfe->measured_bytes()));
	    } else {
		s2 = "?";
	    }
	    measured_s = c_format("%u.%u|%s|%s",
				  XORP_UINT_CAST(start_time.sec()),
				  XORP_UINT_CAST(start_time.usec()),
				  s1.c_str(), s2.c_str());
	    
	    // The entry type
	    type_s = c_format("%-3s",
			      mfea_dfe->is_geq_upcall()? ">="
			      : mfea_dfe->is_leq_upcall()? "<="
			      : "?");
	    
	    // The threshold values
	    if (mfea_dfe->is_threshold_in_packets())
		s1 = c_format("%u",
			      XORP_UINT_CAST(mfea_dfe->threshold_packets()));
	    else
		s1 = "?";
	    if (mfea_dfe->is_threshold_in_bytes())
		s2 = c_format("%u",
			      XORP_UINT_CAST(mfea_dfe->threshold_bytes()));
	    else
		s2 = "?";
	    thresh_s = c_format("%u.%u|%s|%s",
				XORP_UINT_CAST(threshold_interval.sec()),
				XORP_UINT_CAST(threshold_interval.usec()),
				s1.c_str(), s2.c_str());
	    
	    // Remaining time
	    end = start_time + threshold_interval;
	    if (now <= end) {
		delta = end - now;
		remain_s = c_format("%u.%u",
				    XORP_UINT_CAST(delta.sec()),
				    XORP_UINT_CAST(delta.usec()));
	    } else {
		// Negative time
		delta = now - end;
		remain_s = c_format("-%u.%u",
				    XORP_UINT_CAST(delta.sec()),
				    XORP_UINT_CAST(delta.usec()));
	    }
	    
	    cli_print(c_format("  %-29s %-6s %-30s %-6s\n",
			       measured_s.c_str(), type_s.c_str(), 
			       thresh_s.c_str(), remain_s.c_str()));
	}
    }
    
    return (XORP_OK);
}

//
// CLI COMMAND: "show mfea interface [interface-name]"
// CLI COMMAND: "show mfea6 interface [interface-name]"
//
// Display information about the interfaces the MFEA knows about.
//
int
MfeaNodeCli::cli_show_mfea_interface(const vector<string>& argv)
{
    string interface_name;
    
    // Check the optional argument
    if (argv.size()) {
	interface_name = argv[0];
	if (mfea_node().vif_find_by_name(interface_name) == NULL) {
	    cli_print(c_format("ERROR: Invalid interface name: %s\n",
			       interface_name.c_str()));
	    return (XORP_ERROR);
	}
    }
    
    cli_print(c_format("%-12s %-8s %12s %-15s %-1s\n",
		       "Interface", "State", "Vif/PifIndex", "Addr", "Flags"));
    for (uint32_t i = 0; i < mfea_node().maxvifs(); i++) {
	MfeaVif *mfea_vif = mfea_node().vif_find_by_vif_index(i);
	if (mfea_vif == NULL)
	    continue;
	// Test if we should print this entry
	bool do_print = true;
	if (interface_name.size()) {
	    do_print = false;
	    if (mfea_vif->name() == interface_name) {
		do_print = true;
	    }
	}
	if (! do_print)
	    continue;
	
	//
	// Create a string with the interface flags
	//
	string vif_flags = "";
	if (mfea_vif->is_pim_register()) {
	    if (vif_flags.size())
		vif_flags += " ";
	    vif_flags += "PIM_REGISTER";
	}
	if (mfea_vif->is_p2p()) {
	    if (vif_flags.size())
		vif_flags += " ";
	    vif_flags += "P2P";
	}
	if (mfea_vif->is_loopback()) {
	    if (vif_flags.size())
		vif_flags += " ";
	    vif_flags += "LOOPBACK";
	}
	if (mfea_vif->is_multicast_capable()) {
	    if (vif_flags.size())
		vif_flags += " ";
	    vif_flags += "MULTICAST";
	}
	if (mfea_vif->is_broadcast_capable()) {
	    if (vif_flags.size())
		vif_flags += " ";
	    vif_flags += "BROADCAST";
	}
	if (mfea_vif->is_underlying_vif_up()) {
	    if (vif_flags.size())
		vif_flags += " ";
	    vif_flags += "KERN_UP";
	}
	
	//
	// Print the interface
	//
	list<VifAddr>::const_iterator iter = mfea_vif->addr_list().begin();
	string dd = c_format("%d/%d", mfea_vif->vif_index(),
			     mfea_vif->pif_index());
	cli_print(c_format("%-12s %-8s %12s %-15s %-1s\n",
			   mfea_vif->name().c_str(),
			   mfea_vif->state_str().c_str(),
			   dd.c_str(),
			   (iter != mfea_vif->addr_list().end())?
			   cstring((*iter).addr()): "",
			   vif_flags.c_str()));
    }
    
    return (XORP_OK);
}

//
// CLI COMMAND: "show mfea interface address [interface-name]"
// CLI COMMAND: "show mfea6 interface address [interface-name]"
//
// Display information about the addresses of interfaces the MFEA knows about.
//
int
MfeaNodeCli::cli_show_mfea_interface_address(const vector<string>& argv)
{
    string interface_name;
    
    // Check the optional argument
    if (argv.size()) {
	interface_name = argv[0];
	if (mfea_node().vif_find_by_name(interface_name) == NULL) {
	    cli_print(c_format("ERROR: Invalid interface name: %s\n",
			       interface_name.c_str()));
	    return (XORP_ERROR);
	}
    }
    
    cli_print(c_format("%-12s %-15s %-18s %-15s %-15s\n",
		       "Interface", "Addr", "Subnet", "Broadcast", "P2Paddr"));
    for (uint32_t i = 0; i < mfea_node().maxvifs(); i++) {
	MfeaVif *mfea_vif = mfea_node().vif_find_by_vif_index(i);
	if (mfea_vif == NULL)
	    continue;
	// Test if we should print this entry
	bool do_print = true;
	if (interface_name.size()) {
	    do_print = false;
	    if (mfea_vif->name() == interface_name) {
		do_print = true;
	    }
	}
	if (! do_print)
	    continue;
	
	//
	// Print the first address
	//
	list<VifAddr>::const_iterator iter = mfea_vif->addr_list().begin();
	cli_print(c_format("%-12s %-15s %-18s %-15s %-15s\n",
			   mfea_vif->name().c_str(),
			   (iter != mfea_vif->addr_list().end())?
			   cstring((*iter).addr()): "",
			   (iter != mfea_vif->addr_list().end())?
			   cstring((*iter).subnet_addr()): "",
			   (iter != mfea_vif->addr_list().end())?
			   cstring((*iter).broadcast_addr()): "",
			   (iter != mfea_vif->addr_list().end())?
			   cstring((*iter).peer_addr()): ""));
	//
	// Print the rest of the addresses
	//
	if (iter != mfea_vif->addr_list().end())
	    ++iter;
	for ( ; iter != mfea_vif->addr_list().end(); ++iter) {
	    cli_print(c_format("%-12s %-15s %-18s %-15s %-15s\n",
			       " ",
			       cstring((*iter).addr()),
			       cstring((*iter).subnet_addr()),
			       cstring((*iter).broadcast_addr()),
			       cstring((*iter).peer_addr())));
	}
    }
    
    return (XORP_OK);
}
