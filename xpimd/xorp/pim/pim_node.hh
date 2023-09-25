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

// $XORP: xorp/pim/pim_node.hh,v 1.63 2007/02/16 22:46:49 pavlin Exp $


#ifndef __PIM_PIM_NODE_HH__
#define __PIM_PIM_NODE_HH__


//
// PIM node definition.
//
#include "libxorp/xorp.h"

#include <vector>

#include "libxorp/vif.hh"
#include "libproto/proto_node.hh"
#include "mrt/buffer.h"
#include "mrt/mifset.hh"
#include "pim_bsr.hh"
#include "pim_mrib_table.hh"
#include "pim_mrt.hh"
#include "pim_rp.hh"
#include "pim_scope_zone_table.hh"
#include "pim_vif.hh"


//
// Constants definitions
//


//
// Structures/classes, typedefs and macros
//

class EventLoop;
class IPvX;
class IPvXNet;
class PimMrt;
class PimNbr;
class PimVif;

/**
 * @short The PIM node class.
 * 
 * There should be one node per PIM instance. There should be
 * one instance per address family.
 */
class PimNode : public ProtoNode<PimVif>, public ServiceChangeObserverBase {
public:
    /**
     * Constructor for a given address family, module ID, and event loop.
     * 
     * @param family the address family (AF_INET or AF_INET6 for
     * IPv4 and IPv6 respectively).
     * @param module_id the module ID (@ref xorp_module_id). Should be
     * XORP_MODULE_PIMSM Note: if/after PIM-DM is implemented,
     * XORP_MODULE_PIMDM would be allowed as well.
     * @param eventloop the event loop to use.
     */
    PimNode(int family, xorp_module_id module_id, EventLoop& eventloop);
    
    /**
     * Destructor
     */
    virtual	~PimNode();
    
    /**
     * Start the node operation.
     * 
     * Start the PIM protocol.
     * After the startup operations are completed,
     * @ref PimNode::final_start() is called internally
     * to complete the job.
     * 
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		start();
    
    /**
     * Stop the node operation.
     * 
     * Gracefully stop the PIM protocol.
     * The graceful stop will attempt to send Join/Prune, Assert, etc.
     * messages for all multicast routing entries to gracefully clean-up
     * state with neighbors.
     * After the multicast routing entries cleanup is completed,
     * @ref PimNode::final_stop() is called internally to complete the job.
     * This method, unlike start(), will stop the protocol
     * operation on all interfaces.
     * 
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		stop();
    
    /**
     * Completely start the node operation.
     * 
     * This method should be called internally after @ref PimNode::start()
     * to complete the job.
     * 
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		final_start();

    /**
     * Completely stop the node operation.
     * 
     * This method should be called internally after @ref PimNode::stop()
     * to complete the job.
     * 
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		final_stop();

    /**
     * Enable node operation.
     * 
     * If an unit is not enabled, it cannot be start, or pending-start.
     */
    void	enable();
    
    /**
     * Disable node operation.
     * 
     * If an unit is disabled, it cannot be start or pending-start.
     * If the unit was runnning, it will be stop first.
     */
    void	disable();

    /**
     * Install a new PIM vif.
     * 
     * @param vif vif information about the new PimVif to install.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
#ifdef QUAGGA_MULTICAST
    virtual
#endif	// QUAGGA_MULTICAST
    int		add_vif(const Vif& vif, string& error_msg);
    
    /**
     * Install a new PIM vif.
     * 
     * @param vif_name the name of the new vif.
     * @param vif_index the vif index of the new vif.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
#ifdef QUAGGA_MULTCIAST
    virtual
#endif	// QUAGGA_MULTICAST
    int		add_vif(const string& vif_name, uint32_t vif_index,
			string& error_msg);
    
    /**
     * Delete an existing PIM vif.
     * 
     * @param vif_name the name of the vif to delete.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
#ifdef QUAGGA_MULTICAST
    virtual
#endif	// QUAGGA_MULTICAST
    int		delete_vif(const string& vif_name, string& error_msg);
    
    /**
     * Set flags to a vif.
     * 
     * @param vif_name the name of the vif.
     * @param is_pim_register true if this is a PIM Register vif.
     * @param is_p2p true if this is a point-to-point vif.
     * @param is_loopback true if this is a loopback interface.
     * @param is_multicast rue if the vif is multicast-capable.
     * @param is_broadcast true if the vif is broadcast-capable.
     * @param is_up true if the vif is UP and running.
     * @param mtu the MTU of the vif.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
#ifdef QUAGGA_MULTICAST
    virtual
#endif	// QUAGGA_MULTICAST
    int		set_vif_flags(const string& vif_name,
			      bool is_pim_register, bool is_p2p,
			      bool is_loopback, bool is_multicast,
			      bool is_broadcast, bool is_up, uint32_t mtu,
			      string& error_msg);
    
    /**
     * Add a new address to a vif, or update an existing address.
     * 
     * @param vif_name the name of the vif.
     * @param addr the unicast address to add.
     * @param subnet_addr the subnet address to add.
     * @param broadcast_addr the broadcast address (when applicable).
     * @param peer_addr the peer address (when applicable).
     * @param should_send_pim_hello a return-by-reference flag that is set to
     * true if the caller should send a PIM Hello message.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
#ifdef QUAGGA_MULTICAST
    virtual
#endif	// QUAGGA_MULTICAST
    int		add_vif_addr(const string& vif_name,
			     const IPvX& addr,
			     const IPvXNet& subnet_addr,
			     const IPvX& broadcast_addr,
			     const IPvX& peer_addr,
			     bool& should_send_pim_hello,
			     string& error_msg);
    
    /**
     * Delete an address from a vif.
     * 
     * @param vif_name the name of the vif.
     * @param addr the unicast address to delete.
     * @param should_send_pim_hello a return-by-reference flag that is set to
     * true if the caller should send a PIM Hello message.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
#ifdef QUAGGA_MULTICAST
    virtual
#endif	// QUAGGA_MULTICAST
    int		delete_vif_addr(const string& vif_name,
				const IPvX& addr,
				bool& should_send_pim_hello,
				string& error_msg);
    
    /**
     * Enable an existing PIM vif.
     * 
     * @param vif_name the name of the vif to enable.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		enable_vif(const string& vif_name, string& error_msg);

    /**
     * Disable an existing PIM vif.
     * 
     * @param vif_name the name of the vif to disable.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		disable_vif(const string& vif_name, string& error_msg);

    /**
     * Start an existing PIM vif.
     * 
     * @param vif_name the name of the vif to start.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		start_vif(const string& vif_name, string& error_msg);
    
    /**
     * Stop an existing PIM vif.
     * 
     * @param vif_name the name of the vif to start.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		stop_vif(const string& vif_name, string& error_msg);
    
    /**
     * Start PIM on all enabled interfaces.
     * 
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		start_all_vifs();
    
    /**
     * Stop PIM on all interfaces it was running on.
     * 
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		stop_all_vifs();
    
    /**
     * Enable PIM on all interfaces.
     * 
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		enable_all_vifs();
    
    /**
     * Disable PIM on all interfaces.
     * 
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		disable_all_vifs();
    
    /**
     * Delete all PIM vifs.
     */
    void	delete_all_vifs();

