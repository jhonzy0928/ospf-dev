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

// $XORP: xorp/pim/pim_vif.hh,v 1.42 2007/02/16 22:46:51 pavlin Exp $


#ifndef __PIM_PIM_VIF_HH__
#define __PIM_PIM_VIF_HH__


//
// PIM virtual interface definition.
//


#include <list>

#include "libxorp/config_param.hh"
#include "libxorp/vif.hh"
#include "libxorp/timer.hh"
#include "libproto/proto_unit.hh"
#include "mrt/buffer.h"
#include "mrt/mifset.hh"
#include "mrt/multicast_defs.h"
#include "pim_nbr.hh"
#include "pim_proto_join_prune_message.hh"


//
// Constants definitions
//

//
// Structures/classes, typedefs and macros
//

class AssertMetric;
class BsrZone;
class PimJpGroup;
class PimJpHeader;
class PimNbr;
class PimNode;


/**
 * @short A class for PIM-specific virtual interface.
 */
class PimVif : public ProtoUnit, public Vif {
public:
    /**
     * Constructor for a given PIM node and a generic virtual interface.
     * 
     * @param pim_node the @ref PimNode this interface belongs to.
     * @param vif the generic Vif interface that contains various information.
     */
    PimVif(PimNode& pim_node, const Vif& vif);
    
    /**
     * Destructor
     */
    virtual ~PimVif();

    /**
     * Set configuration to default values.
     */
    void	set_default_config();
    
    /**
     * Set the current protocol version.
     * 
     * The protocol version must be in the interval
     * [PIM_VERSION_MIN, PIM_VERSION_MAX].
     * 
     * @param proto_version the protocol version to set.
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		set_proto_version(int proto_version);
    
    /**
     *  Start PIM on a single virtual interface.
     * 
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		start(string& error_msg);
    
    /**
     * Gracefully stop PIM on a single virtual interface.
     * 
     *The graceful stop will attempt to send Join/Prune, Assert, etc.
     * messages for all multicast routing entries to gracefully clean-up
     * state with neighbors.
     * After the multicast routing entries cleanup is completed,
     * PimVif::final_stop() is called to complete the job.
     * 
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		stop(string& error_msg);
    
    /**
     * Completely stop PIM on a single virtual interface.
     * 
     * This method should be called after @ref PimVif::stop() to complete
     * the job.
     * 
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		final_stop(string& error_msg);

    /**
     * Enable PIM on a single virtual interface.
     * 
     * If an unit is not enabled, it cannot be start, or pending-start.
     */
    void	enable();
    
    /**
     * Disable PIM on a single virtual interface.
     * 
     * If an unit is disabled, it cannot be start or pending-start.
     * If the unit was runnning, it will be stop first.
     */
    void	disable();
    
    /**
     * Receive a protocol message.
     * 
     * @param src the source address of the message.
     * @param dst the destination address of the message.
     * @param ip_ttl the IP TTL of the message. If it has a negative value
     * it should be ignored.
     * @param ip_ttl the IP TOS of the message. If it has a negative value,
     * it should be ignored.
     * @param is_router_alert if true, the IP Router Alert option in
     * the IP packet was set (when applicable).
     * @param buffer the data buffer with the received message.
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		pim_recv(const IPvX& src, const IPvX& dst,
			 int ip_ttl, int ip_tos, bool is_router_alert,
			 buffer_t *buffer);
    
    /**
     * Get the string with the flags about the vif status.
     * 
     * TODO: temporary here. Should go to the Vif class after the Vif
     * class starts using the Proto class.
     * 
     * @return the C++ style string with the flags about the vif status
     * (e.g., UP/DOWN/DISABLED, etc).
     */
    string	flags_string() const;
    
    /**
     * Get the PIM node (@ref PimNode).
     * 
     * @return a reference to the PIM node (@ref PimNode).
     */
    PimNode&	pim_node() const	{ return (_pim_node);		}
    
    /**
     * Get the PIM Multicast Routing Table (@ref PimMrt).
     * 
     * @return a reference to the PIM Multicast Routing Table (@ref PimMrt).
     */
    PimMrt&	pim_mrt() const;
    