    /**
     * A method called when a vif has completed its shutdown.
     * 
     * @param vif_name the name of the vif that has completed its shutdown.
     */
    void	vif_shutdown_completed(const string& vif_name);

    /**
     * Receive a protocol message.
     * 
     * @param src_module_instance_name the module instance name of the
     * module-origin of the message.
     * 
     * @param src_module_id the module ID (@ref xorp_module_id) of the
     * module-origin of the message.
     * 
     * @param vif_index the vif index of the interface used to receive this
     * message.
     * 
     * @param src the source address of the message.
     * 
     * @param dst the destination address of the message.
     * 
     * @param ip_ttl the IP TTL of the message. If it has a negative value,
     * it should be ignored.
     * 
     * @param ip_tos the IP TOS of the message. If it has a negative value,
     * it should be ignored.
     * 
     * @param is_router_alert if true, the IP Router Alert option in
     * the IP packet was set (when applicable).
     * 
     * @param rcvbuf the data buffer with the received message.
     * 
     * @param rcvlen the data length in @ref rcvbuf.
     * 
     * @param error_msg the error message (if error).
     * 
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		proto_recv(const string& src_module_instance_name,
			   xorp_module_id src_module_id,
			   uint32_t vif_index,
			   const IPvX& src, const IPvX& dst,
			   int ip_ttl, int ip_tos, bool is_router_alert,
			   const uint8_t *rcvbuf, size_t rcvlen,
			   string& error_msg);
    
    /**
     * Send a protocol message.
     * 
     * Note: this method uses the pure virtual ProtoNode::proto_send() method
     * that is implemented somewhere else (in a class that inherits this one).
     * 
     * @param vif_index the vif index of the vif to send the message.
     * @param src the source address of the message.
     * @param dst the destination address of the message.
     * @param ip_ttl the TTL of the IP packet to send. If it has a
     * negative value, the TTL will be set by the lower layers.
     * @param ip_tos the TOS of the IP packet to send. If it has a
     * negative value, the TOS will be set by the lower layers.
     * @param is_router_alert if true, set the IP Router Alert option in
     * the IP packet to send (when applicable).
     * @param buffer the data buffer with the message to send.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		pim_send(uint32_t vif_index,
			 const IPvX& src, const IPvX& dst,
			 int ip_ttl, int ip_tos, bool is_router_alert,
			 buffer_t *buffer, string& error_msg);
    
    /**
     * Receive a signal message from the kernel.
     * 
     * @param src_module_instance_name the module instance name of the
     * module-origin of the message.
     * 
     * @param src_module_id the module ID (@ref xorp_module_id) of the
     * module-origin of the message.
     * 
     * @param message_type the message type.
     * Currently, the type of messages received from the kernel are:
     * 
<pre>
#define MFEA_KERNEL_MESSAGE_NOCACHE        1
#define MFEA_KERNEL_MESSAGE_WRONGVIF       2
#define MFEA_KERNEL_MESSAGE_WHOLEPKT       3
</pre>
     * 
     * 
     * @param vif_index the vif index of the related interface
     * (message-specific relation).
     * 
     * @param src the source address in the message.
     * 
     * @param dst the destination address in the message.
     * 
     * @param rcvbuf the data buffer with the additional information in
     * the message.
     * @param rcvlen the data length in @ref rcvbuf.
     * 
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		signal_message_recv(const string& src_module_instance_name,
				    xorp_module_id src_module_id,
				    int message_type,
				    uint32_t vif_index,
				    const IPvX& src,
				    const IPvX& dst,
				    const uint8_t *rcvbuf,
				    size_t rcvlen);
    /**
     * Send signal message: not used by PIM.
     */
    int		signal_message_send(const string&, // dst_module_instance_name,
				    xorp_module_id, // dst_module_id,
				    int		, // message_type,
				    uint32_t	, // vif_index,
				    const IPvX&	, // src,
				    const IPvX&	, // dst,
				    const uint8_t * , // sndbuf,
				    size_t	  // sndlen
	) { XLOG_UNREACHABLE(); return (XORP_ERROR); }

    /**
     * Start a protocol vif with the kernel.
     * 
     * This is a pure virtual function, and it must be implemented
     * by the communication-wrapper class that inherits this base class.
     * 
     * @param vif_index the vif index of the interface to start.
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    virtual int start_protocol_kernel_vif(uint32_t vif_index) = 0;
    
    /**
     * Stop a protocol vif with the kernel.
     * 
     * This is a pure virtual function, and it must be implemented
     * by the communication-wrapper class that inherits this base class.
     * 
     * @param vif_index the vif index of the interface to stop.
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    virtual int stop_protocol_kernel_vif(uint32_t vif_index) = 0;
    
    /**
     * Join a multicast group on an interface.
     * 
     * This is a pure virtual function, and it must be implemented
     * by the communication-wrapper class that inherits this base class.
     * 
     * TODO: add a source address as well!!
     * 
     * @param vif_index the vif index of the interface to join.
     * @param multicast_group the multicast group address.
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    virtual int join_multicast_group(uint32_t vif_index,
				     const IPvX& multicast_group) = 0;
    
    /**
     * Leave a multicast group on an interface.
     * 
     * This is a pure virtual function, and it must be implemented
     * by the communication-wrapper class that inherits this base class.
     * 
     * TODO: add a source address as well!!
     * 
     * @param vif_index the vif index of the interface to leave.
     * @param multicast_group the multicast group address.
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    virtual int leave_multicast_group(uint32_t vif_index,
				      const IPvX& multicast_group) = 0;
    
    /**
     * Add a Multicast Forwarding Cache to the kernel.
     * 
     * This is a pure virtual function, and it must be implemented
     * by the communication-wrapper class that inherits this base class.
     * 
     * @param pim_mfc the @ref PimMfc entry to add.
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    virtual int add_mfc_to_kernel(const PimMfc& pim_mfc) = 0;
    
    /**
     * Delete a Multicast Forwarding Cache to the kernel.
     * 
     * This is a pure virtual function, and it must be implemented
     * by the communication-wrapper class that inherits this base class.
     * 
     * @param pim_mfc the @ref PimMfc entry to delete.
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    virtual int delete_mfc_from_kernel(const PimMfc& pim_mfc) = 0;
    
    /**
     * Add a dataflow monitor to the MFEA.
     * 
     * This is a pure virtual function, and it must be implemented
     * by the communication-wrapper class that inherits this base class.
     * 
     * Note: either @ref is_threshold_in_packets or @ref is_threshold_in_bytes
     * (or both) must be true.
     * Note: either @ref is_geq_upcall or @ref is_leq_upcall
     * (but not both) must be true.
     * 
     * @param source the source address.
     * 
     * @param group the group address.
     * 
     * @param threshold_interval_sec the dataflow threshold interval
     * (seconds).
     * 
     * @param threshold_interval_usec the dataflow threshold interval
     * (microseconds).
     * 
     * @param threshold_packets the threshold (in number of packets) to
     * compare against.
     * 
     * @param threshold_bytes the threshold (in number of bytes) to
     * compare against.
     * 
     * @param is_threshold_in_packets if true, @ref threshold_packets is valid.
     * 
     * @param is_threshold_in_bytes if true, @ref threshold_bytes is valid.
     * 
     * @param is_geq_upcall if true, the operation for comparison is ">=".
     * 
     * @param is_leq_upcall if true, the operation for comparison is "<=".
     * 
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    virtual int add_dataflow_monitor(const IPvX& source_addr,
				     const IPvX& group_addr,
				     uint32_t threshold_interval_sec,
				     uint32_t threshold_interval_usec,
				     uint32_t threshold_packets,
				     uint32_t threshold_bytes,
				     bool is_threshold_in_packets,
				     bool is_threshold_in_bytes,
				     bool is_geq_upcall,
				     bool is_leq_upcall,
				     bool rolling) = 0;
    
    /**
     * Delete a dataflow monitor from the MFEA.
     * 
     * This is a pure virtual function, and it must be implemented
     * by the communication-wrapper class that inherits this base class.
     * 
     * Note: either @ref is_threshold_in_packets or @ref is_threshold_in_bytes
     * (or both) must be true.
     * Note: either @ref is_geq_upcall or @ref is_leq_upcall
     * (but not both) must be true.
     * 
     * @param source the source address.
     * 
     * @param group the group address.
     * 
     * @param threshold_interval_sec the dataflow threshold interval
     * (seconds).
     * 
     * @param threshold_interval_usec the dataflow threshold interval
     * (microseconds).
     * 
     * @param threshold_packets the threshold (in number of packets) to
     * compare against.
     * 
     * @param threshold_bytes the threshold (in number of bytes) to
     * compare against.
     * 
     * @param is_threshold_in_packets if true, @ref threshold_packets is valid.
     * 
     * @param is_threshold_in_bytes if true, @ref threshold_bytes is valid.
     * 
     * @param is_geq_upcall if true, the operation for comparison is ">=".
     * 
     * @param is_leq_upcall if true, the operation for comparison is "<=".
     * 
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    virtual int delete_dataflow_monitor(const IPvX& source_addr,
					const IPvX& group_addr,
					uint32_t threshold_interval_sec,
					uint32_t threshold_interval_usec,
					uint32_t threshold_packets,
					uint32_t threshold_bytes,
					bool is_threshold_in_packets,
					bool is_threshold_in_bytes,
					bool is_geq_upcall,
					bool is_leq_upcall,
					bool rolling) = 0;
    
    /**
     * Delete all dataflow monitors for a given source and group address from
     * the MFEA.
     * 
     * This is a pure virtual function, and it must be implemented
     * by the communication-wrapper class that inherits this base class.
     * 
     * @param source_addr the source address.
     * @param group_addr the group address.
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    virtual int delete_all_dataflow_monitor(const IPvX& source_addr,
					    const IPvX& group_addr) = 0;
    
    /**
     * Register this protocol with the MLD/IGMP module.
     * 
     * This is a pure virtual function, and it must be implemented
     * by the communication-wrapper class that inherits this base class.
     * 
     * By registering this protocol with the MLD/IGMP module, it will
     * be notified about multicast group membership events.
     * 
     * @param vif_index the vif index of the interface to register with
     * the MLD/IGMP module.
     * 
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    virtual int add_protocol_mld6igmp(uint32_t vif_index) = 0;
    
    /**
     * Deregister this protocol with the MLD/IGMP module.
     * 
     * This is a pure virtual function, and it must be implemented
     * by the communication-wrapper class that inherits this base class.
     * 
     * @param vif_index the vif index of the interface to deregister with
     * the MLD/IGMP module.
     * 
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    virtual int delete_protocol_mld6igmp(uint32_t vif_index) = 0;
    
    /**
     * Receive "add membership" from the MLD/IGMP module.
     * 
     * @param vif_index the vif index of the interface with membership change.
     * @param source the source address of the (S,G) or (*,G) entry that has
     * changed membership. In case of Any-Source Multicast, it is IPvX::ZERO().
     * @param group the group address.
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		add_membership(uint32_t vif_index, const IPvX& source,
			       const IPvX& group);
    
    /**
     * Receive "delete membership" from the MLD/IGMP module.
     * 
     * @param vif_index the vif index of the interface with membership change.
     * @param source the source address of the (S,G) or (*,G) entry that has
     * changed membership. In case of Any-Source Multicast, it is IPvX::ZERO().
     * @param group the group address.
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		delete_membership(uint32_t vif_index, const IPvX& source,
				  const IPvX& group);
    
    /**
     * Test if an address is directly connected to a specified virtual
     * interface.
     * 
     * Note that the virtual interface the address is directly connected to
     * must be UP.
     * 
     * @param pim_vif the virtual interface to test against.
     * @param ipaddr_test the address to test.
     * @return true if @ref ipaddr_test is directly connected to @ref vif,
     * otherwise false.
     */
    bool is_directly_connected(const PimVif& pim_vif,
			       const IPvX& ipaddr_test) const;
    