    /**
     * Get the PIM neighbor information (@ref PimNbr) about myself.
     * 
     * @return a reference to the PIM neighbor information (@ref PimNbr)
     * about myself.
     */
    PimNbr&	pim_nbr_me()		{ return (_pim_nbr_me);		}
    
    /**
     * Start the PIM Hello operation.
     */
    void	pim_hello_start();
    
    /**
     * Stop the PIM Hello operation.
     */
    void	pim_hello_stop();
    
    /**
     * Elect a Designated Router on this interface.
     */
    void	pim_dr_elect();
    
    /**
     * Compute if I may become the Designated Router on this interface
     * if one of the PIM neighbor addresses is not considered.
     * 
     * Compute if I may become the DR on this interface if @ref exclude_addr
     * is excluded.
     * 
     * @param exclude_addr the address to exclude in the computation.
     * @return true if I may become the DR on this interface, otherwise
     * false.
     */
    bool	i_may_become_dr(const IPvX& exclude_addr);
    
    /**
     * Get my primary address on this interface.
     * 
     * @return my primary address on this interface.
     */
    const IPvX&	primary_addr() const	{ return (_pim_nbr_me.primary_addr()); }

    /**
     * Get my domain-wide reachable address on this interface.
     * 
     * @return my domain-wide reachable address on this interface.
     */
    const IPvX&	domain_wide_addr() const	{ return (_domain_wide_addr); }

    /**
     * Set my domain-wide reachable address on this interface.
     * 
     * @param v the value of the domain-wide reachable address.
     */
    void	set_domain_wide_addr(const IPvX& v) { _domain_wide_addr = v; }

    /**
     * Update the primary and the domain-wide reachable addresses.
     * 
     * The primary address should be a link-local unicast address, and
     * is used for transmitting the multicast control packets on the LAN.
     * The domain-wide reachable address is the address that should be
     * reachable by all PIM-SM routers in the domain
     * (e.g., the Cand-BSR, or the Cand-RP address).
     * 
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int		update_primary_and_domain_wide_address(string& error_msg);

    /**
     * Get the address of the Designated Router on this interface.
     * 
     * @return the address of the Designated Router on this interface.
     */
    const IPvX&	dr_addr() const		{ return (_dr_addr);		}

    void	pim_passive();

    ConfigParam<bool>& passive() { return (_passive); }

    /**
     * Optain a reference to the "IP Router Alert option check" flag.
     *
     * @return a reference to the "IP Router Alert option check" flag.
     */
    ConfigParam<bool>& ip_router_alert_option_check() { return (_ip_router_alert_option_check); }
    
    //
    // Hello-related configuration parameters
    //
    ConfigParam<uint16_t>& hello_triggered_delay() { return (_hello_triggered_delay); }
    ConfigParam<uint16_t>& hello_period() { return (_hello_period); }
    ConfigParam<uint16_t>& hello_holdtime() { return (_hello_holdtime); }
    ConfigParam<uint32_t>& dr_priority() { return (_dr_priority); }
    ConfigParam<uint16_t>& propagation_delay() { return (_propagation_delay); }
    ConfigParam<uint16_t>& override_interval() { return (_override_interval); }
    ConfigParam<bool>& is_tracking_support_disabled() { return (_is_tracking_support_disabled); }
    ConfigParam<bool>& accept_nohello_neighbors() { return (_accept_nohello_neighbors); }
    
    //
    // Hello-related non-configurable parameters
    //
    ConfigParam<uint32_t>& genid() { return (_genid); }
    
    //
    // Join/Prune-related configuration parameters
    //
    ConfigParam<uint16_t>& join_prune_period() { return (_join_prune_period); }
    ConfigParam<uint16_t>& join_prune_holdtime() { return (_join_prune_holdtime); }
    
    //
    // Assert-related configuration parameters
    //
    ConfigParam<uint32_t>& assert_time() { return (_assert_time); }
    ConfigParam<uint32_t>& assert_override_interval() { return (_assert_override_interval); }
    