    /**
     * Get the PIM-Register virtual interface.
     * 
     * @return the PIM-Register virtual interface if exists, otherwise NULL.
     */
    PimVif	*vif_find_pim_register() const;
    
    /**
     * Get the vif index of the PIM-Register virtual interface.
     * 
     * @return the vif index of the PIM-Register virtual interface if exists,
     * otherwise @ref Vif::VIF_INDEX_INVALID.
     */
    uint32_t	pim_register_vif_index() const { return (_pim_register_vif_index); }

    /**
     * Get the PIM Register source interface.
     *
     * @return the PIM Register source interface if exists, otherwise NULL.
     */
    PimVif	*vif_find_register_source() const;

    /**
     * Get the vif index of the PIM Register source interface.
     *
     * @return the vif index of the PIM Register source interface if exists,
     * otherwise @ref Vif::VIF_INDEX_INVALID.
     */
    uint32_t	register_source_vif_index() const { return (_register_source_vif_index); }
    
    /**
     * Get the PIM Multicast Routing Table.
     * 
     * @return a reference to the PIM Multicast Routing Table (@ref PimMrt).
     */
    PimMrt&	pim_mrt()		{ return (_pim_mrt);		}
    
    /**
     * Get the table with the Multicast Routing Information Base used by PIM.
     * 
     * @return a reference to the table with the Multicast Routing Information
     * Base used by PIM (@ref PimMribTable).
     */
    PimMribTable& pim_mrib_table()	{ return (_pim_mrib_table);	}
    
    /**
     * Get the PIM Bootstrap entity.
     * 
     * @return a reference to the PIM Bootstrap entity (@ref PimBsr).
     */
    PimBsr&	pim_bsr()		{ return (_pim_bsr);		}
    
    /**
     * Get the PIM RP table.
     * 
     * @return a reference to the PIM RP table (@ref RpTable).
     */
    RpTable&	rp_table()		{ return (_rp_table);		}
    
    /**
     * Get the PIM Scope-Zone table.
     * 
     * @return a reference to the PIM Scope-Zone table.
     */
    PimScopeZoneTable& pim_scope_zone_table() { return (_pim_scope_zone_table); }
    
    /**
     * Get the set of vifs for which this PIM note is a Designated Router.
     * 
     * @return the @ref Mifset indicating the vifs for which this PIM node is
     * a Designated Router.
     */
    Mifset&	pim_vifs_dr()		{ return (_pim_vifs_dr);	}
    
    /**
     * Set/reset a virtual interface as a Designated Router.
     * 
     * @param vif_index the vif index of the virtual interface to set/reset as
     * a Designated Router.
     * @param v if true, set the virtual interface as a Designated Router,
     * otherwise reset it.
     */
    void	set_pim_vifs_dr(uint32_t vif_index, bool v);

    /**
     * Find the RPF virtual interface for a given destination address.
     * 
     * @param dst_addr the destination address to lookup.
     * @return the RPF virtual interface (@ref PimVif) toward @ref dst_addr
     * if found, otherwise NULL.
     */
    PimVif	*pim_vif_rpf_find(const IPvX& dst_addr);

    /**
     * Find the RPF PIM neighbor for a given destination address.
     * 
     * @param dst_addr the destination address to lookup.
     * @return the RPF PIM neighbor (@ref PimNbr) toward @ref dst_addr
     * if found, otherwise NULL.
     */
    PimNbr	*pim_nbr_rpf_find(const IPvX& dst_addr);
    
    /**
     * Find the RPF PIM neighbor for a given destination address, and
     * already known @ref Mrib entry.
     * 
     * @param dst_addr the destination address to lookup.
     * @param mrib the already known @ref Mrib entry.
     * @return the RPF PIM neighbor (@ref PimNbr) toward @ref dst_addr
     * if found, otherwise NULL.
     */
    PimNbr	*pim_nbr_rpf_find(const IPvX& dst_addr, const Mrib *mrib);
    
    /**
     * Find a PIM neighbor by its address.
     * 
     * Note: this method should be used in very limited cases, because
     * in case of IPv6 a neighbor's IP address may be non-unique within
     * the PIM neighbor database due to scope issues.
     * 
     * @param nbr_addr the address of the PIM neighbor.
     * @return the PIM neighbor (@ref PimNbr) if found, otherwise NULL.
     */
    PimNbr	*pim_nbr_find_global(const IPvX& nbr_addr);
    
    /**
     * Enable the PIM Bootstrap mechanism.
     */
    void	enable_bsr() { pim_bsr().enable(); }
    
    /**
     * Disable the PIM Bootstrap mechanism.

     */
    void	disable_bsr() { pim_bsr().disable(); }
    
    /**
     * Start the Bootstrap mechanism.
     * 
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		start_bsr() { return (pim_bsr().start()); }
    
    /**
     * Stop the Bootstrap mechanism.
     * 
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		stop_bsr() { return (pim_bsr().stop()); }
    
    //
    // Configuration methods
    //
    
    /**
     * Complete the set of vif configuration changes.
     * 
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		set_config_all_vifs_done(string& error_msg);

    /**
     * Get the protocol version on an interface.
     * 
     * @param vif_name the name of the vif to get the protocol version of.
     * @param proto_version the return-by-reference protocol version.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		get_vif_proto_version(const string& vif_name,
				      int& proto_version,
				      string& error_msg);
    
    /**
     * Set the protocol version on an interface.
     * 
     * @param vif_name the name of the vif to set the protocol version of.
     * @param proto_version the new protocol version.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		set_vif_proto_version(const string& vif_name,
				      int proto_version,
				      string& error_msg);
    
    /**
     * Reset the protocol version on an interface to its default value.
     * 
     * @param vif_name the name of the vif to reset the protocol version of
     * to its default value.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		reset_vif_proto_version(const string& vif_name,
					string& error_msg);

    int		get_vif_passive(const string& vif_name,
				bool& passive,
				string& error_msg);
    int		set_vif_passive(const string& vif_name,
				bool passive,
				string& error_msg);
    int		reset_vif_passive(const string& vif_name,
				  string& error_msg);

    /**
     * Get the value of the flag that enables/disables the IP Router Alert
     * option check per interface for received packets.
     * 
     * @param vif_name the name of the vif to apply to.
     * @param enabled the return-by-reference flag value.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		get_vif_ip_router_alert_option_check(const string& vif_name,
						     bool& enabled,
						     string& error_msg);
    
    /**
     * Enable/disable the IP Router Alert option check per interface for
     * received packets.
     * 
     * @param vif_name the name of the vif to apply to.
     * @param enable if true, then enable the IP Router Alert option check,
     * otherwise disable it.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		set_vif_ip_router_alert_option_check(const string& vif_name,
						     bool enable,
						     string& error_msg);
    
    /**
     * Reset the value of the flag that enables/disables the IP Router Alert
     * option check per interface for received packets to its default value.
     * 
     * @param vif_name the name of the vif to apply to.
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		reset_vif_ip_router_alert_option_check(const string& vif_name,
						       string& error_msg);

    //
    int		get_vif_hello_triggered_delay(const string& vif_name,
					      uint16_t& hello_triggered_delay,
					      string& error_msg);
    int		set_vif_hello_triggered_delay(const string& vif_name,
					      uint16_t hello_triggered_delay,
					      string& error_msg);
    int		reset_vif_hello_triggered_delay(const string& vif_name,
						string& error_msg);
    //
    int		get_vif_hello_period(const string& vif_name,
				     uint16_t& hello_period,
				     string& error_msg);
    int		set_vif_hello_period(const string& vif_name,
				     uint16_t hello_period,
				     string& error_msg);
    int		reset_vif_hello_period(const string& vif_name,
				       string& error_msg);
    //
    int		get_vif_hello_holdtime(const string& vif_name,
				       uint16_t& hello_holdtime,
				       string& error_msg);
    int		set_vif_hello_holdtime(const string& vif_name,
				       uint16_t	hello_holdtime,
				       string& error_msg);
    int		reset_vif_hello_holdtime(const string& vif_name,
					 string& error_msg);
    //
    int		get_vif_dr_priority(const string& vif_name,
				    uint32_t& dr_priority,
				    string& error_msg);
    int		set_vif_dr_priority(const string& vif_name,
				    uint32_t dr_priority,
				    string& error_msg);
    int		reset_vif_dr_priority(const string& vif_name,
				      string& error_msg);
    //
    int		get_vif_propagation_delay(const string&	vif_name,
					  uint16_t& propagation_delay,
					  string& error_msg);
    int		set_vif_propagation_delay(const string&	vif_name,
					  uint16_t propagation_delay,
					  string& error_msg);
    int		reset_vif_propagation_delay(const string& vif_name,
					    string& error_msg);
    //
    int		get_vif_override_interval(const string&	vif_name,
					  uint16_t& override_interval,
					  string& error_msg);
    int		set_vif_override_interval(const string&	vif_name,
					  uint16_t override_interval,
					  string& error_msg);
    int		reset_vif_override_interval(const string& vif_name,
					    string& error_msg);
    //
    int		get_vif_is_tracking_support_disabled(const string& vif_name,
						     bool& is_tracking_support_disabled,
						     string& error_msg);
    int		set_vif_is_tracking_support_disabled(const string& vif_name,
						     bool is_tracking_support_disabled,
						     string& error_msg);
    int		reset_vif_is_tracking_support_disabled(const string& vif_name,
						       string& error_msg);
    //
    int		get_vif_accept_nohello_neighbors(const string& vif_name,
						 bool& accept_nohello_neighbors,
						 string& error_msg);
    int		set_vif_accept_nohello_neighbors(const string& vif_name,
						 bool accept_nohello_neighbors,
						 string& error_msg);
    int		reset_vif_accept_nohello_neighbors(const string& vif_name,
						   string& error_msg);
    //
    int		get_vif_join_prune_period(const string&	vif_name,
					  uint16_t& join_prune_period,
					  string& error_msg);
    int		set_vif_join_prune_period(const string&	vif_name,
					  uint16_t join_prune_period,
					  string& error_msg);
    int		reset_vif_join_prune_period(const string& vif_name,
					    string& error_msg);
    //
    int		get_switch_to_spt_threshold(bool& is_enabled,
					    uint32_t& interval_sec,
					    uint32_t& bytes,
					    string& error_msg);
    int		set_switch_to_spt_threshold(bool is_enabled,
					    uint32_t interval_sec,
					    uint32_t bytes,
					    string& error_msg);
    int		reset_switch_to_spt_threshold(string& error_msg);
    //
    int		add_config_scope_zone_by_vif_name(const IPvXNet& scope_zone_id,
						  const string& vif_name,
						  string& error_msg);
    int		add_config_scope_zone_by_vif_addr(const IPvXNet& scope_zone_id,
						  const IPvX& vif_addr,
						  string& error_msg);
    int		delete_config_scope_zone_by_vif_name(const IPvXNet& scope_zone_id,
						     const string& vif_name,
						     string& error_msg);
    int		delete_config_scope_zone_by_vif_addr(const IPvXNet& scope_zone_id,
						     const IPvX& vif_addr,
						     string& error_msg);
    //
    int		add_config_cand_bsr(const IPvXNet& scope_zone_id,
				    bool is_scope_zone,
				    const string& vif_name,
				    const IPvX& vif_addr,
				    uint8_t bsr_priority,
				    uint8_t hash_mask_len,
				    string& error_msg);
    int		delete_config_cand_bsr(const IPvXNet& scope_zone_id,
				       bool is_scope_zone,
				       string& error_msg);
    int		add_config_cand_rp(const IPvXNet& group_prefix,
				   bool is_scope_zone,
				   const string& vif_name,
				   const IPvX& vif_addr,
				   uint8_t rp_priority,
				   uint16_t rp_holdtime,
				   string& error_msg);
    int		delete_config_cand_rp(const IPvXNet& group_prefix,
				      bool is_scope_zone,
				      const string& vif_name,
				      const IPvX& vif_addr,
				      string& error_msg);
    int		add_config_static_rp(const IPvXNet& group_prefix,
				     const IPvX& rp_addr,
				     uint8_t rp_priority,
				     uint8_t hash_mask_len,
				     string& error_msg);
    int		delete_config_static_rp(const IPvXNet& group_prefix,
					const IPvX& rp_addr,
					string& error_msg);
    int		delete_config_all_static_group_prefixes_rp(const IPvX& rp_addr,
							   string& error_msg);
    int		delete_config_all_static_rps(string& error_msg);
    int		config_static_rp_done(string& error_msg);

    int		add_alternative_subnet(const string& vif_name,
				       const IPvXNet& subnet,
				       string& error_msg);
    int		delete_alternative_subnet(const string& vif_name,
					  const IPvXNet& subnet,
					  string& error_msg);
    int		remove_all_alternative_subnets(const string& vif_name,
					       string& error_msg);

    int		get_default_ip_tos(uint8_t& ip_tos,
				   string& error_msg);
    int		set_default_ip_tos(uint8_t ip_tos,
				   string& error_msg);
    int		reset_default_ip_tos(string& error_msg);

    int		set_vif_register_source(const string& vif_name,
					string& error_msg);
    int		reset_vif_register_source(string& error_msg);
    
    //
    // Debug-related methods
    //
    
#ifndef QUAGGA_MULTICAST
    /**
     * Test if trace log is enabled.
     * 
     * This method is used to test whether to output trace log debug messges.
     * 
     * @return true if trace log is enabled, otherwise false.
     */
    bool	is_log_trace() const { return (_is_log_trace); }
    