    //
    // Functions for sending protocol messages.
    //
    int		pim_send(const IPvX& src, const IPvX& dst,
			 uint8_t message_type, buffer_t *buffer,
			 string& error_msg);
    int		pim_hello_send(string& error_msg);
    int		pim_hello_first_send();
    int		pim_join_prune_send(PimNbr *pim_nbr, PimJpHeader *jp_header,
				    string& error_msg);
    int		pim_assert_mre_send(PimMre *pim_mre,
				    const IPvX& assert_source_addr,
				    string& error_msg);
    int		pim_assert_cancel_send(PimMre *pim_mre, string& error_msg);
    int		pim_assert_send(const IPvX& assert_source_addr,
				const IPvX& assert_group_addr,
				bool rpt_bit,
				uint32_t metric_preference,
				uint32_t metric,
				string& error_msg);
    int		pim_register_send(const IPvX& rp_addr,
				  const IPvX& source_addr,
				  const IPvX& group_addr,
				  const uint8_t *rcvbuf,
				  size_t rcvlen,
				  string& error_msg);
    int		pim_register_null_send(const IPvX& rp_addr,
				       const IPvX& source_addr,
				       const IPvX& group_addr,
				       string& error_msg);
    int		pim_register_stop_send(const IPvX& rp_addr,
				       const IPvX& dr_addr,
				       const IPvX& source_addr,
				       const IPvX& group_addr,
				       string& error_msg);
    int		pim_bootstrap_send(const IPvX& dst_addr,
				   const BsrZone& bsr_zone,
				   string& error_msg);
    buffer_t	*pim_bootstrap_send_prepare(const IPvX& src_addr,
					    const IPvX& dst_addr,
					    const BsrZone& bsr_zone,
					    bool is_first_fragment);
    int		pim_cand_rp_adv_send(const IPvX& bsr_addr,
				     const BsrZone& bsr_zone);
    
    void	hello_timer_start(uint32_t sec, uint32_t usec);
    void	hello_timer_start_random(uint32_t sec, uint32_t usec);
    
    bool	is_lan_delay_enabled() const;
    // Link-related time intervals
    const TimeVal& effective_propagation_delay() const;
    const TimeVal& effective_override_interval() const;
    bool	is_lan_suppression_state_enabled() const;
    const TimeVal& upstream_join_timer_t_suppressed() const;
    const TimeVal& upstream_join_timer_t_override() const;
    
    // Misc. functions
    const TimeVal& jp_override_interval() const;
    list<PimNbr *>& pim_nbrs() { return (_pim_nbrs); }
    size_t	pim_nbrs_number() const { return (_pim_nbrs.size()); }
    bool	i_am_dr() const;
    void	set_i_am_dr(bool v);
    PimNbr	*pim_nbr_find(const IPvX& nbr_addr);
    void	add_pim_nbr(PimNbr *pim_nbr);
    int		delete_pim_nbr(PimNbr *pim_nbr);
    void	delete_pim_nbr_from_nbr_list(PimNbr *pim_nbr);
    const list<IPvXNet>& alternative_subnet_list() const { return _alternative_subnet_list; }
    void add_alternative_subnet(const IPvXNet& subnet);
    void delete_alternative_subnet(const IPvXNet& subnet);
    void remove_all_alternative_subnets();

    // Usage-related functions
    size_t	usage_by_pim_mre_task() const { return (_usage_by_pim_mre_task); }
    void	incr_usage_by_pim_mre_task();
    void	decr_usage_by_pim_mre_task();
    
    /**
     * Calculate the checksum of an IPv6 "pseudo-header" as described
     * in RFC 2460.
     * 
     * @param src the source address of the pseudo-header.
     * @param dst the destination address of the pseudo-header.
     * @param len the upper-layer packet length of the pseudo-header
     * (in host-order).
     * @param protocol the upper-layer protocol number.
     * @return the checksum of the IPv6 "pseudo-header".
     */
    uint16_t	calculate_ipv6_pseudo_header_checksum(const IPvX& src,
						      const IPvX& dst,
						      size_t len,
						      uint8_t protocol);
    buffer_t	*buffer_send_prepare();
    buffer_t	*buffer_send_prepare(buffer_t *buffer);

    //
    // Statistics-related counters and values
    //
    void	clear_pim_statistics();
    //
    uint32_t	pimstat_hello_messages_received() const { return _pimstat_hello_messages_received.get(); }
    uint32_t	pimstat_hello_messages_sent() const { return _pimstat_hello_messages_sent.get(); }
    uint32_t	pimstat_hello_messages_rx_errors() const { return _pimstat_hello_messages_rx_errors.get(); }
    uint32_t	pimstat_register_messages_received() const { return _pimstat_register_messages_received.get(); }
    uint32_t	pimstat_register_messages_sent() const { return _pimstat_register_messages_sent.get(); }
    uint32_t	pimstat_register_messages_rx_errors() const { return _pimstat_register_messages_rx_errors.get(); }
    uint32_t	pimstat_register_stop_messages_received() const { return _pimstat_register_stop_messages_received.get(); }
    uint32_t	pimstat_register_stop_messages_sent() const { return _pimstat_register_stop_messages_sent.get(); }
    uint32_t	pimstat_register_stop_messages_rx_errors() const { return _pimstat_register_stop_messages_rx_errors.get(); }
    uint32_t	pimstat_join_prune_messages_received() const { return _pimstat_join_prune_messages_received.get(); }
    uint32_t	pimstat_join_prune_messages_sent() const { return _pimstat_join_prune_messages_sent.get(); }
    uint32_t	pimstat_join_prune_messages_rx_errors() const { return _pimstat_join_prune_messages_rx_errors.get(); }
    uint32_t	pimstat_bootstrap_messages_received() const { return _pimstat_bootstrap_messages_received.get(); }
    uint32_t	pimstat_bootstrap_messages_sent() const { return _pimstat_bootstrap_messages_sent.get(); }
    uint32_t	pimstat_bootstrap_messages_rx_errors() const { return _pimstat_bootstrap_messages_rx_errors.get(); }
    uint32_t	pimstat_assert_messages_received() const { return _pimstat_assert_messages_received.get(); }
    uint32_t	pimstat_assert_messages_sent() const { return _pimstat_assert_messages_sent.get(); }
    uint32_t	pimstat_assert_messages_rx_errors() const { return _pimstat_assert_messages_rx_errors.get(); }
    uint32_t	pimstat_graft_messages_received() const { return _pimstat_graft_messages_received.get(); }
    uint32_t	pimstat_graft_messages_sent() const { return _pimstat_graft_messages_sent.get(); }
    uint32_t	pimstat_graft_messages_rx_errors() const { return _pimstat_graft_messages_rx_errors.get(); }
    uint32_t	pimstat_graft_ack_messages_received() const { return _pimstat_graft_ack_messages_received.get(); }
    uint32_t	pimstat_graft_ack_messages_sent() const { return _pimstat_graft_ack_messages_sent.get(); }
    uint32_t	pimstat_graft_ack_messages_rx_errors() const { return _pimstat_graft_ack_messages_rx_errors.get(); }
    uint32_t	pimstat_candidate_rp_messages_received() const { return _pimstat_candidate_rp_messages_received.get(); }
    uint32_t	pimstat_candidate_rp_messages_sent() const { return _pimstat_candidate_rp_messages_sent.get(); }
    uint32_t	pimstat_candidate_rp_messages_rx_errors() const { return _pimstat_candidate_rp_messages_rx_errors.get(); }
    //
    uint32_t	pimstat_unknown_type_messages() const { return _pimstat_unknown_type_messages.get(); }
    uint32_t	pimstat_unknown_version_messages() const { return _pimstat_unknown_version_messages.get(); }
    uint32_t	pimstat_neighbor_unknown_messages() const { return _pimstat_neighbor_unknown_messages.get(); }
    uint32_t	pimstat_bad_length_messages() const { return _pimstat_bad_length_messages.get(); }
    uint32_t	pimstat_bad_checksum_messages() const { return _pimstat_bad_checksum_messages.get(); }
    uint32_t	pimstat_bad_receive_interface_messages() const { return _pimstat_bad_receive_interface_messages.get(); }
    uint32_t	pimstat_rx_interface_disabled_messages() const { return _pimstat_rx_interface_disabled_messages.get(); }
    uint32_t	pimstat_rx_register_not_rp() const { return _pimstat_rx_register_not_rp.get(); }
    uint32_t	pimstat_rp_filtered_source() const { return _pimstat_rp_filtered_source.get(); }
    uint32_t	pimstat_unknown_register_stop() const { return _pimstat_unknown_register_stop.get(); }
    uint32_t	pimstat_rx_join_prune_no_state() const { return _pimstat_rx_join_prune_no_state.get(); }
    uint32_t	pimstat_rx_graft_graft_ack_no_state() const { return _pimstat_rx_graft_graft_ack_no_state.get(); }
    uint32_t	pimstat_rx_graft_on_upstream_interface() const { return _pimstat_rx_graft_on_upstream_interface.get(); }
    uint32_t	pimstat_rx_candidate_rp_not_bsr() const { return _pimstat_rx_candidate_rp_not_bsr.get(); }
    uint32_t	pimstat_rx_bsr_when_bsr() const { return _pimstat_rx_bsr_when_bsr.get(); }
    uint32_t	pimstat_rx_bsr_not_rpf_interface() const { return _pimstat_rx_bsr_not_rpf_interface.get(); }
    uint32_t	pimstat_rx_unknown_hello_option() const { return _pimstat_rx_unknown_hello_option.get(); }
    uint32_t	pimstat_rx_data_no_state() const { return _pimstat_rx_data_no_state.get(); }
    uint32_t	pimstat_rx_rp_no_state() const { return _pimstat_rx_rp_no_state.get(); }
    uint32_t	pimstat_rx_aggregate() const { return _pimstat_rx_aggregate.get(); }
    uint32_t	pimstat_rx_malformed_packet() const { return _pimstat_rx_malformed_packet.get(); }
    uint32_t	pimstat_no_rp() const { return _pimstat_no_rp.get(); }
    uint32_t	pimstat_no_route_upstream() const { return _pimstat_no_route_upstream.get(); }
    uint32_t	pimstat_rp_mismatch() const { return _pimstat_rp_mismatch.get(); }
    uint32_t	pimstat_rpf_neighbor_unknown() const { return _pimstat_rpf_neighbor_unknown.get(); }
    //
    uint32_t	pimstat_rx_join_rp() const { return _pimstat_rx_join_rp.get(); }
    uint32_t	pimstat_rx_prune_rp() const { return _pimstat_rx_prune_rp.get(); }
    uint32_t	pimstat_rx_join_wc() const { return _pimstat_rx_join_wc.get(); }
    uint32_t	pimstat_rx_prune_wc() const { return _pimstat_rx_prune_wc.get(); }
    uint32_t	pimstat_rx_join_sg() const { return _pimstat_rx_join_sg.get(); }
    uint32_t	pimstat_rx_prune_sg() const { return _pimstat_rx_prune_sg.get(); }
    uint32_t	pimstat_rx_join_sg_rpt() const { return _pimstat_rx_join_sg_rpt.get(); }
    uint32_t	pimstat_rx_prune_sg_rpt() const { return _pimstat_rx_prune_sg_rpt.get(); }
    
private:
    // Private functions
    void	hello_timer_timeout();
    void	hello_once_timer_timeout();
    