    /**
     * Enable/disable trace log.
     * 
     * This method is used to enable/disable trace log debug messages output.
     * 
     * @param is_enabled if true, trace log is enabled, otherwise is disabled.
     */
    void	set_log_trace(bool is_enabled) { _is_log_trace = is_enabled; }
#else
    /**
     * Test if trace log is enabled.
     *
     * This method is used to test whether to output trace log debug messges.
     *
     * @return true if trace log is enabled, otherwise false.
     */
    bool	is_log_trace() const { return (_log_flags & PIM_LOG_TRACE); }

    /**
     * Enable/disable trace log.
     *
     * This method is used to enable/disable trace log debug messages output.
     *
     * @param is_enabled if true, trace log is enabled, otherwise is disabled.
     */
    void	set_log_trace(bool v) {
	if (v)
	    _log_flags |= PIM_LOG_TRACE;
	else
	    _log_flags &= ~PIM_LOG_TRACE;
    }

    /**
     * Test if info log is enabled.
     *
     * This method is used to test whether to output info log messges.
     *
     * @return true if info log is enabled, otherwise false.
     */
    bool	is_log_info() const {
	return (_log_flags & (PIM_LOG_INFO | PIM_LOG_TRACE));
    }

    /**
     * Enable/disable info log.
     *
     * This method is used to enable/disable info log messages output.
     *
     * @param is_enabled if true, info log is enabled, otherwise is disabled.
     */
    void	set_log_info(bool v) {
	if (v)
	    _log_flags |= PIM_LOG_INFO;
	else
	    _log_flags &= ~PIM_LOG_INFO;
    }

    /**
     * Test if neighbor change logging is enabled.
     *
     * @return true if neighbor change logging is enabled, otherwise false.
     */
    bool	is_log_nbr() const { return (_log_flags & PIM_LOG_NBR); }

    /**
     * Enable/disable neighbor change logging.
     *
     * @param is_enabled if true, neighbor change logging is enabled,
     * otherwise is disabled.
     */
    void	set_log_nbr(bool v) {
	if (v)
	    _log_flags |= PIM_LOG_NBR;
	else
	    _log_flags &= ~PIM_LOG_NBR;
    }
#endif	// QUAGGA_MULTICAST
    