    //
    // Callbacks for configuration and non-configurable parameters
    //
    void	set_hello_period_callback(uint16_t v) {
	uint16_t old_hello_holdtime_divided
	    = (uint16_t) (_hello_holdtime.get() / PIM_HELLO_HELLO_HOLDTIME_PERIOD_RATIO);
	if (v != old_hello_holdtime_divided)
	    _hello_holdtime.set(
		(uint16_t)(v * PIM_HELLO_HELLO_HOLDTIME_PERIOD_RATIO));
	_pim_nbr_me.set_hello_holdtime(_hello_holdtime.get());
    }
    void	set_hello_holdtime_callback(uint16_t v) {
	uint16_t new_hello_period
	    = (uint16_t)(v / PIM_HELLO_HELLO_HOLDTIME_PERIOD_RATIO);
	if (_hello_period.get() != new_hello_period)
	    _hello_period.set(new_hello_period);
	_pim_nbr_me.set_hello_holdtime(_hello_holdtime.get());
    }
    void	set_dr_priority_callback(uint32_t v) {
	_pim_nbr_me.set_dr_priority(v);
	_pim_nbr_me.set_is_dr_priority_present(true);
    }
    void	set_propagation_delay_callback(uint16_t v) {
	_pim_nbr_me.set_propagation_delay(v);
	_pim_nbr_me.set_is_lan_prune_delay_present(true);
    }
    void	set_override_interval_callback(uint16_t v) {
	_pim_nbr_me.set_override_interval(v);
	_pim_nbr_me.set_is_lan_prune_delay_present(true);
    }
    void	set_is_tracking_support_disabled_callback(bool v) {
	_pim_nbr_me.set_is_tracking_support_disabled(v);
    }
    void	set_genid_callback(uint32_t v) {
	_pim_nbr_me.set_genid(v);
	_pim_nbr_me.set_is_genid_present(true);
    }
    void	set_join_prune_period_callback(uint16_t v) {
	_join_prune_holdtime.set(
	    (uint16_t)(v * PIM_JOIN_PRUNE_HOLDTIME_PERIOD_RATIO));
    }
    
    
    int jp_entry_add(const IPvX& source_addr, const IPvX& group_addr,
		     mrt_entry_type_t mrt_entry_type, action_jp_t action_jp,
		     uint16_t holdtime);
    int jp_entry_flush();
    
    bool is_send_unicast_bootstrap() const {
	return (! _send_unicast_bootstrap_nbr_list.empty());
    }
    void add_send_unicast_bootstrap_nbr(const IPvX& nbr_addr) {
	_send_unicast_bootstrap_nbr_list.push_back(nbr_addr);
    }
    const list<IPvX>& send_unicast_bootstrap_nbr_list() const {
	return (_send_unicast_bootstrap_nbr_list);
    }
    void delete_send_unicast_bootstrap_nbr_list() {
	_send_unicast_bootstrap_nbr_list.clear();
    }

    bool should_send_pim_hello() const { return (_should_send_pim_hello); }
    void set_should_send_pim_hello(bool v) { _should_send_pim_hello = v; }
    
    // Private state
    PimNode&	_pim_node;		// The PIM node I belong to
    buffer_t	*_buffer_send;		// Buffer for sending messages
    buffer_t	*_buffer_send_hello;	// Buffer for sending Hello messages
    buffer_t	*_buffer_send_bootstrap;// Buffer for sending Bootstrap msgs
    enum {
	PIM_VIF_DR	= 1 << 0	// I am the Designated Router
    };
    uint32_t	_proto_flags;		// Various flags (PIM_VIF_*)
    IPvX	_dr_addr;		// IP address of the current DR
    XorpTimer	_hello_timer;		// Timer to send a HELLO message
    XorpTimer	_hello_once_timer;	// Timer to send once a HELLO message
    list<PimNbr *> _pim_nbrs;		// List of all PIM neighbors
    PimNbr	_pim_nbr_me;		// Myself (for misc. purpose)
    IPvX	_domain_wide_addr;	// The domain-wide reachable address on
					// this vif
    list<IPvX>	_send_unicast_bootstrap_nbr_list; // List of new nbrs to
						  // unicast to them the
						  // Bootstrap message.

    // The alternative subnets on a vif. Used to make incoming traffic with a
    // non-local source address to appear as it is coming from a local subnet.
    list<IPvXNet> _alternative_subnet_list;

    //
    // Misc configuration parameters
    //
    ConfigParam<bool> _passive;
    ConfigParam<bool> _ip_router_alert_option_check; // The IP Router Alert option check flag

    //
    // Hello-related configuration parameters
    //
    ConfigParam<uint16_t> _hello_triggered_delay; // The Triggered_Hello_Delay
    ConfigParam<uint16_t> _hello_period;	// The Hello_Period
    ConfigParam<uint16_t> _hello_holdtime;	// The Hello_Holdtime
    ConfigParam<uint32_t> _dr_priority;		// The DR Priority
    ConfigParam<uint16_t> _propagation_delay;	// The Propagation_Delay
    ConfigParam<uint16_t> _override_interval;	// The Override_Interval
    ConfigParam<bool>	  _is_tracking_support_disabled; // The T-bit
    ConfigParam<bool>	  _accept_nohello_neighbors; // If true, accept
						// neighbors that didn't send
						// a Hello message first
    