    //
    // Test-related methods
    //
    // Join/Prune test-related methods
    int		add_test_jp_entry(const IPvX& source_addr,
				  const IPvX& group_addr,
				  uint8_t group_mask_len,
				  mrt_entry_type_t mrt_entry_type,
				  action_jp_t action_jp, uint16_t holdtime,
				  bool is_new_group);
    int		send_test_jp_entry(const string& vif_name,
				   const IPvX& nbr_addr,
				   string& error_msg);
    // Assert test-related methods
    int		send_test_assert(const string& vif_name,
				 const IPvX& source_addr,
				 const IPvX& group_addr,
				 bool rpt_bit,
				 uint32_t metric_preference,
				 uint32_t metric,
				 string& error_msg);
    // Bootstrap test-related methods
    int		add_test_bsr_zone(const PimScopeZoneId& zone_id,
				  const IPvX& bsr_addr,
				  uint8_t bsr_priority,
				  uint8_t hash_mask_len,
				  uint16_t fragment_tag);
    int		add_test_bsr_group_prefix(const PimScopeZoneId& zone_id,
					  const IPvXNet& group_prefix,
					  bool is_scope_zone,
					  uint8_t expected_rp_count);
    int		add_test_bsr_rp(const PimScopeZoneId& zone_id,
				const IPvXNet& group_prefix,
				const IPvX& rp_addr,
				uint8_t rp_priority,
				uint16_t rp_holdtime);
    int		send_test_bootstrap(const string& vif_name, string& error_msg);
    int		send_test_bootstrap_by_dest(const string& vif_name,
					    const IPvX& dest_addr,
					    string& error_msg);
    int		send_test_cand_rp_adv();
    
    
    //
    // PimNbr-related methods
    //
    void	add_pim_mre_no_pim_nbr(PimMre *pim_mre);
    void	delete_pim_mre_no_pim_nbr(PimMre *pim_mre);
    list<PimNbr *>& processing_pim_nbr_list() {
	return (_processing_pim_nbr_list);
    }
    void	init_processing_pim_mre_rp(uint32_t vif_index,
					   const IPvX& pim_nbr_addr);
    void	init_processing_pim_mre_wc(uint32_t vif_index,
					   const IPvX& pim_nbr_addr);
    void	init_processing_pim_mre_sg(uint32_t vif_index,
					   const IPvX& pim_nbr_addr);
    void	init_processing_pim_mre_sg_rpt(uint32_t vif_index,
					       const IPvX& pim_nbr_addr);
    PimNbr	*find_processing_pim_mre_rp(uint32_t vif_index,
					    const IPvX& pim_nbr_addr);
    PimNbr	*find_processing_pim_mre_wc(uint32_t vif_index,
					    const IPvX& pim_nbr_addr);
    PimNbr	*find_processing_pim_mre_sg(uint32_t vif_index,
					    const IPvX& pim_nbr_addr);
    PimNbr	*find_processing_pim_mre_sg_rpt(uint32_t vif_index,
						const IPvX& pim_nbr_addr);
    
    //
    // Configuration parameters
    //
    ConfigParam<bool>& is_switch_to_spt_enabled() {
	return (_is_switch_to_spt_enabled);
    }
    ConfigParam<uint32_t>& switch_to_spt_threshold_interval_sec() {
	return (_switch_to_spt_threshold_interval_sec);
    }
    ConfigParam<uint32_t>& switch_to_spt_threshold_bytes() {
	return (_switch_to_spt_threshold_bytes);
    }
    ConfigParam<uint8_t>& default_ip_tos() {
	return (_default_ip_tos);
    }


    //
    // Statistics-related counters and values
    //
    void	clear_pim_statistics();
    int		clear_pim_statistics_per_vif(const string& vif_name,
					     string& error_msg);
    //
    uint32_t	pimstat_hello_messages_received() const;
    uint32_t	pimstat_hello_messages_sent() const;
    uint32_t	pimstat_hello_messages_rx_errors() const;
    uint32_t	pimstat_register_messages_received() const;
    uint32_t	pimstat_register_messages_sent() const;
    uint32_t	pimstat_register_messages_rx_errors() const;
    uint32_t	pimstat_register_stop_messages_received() const;
    uint32_t	pimstat_register_stop_messages_sent() const;
    uint32_t	pimstat_register_stop_messages_rx_errors() const;
    uint32_t	pimstat_join_prune_messages_received() const;
    uint32_t	pimstat_join_prune_messages_sent() const;
    uint32_t	pimstat_join_prune_messages_rx_errors() const;
    uint32_t	pimstat_bootstrap_messages_received() const;
    uint32_t	pimstat_bootstrap_messages_sent() const;
    uint32_t	pimstat_bootstrap_messages_rx_errors() const;
    uint32_t	pimstat_assert_messages_received() const;
    uint32_t	pimstat_assert_messages_sent() const;
    uint32_t	pimstat_assert_messages_rx_errors() const;
    uint32_t	pimstat_graft_messages_received() const;
    uint32_t	pimstat_graft_messages_sent() const;
    uint32_t	pimstat_graft_messages_rx_errors() const;
    uint32_t	pimstat_graft_ack_messages_received() const;
    uint32_t	pimstat_graft_ack_messages_sent() const;
    uint32_t	pimstat_graft_ack_messages_rx_errors() const;
    uint32_t	pimstat_candidate_rp_messages_received() const;
    uint32_t	pimstat_candidate_rp_messages_sent() const;
    uint32_t	pimstat_candidate_rp_messages_rx_errors() const;
    //
    uint32_t	pimstat_unknown_type_messages() const;
    uint32_t	pimstat_unknown_version_messages() const;
    uint32_t	pimstat_neighbor_unknown_messages() const;
    uint32_t	pimstat_bad_length_messages() const;
    uint32_t	pimstat_bad_checksum_messages() const;
    uint32_t	pimstat_bad_receive_interface_messages() const;
    uint32_t	pimstat_rx_interface_disabled_messages() const;
    uint32_t	pimstat_rx_register_not_rp() const;
    uint32_t	pimstat_rp_filtered_source() const;
    uint32_t	pimstat_unknown_register_stop() const;
    uint32_t	pimstat_rx_join_prune_no_state() const;
    uint32_t	pimstat_rx_graft_graft_ack_no_state() const;
    uint32_t	pimstat_rx_graft_on_upstream_interface() const;
    uint32_t	pimstat_rx_candidate_rp_not_bsr() const;
    uint32_t	pimstat_rx_bsr_when_bsr() const;
    uint32_t	pimstat_rx_bsr_not_rpf_interface() const;
    uint32_t	pimstat_rx_unknown_hello_option() const;
    uint32_t	pimstat_rx_data_no_state() const;
    uint32_t	pimstat_rx_rp_no_state() const;
    uint32_t	pimstat_rx_aggregate() const;
    uint32_t	pimstat_rx_malformed_packet() const;
    uint32_t	pimstat_no_rp() const;
    uint32_t	pimstat_no_route_upstream() const;
    uint32_t	pimstat_rp_mismatch() const;
    uint32_t	pimstat_rpf_neighbor_unknown() const;
    //
    uint32_t	pimstat_rx_join_rp() const;
    uint32_t	pimstat_rx_prune_rp() const;
    uint32_t	pimstat_rx_join_wc() const;
    uint32_t	pimstat_rx_prune_wc() const;
    uint32_t	pimstat_rx_join_sg() const;
    uint32_t	pimstat_rx_prune_sg() const;
    uint32_t	pimstat_rx_join_sg_rpt() const;
    uint32_t	pimstat_rx_prune_sg_rpt() const;
    //
    //
    int	pimstat_hello_messages_received_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_hello_messages_sent_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_hello_messages_rx_errors_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_register_messages_received_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_register_messages_sent_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_register_messages_rx_errors_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_register_stop_messages_received_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_register_stop_messages_sent_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_register_stop_messages_rx_errors_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_join_prune_messages_received_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_join_prune_messages_sent_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_join_prune_messages_rx_errors_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_bootstrap_messages_received_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_bootstrap_messages_sent_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_bootstrap_messages_rx_errors_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_assert_messages_received_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_assert_messages_sent_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_assert_messages_rx_errors_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_graft_messages_received_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_graft_messages_sent_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_graft_messages_rx_errors_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_graft_ack_messages_received_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_graft_ack_messages_sent_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_graft_ack_messages_rx_errors_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_candidate_rp_messages_received_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_candidate_rp_messages_sent_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_candidate_rp_messages_rx_errors_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    //
    int	pimstat_unknown_type_messages_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_unknown_version_messages_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_neighbor_unknown_messages_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_bad_length_messages_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_bad_checksum_messages_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_bad_receive_interface_messages_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_interface_disabled_messages_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_register_not_rp_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rp_filtered_source_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_unknown_register_stop_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_join_prune_no_state_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_graft_graft_ack_no_state_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_graft_on_upstream_interface_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_candidate_rp_not_bsr_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_bsr_when_bsr_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_bsr_not_rpf_interface_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_unknown_hello_option_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_data_no_state_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_rp_no_state_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_aggregate_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_malformed_packet_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_no_rp_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_no_route_upstream_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rp_mismatch_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rpf_neighbor_unknown_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    //
    int	pimstat_rx_join_rp_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_prune_rp_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_join_wc_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_prune_wc_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_join_sg_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_prune_sg_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int	pimstat_rx_join_sg_rpt_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    int pimstat_rx_prune_sg_rpt_per_vif(const string& vif_name, uint32_t& result, string& error_msg) const;
    
private:
    /**
     * A method invoked when the status of a service changes.
     * 
     * @param service the service whose status has changed.
     * @param old_status the old status.
     * @param new_status the new status.
     */
    void status_change(ServiceBase*  service,
		       ServiceStatus old_status,
		       ServiceStatus new_status);