    //
    // Hello-related non-configurable parameters
    //
    ConfigParam<uint32_t> _genid;		// The Generation ID
    
    //
    // Join/Prune-related configuration parameters
    //
    ConfigParam<uint16_t> _join_prune_period;	// The period between J/P msgs
    ConfigParam<uint16_t> _join_prune_holdtime;	// The holdtime in J/P msgs
    
    //
    // Assert-related configuration parameters
    //
    ConfigParam<uint32_t> _assert_time;		// The Assert_Time
    ConfigParam<uint32_t> _assert_override_interval; // The Assert_Override_Interval
    
    bool	_should_send_pim_hello;	// True if PIM_HELLO should be sent
					// before any other control messages
    
    //
    // Statistics-related counters and values
    //
    ConfigParam<uint32_t> _pimstat_hello_messages_received;
    ConfigParam<uint32_t> _pimstat_hello_messages_sent;
    ConfigParam<uint32_t> _pimstat_hello_messages_rx_errors;
    ConfigParam<uint32_t> _pimstat_register_messages_received;
    ConfigParam<uint32_t> _pimstat_register_messages_sent;
    ConfigParam<uint32_t> _pimstat_register_messages_rx_errors;
    ConfigParam<uint32_t> _pimstat_register_stop_messages_received;
    ConfigParam<uint32_t> _pimstat_register_stop_messages_sent;
    ConfigParam<uint32_t> _pimstat_register_stop_messages_rx_errors;
    ConfigParam<uint32_t> _pimstat_join_prune_messages_received;
    ConfigParam<uint32_t> _pimstat_join_prune_messages_sent;
    ConfigParam<uint32_t> _pimstat_join_prune_messages_rx_errors;
    ConfigParam<uint32_t> _pimstat_bootstrap_messages_received;
    ConfigParam<uint32_t> _pimstat_bootstrap_messages_sent;
    ConfigParam<uint32_t> _pimstat_bootstrap_messages_rx_errors;
    ConfigParam<uint32_t> _pimstat_assert_messages_received;
    ConfigParam<uint32_t> _pimstat_assert_messages_sent;
    ConfigParam<uint32_t> _pimstat_assert_messages_rx_errors;
    ConfigParam<uint32_t> _pimstat_graft_messages_received;
    ConfigParam<uint32_t> _pimstat_graft_messages_sent;
    ConfigParam<uint32_t> _pimstat_graft_messages_rx_errors;
    ConfigParam<uint32_t> _pimstat_graft_ack_messages_received;
    ConfigParam<uint32_t> _pimstat_graft_ack_messages_sent;
    ConfigParam<uint32_t> _pimstat_graft_ack_messages_rx_errors;
    ConfigParam<uint32_t> _pimstat_candidate_rp_messages_received;
    ConfigParam<uint32_t> _pimstat_candidate_rp_messages_sent;
    ConfigParam<uint32_t> _pimstat_candidate_rp_messages_rx_errors;
    //
    ConfigParam<uint32_t> _pimstat_unknown_type_messages;
    ConfigParam<uint32_t> _pimstat_unknown_version_messages;
    ConfigParam<uint32_t> _pimstat_neighbor_unknown_messages;
    ConfigParam<uint32_t> _pimstat_bad_length_messages;
    ConfigParam<uint32_t> _pimstat_bad_checksum_messages;
    ConfigParam<uint32_t> _pimstat_bad_receive_interface_messages; // XXX: unused
    ConfigParam<uint32_t> _pimstat_rx_interface_disabled_messages;
    ConfigParam<uint32_t> _pimstat_rx_register_not_rp;
    ConfigParam<uint32_t> _pimstat_rp_filtered_source;		// XXX: unused
    ConfigParam<uint32_t> _pimstat_unknown_register_stop;
    ConfigParam<uint32_t> _pimstat_rx_join_prune_no_state;	// XXX: unused
    ConfigParam<uint32_t> _pimstat_rx_graft_graft_ack_no_state;	// XXX: unused
    ConfigParam<uint32_t> _pimstat_rx_graft_on_upstream_interface; // XXX: unused
    ConfigParam<uint32_t> _pimstat_rx_candidate_rp_not_bsr;
    ConfigParam<uint32_t> _pimstat_rx_bsr_when_bsr;		// XXX: unused
    ConfigParam<uint32_t> _pimstat_rx_bsr_not_rpf_interface;
    ConfigParam<uint32_t> _pimstat_rx_unknown_hello_option;
    ConfigParam<uint32_t> _pimstat_rx_data_no_state;		// XXX: unused
    ConfigParam<uint32_t> _pimstat_rx_rp_no_state;		// XXX: unused
    ConfigParam<uint32_t> _pimstat_rx_aggregate;		// XXX: unused
    ConfigParam<uint32_t> _pimstat_rx_malformed_packet;
    ConfigParam<uint32_t> _pimstat_no_rp;			// XXX: unused
    ConfigParam<uint32_t> _pimstat_no_route_upstream;		// XXX: unused
    ConfigParam<uint32_t> _pimstat_rp_mismatch;			// XXX: unused
    ConfigParam<uint32_t> _pimstat_rpf_neighbor_unknown;	// XXX: unused
    //
    ConfigParam<uint32_t> _pimstat_rx_join_rp;
    ConfigParam<uint32_t> _pimstat_rx_prune_rp;
    ConfigParam<uint32_t> _pimstat_rx_join_wc;
    ConfigParam<uint32_t> _pimstat_rx_prune_wc;
    ConfigParam<uint32_t> _pimstat_rx_join_sg;
    ConfigParam<uint32_t> _pimstat_rx_prune_sg;
    ConfigParam<uint32_t> _pimstat_rx_join_sg_rpt;
    ConfigParam<uint32_t> _pimstat_rx_prune_sg_rpt;
    