    /**
     * Initiate registration with the MFEA.
     * 
     * This is a pure virtual function, and it must be implemented
     * by the communication-wrapper class that inherits this base class.
     */
    virtual void mfea_register_startup() = 0;

    /**
     * Initiate de-registration with the MFEA.
     * 
     * This is a pure virtual function, and it must be implemented
     * by the communication-wrapper class that inherits this base class.
     */
    virtual void mfea_register_shutdown() = 0;

#ifndef QUAGGA_MULTICAST
    /**
     * Initiate registration with the RIB.
     * 
     * This is a pure virtual function, and it must be implemented
     * by the communication-wrapper class that inherits this base class.
     */
    virtual void rib_register_startup() = 0;

    /**
     * Initiate de-registration with the RIB.
     * 
     * This is a pure virtual function, and it must be implemented
     * by the communication-wrapper class that inherits this base class.
     */
    virtual void rib_register_shutdown() = 0;
#endif	// !QUAGGA_MULTICAST
    
    PimMrt	_pim_mrt;		// PIM Multicast Routing Table
    PimMribTable _pim_mrib_table;	// PIM Multicast Routing Information Base table
    RpTable	_rp_table;		// The RP table
    PimScopeZoneTable _pim_scope_zone_table; // The scope zone table
    PimBsr	_pim_bsr;		// The BSR state
    
    uint32_t	_pim_register_vif_index;// The PIM Register vif index
    uint32_t	_register_source_vif_index;// The PIM Register source vif index
    Mifset	_pim_vifs_dr;		// The vifs I am the DR
    
    buffer_t	*_buffer_recv;		// Buffer for receiving messages
    
    list<PimNbr *> _processing_pim_nbr_list; // The list of deleted PimNbr with
					// PimMre entries to process.
    
    //
    // Configuration parameters
    //
    ConfigParam<bool>		_is_switch_to_spt_enabled;
    ConfigParam<uint32_t>	_switch_to_spt_threshold_interval_sec;
    ConfigParam<uint32_t>	_switch_to_spt_threshold_bytes;
    ConfigParam<uint8_t>	_default_ip_tos;


    //
    // Debug and test-related state
    //
#ifndef QUAGGA_MULTICAST
    bool	_is_log_trace;		// If true, enable XLOG_TRACE()
#else
    enum {
	PIM_LOG_TRACE	= 1 << 0,	// enable XLOG_TRACE()
	PIM_LOG_INFO	= 1 << 1,	// enable XLOG_INFO()
	PIM_LOG_NBR	= 1 << 2,	// log neighbor changes
    };
    uint32_t	_log_flags;
#endif	// QUAGGA_MULTICAST
    list<PimJpHeader> _test_jp_headers_list; // J/P headers to send test J/P messages
};


//
// Global variables
//


//
// Global functions prototypes
//

#endif // __PIM_PIM_NODE_HH__