    size_t	_usage_by_pim_mre_task;	// Counter for usage by PimMreTask
    
    // Not-so handy private functions that should go somewhere else
    // PIM control messages recv functions
    int		pim_hello_recv(PimNbr *pim_nbr, const IPvX& src,
			       const IPvX& dst, buffer_t *buffer,
			       int nbr_proto_version);
    int		pim_register_recv(PimNbr *pim_nbr, const IPvX& src,
				  const IPvX& dst, buffer_t *buffer);
    int		pim_register_stop_recv(PimNbr *pim_nbr, const IPvX& src,
				       const IPvX& dst, buffer_t *buffer);
    int		pim_join_prune_recv(PimNbr *pim_nbr, const IPvX& src,
				    const IPvX& dst, buffer_t *buffer,
				    uint8_t message_type);
    int		pim_bootstrap_recv(PimNbr *pim_nbr, const IPvX& src,
				   const IPvX& dst, buffer_t *buffer);
    int		pim_assert_recv(PimNbr *pim_nbr, const IPvX& src,
				const IPvX& dst, buffer_t *buffer);
    int		pim_graft_recv(PimNbr *pim_nbr, const IPvX& src,
			       const IPvX& dst, buffer_t *buffer);
    int		pim_graft_ack_recv(PimNbr *pim_nbr, const IPvX& src,
				   const IPvX& dst, buffer_t *buffer);
    int		pim_cand_rp_adv_recv(PimNbr *pim_nbr, const IPvX& src,
				     const IPvX& dst, buffer_t *buffer);
    
    // PIM control messages process functions
    int		pim_process(const IPvX& src, const IPvX& dst,
			    int ip_ttl, int ip_tos, bool is_router_alert,
			    buffer_t *buffer);
    int		pim_assert_process(PimNbr *pim_nbr,
				   const IPvX& src,
				   const IPvX& dst,
				   const IPvX& assert_source_addr,
				   const IPvX& assert_group_addr,
				   uint8_t assert_group_mask_len,
				   AssertMetric *assert_metric);
    int		pim_register_stop_process(const IPvX& rp_addr,
					  const IPvX& source_addr,
					  const IPvX& group_addr,
					  uint8_t group_mask_len);
};

//
// Global variables
//

//
// Global functions prototypes
//

#endif // __PIM_PIM_VIF_HH__
