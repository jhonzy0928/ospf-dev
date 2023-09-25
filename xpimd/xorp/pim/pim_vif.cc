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

#ident "$XORP: xorp/pim/pim_vif.cc,v 1.65 2007/02/16 22:46:51 pavlin Exp $"


//
// PIM virtual interfaces implementation.
//


#include "pim_module.h"
#include "libxorp/xorp.h"
#include "libxorp/xlog.h"
#include "libxorp/debug.h"
#include "libxorp/ipvx.hh"

#include "libproto/checksum.h"

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


/**
 * PimVif::PimVif:
 * @pim_node: The PIM node this interface belongs to.
 * @vif: The generic Vif interface that contains various information.
 * 
 * PIM protocol vif constructor.
 **/
PimVif::PimVif(PimNode& pim_node, const Vif& vif)
    : ProtoUnit(pim_node.family(), pim_node.module_id()),
      Vif(vif),
      _pim_node(pim_node),
      _dr_addr(pim_node.family()),
      _pim_nbr_me(*this, IPvX::ZERO(pim_node.family()), PIM_VERSION_DEFAULT),
      _domain_wide_addr(IPvX::ZERO(pim_node.family())),
      _passive(false),
      _ip_router_alert_option_check(false),
      _hello_triggered_delay(PIM_HELLO_HELLO_TRIGGERED_DELAY_DEFAULT),
      _hello_period(PIM_HELLO_HELLO_PERIOD_DEFAULT,
		    callback(this, &PimVif::set_hello_period_callback)),
      _hello_holdtime(PIM_HELLO_HELLO_HOLDTIME_DEFAULT,
		      callback(this, &PimVif::set_hello_holdtime_callback)),
      _dr_priority(PIM_HELLO_DR_PRIORITY_DEFAULT,
		   callback(this, &PimVif::set_dr_priority_callback)),
      _propagation_delay(PIM_PROPAGATION_DELAY_MSEC_DEFAULT,
			 callback(this,
				  &PimVif::set_propagation_delay_callback)),
      _override_interval(PIM_OVERRIDE_INTERVAL_MSEC_DEFAULT,
			 callback(this,
				  &PimVif::set_override_interval_callback)),
      _is_tracking_support_disabled(false,
				    callback(this,
					     &PimVif::set_is_tracking_support_disabled_callback)),
      _accept_nohello_neighbors(false),
      _genid(RANDOM(0xffffffffU),
	     callback(this, &PimVif::set_genid_callback)),
      _join_prune_period(PIM_JOIN_PRUNE_PERIOD_DEFAULT,
			 callback(this,
				  &PimVif::set_join_prune_period_callback)),
      _join_prune_holdtime(PIM_JOIN_PRUNE_HOLDTIME_DEFAULT),
      _assert_time(PIM_ASSERT_ASSERT_TIME_DEFAULT),
      _assert_override_interval(PIM_ASSERT_ASSERT_OVERRIDE_INTERVAL_DEFAULT),
      //
      _pimstat_hello_messages_received(0),
      _pimstat_hello_messages_sent(0),
      _pimstat_hello_messages_rx_errors(0),
      _pimstat_register_messages_received(0),
      _pimstat_register_messages_sent(0),
      _pimstat_register_messages_rx_errors(0),
      _pimstat_register_stop_messages_received(0),
      _pimstat_register_stop_messages_sent(0),
      _pimstat_register_stop_messages_rx_errors(0),
      _pimstat_join_prune_messages_received(0),
      _pimstat_join_prune_messages_sent(0),
      _pimstat_join_prune_messages_rx_errors(0),
      _pimstat_bootstrap_messages_received(0),
      _pimstat_bootstrap_messages_sent(0),
      _pimstat_bootstrap_messages_rx_errors(0),
      _pimstat_assert_messages_received(0),
      _pimstat_assert_messages_sent(0),
      _pimstat_assert_messages_rx_errors(0),
      _pimstat_graft_messages_received(0),
      _pimstat_graft_messages_sent(0),
      _pimstat_graft_messages_rx_errors(0),
      _pimstat_graft_ack_messages_received(0),
      _pimstat_graft_ack_messages_sent(0),
      _pimstat_graft_ack_messages_rx_errors(0),
      _pimstat_candidate_rp_messages_received(0),
      _pimstat_candidate_rp_messages_sent(0),
      _pimstat_candidate_rp_messages_rx_errors(0),
      //
      _pimstat_unknown_type_messages(0),
      _pimstat_unknown_version_messages(0),
      _pimstat_neighbor_unknown_messages(0),
      _pimstat_bad_length_messages(0),
      _pimstat_bad_checksum_messages(0),
      _pimstat_bad_receive_interface_messages(0),
      _pimstat_rx_interface_disabled_messages(0),
      _pimstat_rx_register_not_rp(0),
      _pimstat_rp_filtered_source(0),
      _pimstat_unknown_register_stop(0),
      _pimstat_rx_join_prune_no_state(0),
      _pimstat_rx_graft_graft_ack_no_state(0),
      _pimstat_rx_graft_on_upstream_interface(0),
      _pimstat_rx_candidate_rp_not_bsr(0),
      _pimstat_rx_bsr_when_bsr(0),
      _pimstat_rx_bsr_not_rpf_interface(0),
      _pimstat_rx_unknown_hello_option(0),
      _pimstat_rx_data_no_state(0),
      _pimstat_rx_rp_no_state(0),
      _pimstat_rx_aggregate(0),
      _pimstat_rx_malformed_packet(0),
      _pimstat_no_rp(0),
      _pimstat_no_route_upstream(0),
      _pimstat_rp_mismatch(0),
      _pimstat_rpf_neighbor_unknown(0),
      //
      _pimstat_rx_join_rp(0),
      _pimstat_rx_prune_rp(0),
      _pimstat_rx_join_wc(0),
      _pimstat_rx_prune_wc(0),
      _pimstat_rx_join_sg(0),
      _pimstat_rx_prune_sg(0),
      _pimstat_rx_join_sg_rpt(0),
      _pimstat_rx_prune_sg_rpt(0),
      //
      _usage_by_pim_mre_task(0)
{
    _buffer_send = BUFFER_MALLOC(BUF_SIZE_DEFAULT);
    _buffer_send_hello = BUFFER_MALLOC(BUF_SIZE_DEFAULT);
    _buffer_send_bootstrap = BUFFER_MALLOC(BUF_SIZE_DEFAULT);
    _proto_flags = 0;
    
    set_proto_version_default(PIM_VERSION_DEFAULT);
    
    set_default_config();
    
    set_should_send_pim_hello(true);
}

/**
 * PimVif::~PimVif:
 * @: 
 * 
 * PIM protocol vif destructor.
 * 
 **/
PimVif::~PimVif()
{
    string error_msg;

    stop(error_msg);
    
    BUFFER_FREE(_buffer_send);
    BUFFER_FREE(_buffer_send_hello);
    BUFFER_FREE(_buffer_send_bootstrap);
    
    // Remove all PIM neighbor entries
    while (! _pim_nbrs.empty()) {
	PimNbr *pim_nbr = _pim_nbrs.front();
	_pim_nbrs.pop_front();
	// TODO: perform the appropriate actions
	delete_pim_nbr(pim_nbr);
    }
}

/**
 * PimVif::set_default_config:
 * @: 
 * 
 * Set configuration to default values.
 **/
void
PimVif::set_default_config()
{
    // Protocol version
    set_proto_version(proto_version_default());
    
    // Hello-related configurable parameters
    hello_triggered_delay().reset();
    hello_period().reset();
    hello_holdtime().reset();
    dr_priority().reset();
    propagation_delay().reset();
    override_interval().reset();
    is_tracking_support_disabled().reset();
    accept_nohello_neighbors().reset();
    
    // Hello-related non-configurable parameters
    genid().set(RANDOM(0xffffffffU));
    
    // Join/Prune-related parameters
    _join_prune_period.reset();
    _join_prune_holdtime.reset();
}

/**
 * PimVif::set_proto_version:
 * @proto_version: The protocol version to set.
 * 
 * Set protocol version.
 * 
 * Return value: %XORP_OK is @proto_version is valid, otherwise %XORP_ERROR.
 **/
int
PimVif::set_proto_version(int proto_version)
{
    if ((proto_version < PIM_VERSION_MIN) || (proto_version > PIM_VERSION_MAX))
	return (XORP_ERROR);
    
    ProtoUnit::set_proto_version(proto_version);
    
    return (XORP_OK);
}

/**
 * PimVif::pim_mrt:
 * @: 
 * 
 * Get the PIM Multicast Routing Table.
 * 
 * Return value: A reference to the PIM Multicast Routing Table.
 **/
PimMrt&
PimVif::pim_mrt() const
{
    return (_pim_node.pim_mrt());
}

/**
 * PimVif::start:
 * @error_msg: The error message (if error).
 * 
 * Start PIM on a single virtual interface.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimVif::start(string& error_msg)
{
    if (! is_enabled())
	return (XORP_OK);

    if (is_up() || is_pending_up())
	return (XORP_OK);

    if (! is_underlying_vif_up()) {
	error_msg = "underlying vif is not UP";
	return (XORP_ERROR);
    }

    //
    // Start the vif only if it is of the appropriate type:
    // multicast-capable (loopback excluded), or PIM Register vif.
    //
    if (! ((is_multicast_capable() && (! is_loopback()))
           || is_pim_register())) {
	error_msg = "the interface is not multicast capable";
	return (XORP_ERROR);
    }

    if (!is_pim_register() &&
	update_primary_and_domain_wide_address(error_msg) < 0)
	return (XORP_ERROR);

    if (ProtoUnit::start() < 0) {
	error_msg = "internal error";
	return (XORP_ERROR);
    }
    
    //
    // Start the vif with the kernel
    //
    if (pim_node().start_protocol_kernel_vif(vif_index()) != XORP_OK) {
	error_msg = c_format("cannot start protocol vif %s with the kernel",
			     name().c_str());
	return (XORP_ERROR);
    }
    
    if (! is_pim_register()) {    
	//
	// Join the appropriate multicast groups: ALL-PIM-ROUTERS
	//
	const IPvX group1 = IPvX::PIM_ROUTERS(family());
	if (pim_node().join_multicast_group(vif_index(), group1) != XORP_OK) {
	    error_msg = c_format("cannot join group %s on vif %s",
				 cstring(group1), name().c_str());
	    return (XORP_ERROR);
	}
	
	if (!_passive.get())
	    pim_hello_start();
	
	//
	// Add MLD6/IGMP membership tracking
	//
	pim_node().add_protocol_mld6igmp(vif_index());
    }
    
    //
    // Add the tasks to take care of the PimMre processing
    //
    pim_node().pim_mrt().add_task_start_vif(vif_index());
    pim_node().pim_mrt().add_task_my_ip_address(vif_index());
    pim_node().pim_mrt().add_task_my_ip_subnet_address(vif_index());

    XLOG_INFO(pim_node().is_log_info(), "Interface started: %s%s",
	      this->str().c_str(), flags_string().c_str());
    
    return (XORP_OK);
}

/**
 * PimVif::stop:
 * @error_msg: The error message (if error).
 * 
 * Gracefully stop PIM on a single virtual interface.
 * XXX: The graceful stop will attempt to send Join/Prune, Assert, etc.
 * messages for all multicast routing entries to gracefully clean-up
 * state with neighbors.
 * XXX: After the multicast routing entries cleanup is completed,
 * PimVif::final_stop() is called to complete the job.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimVif::stop(string& error_msg)
{
    int ret_value = XORP_OK;

    if (is_down())
	return (XORP_OK);
    
    if (! (is_up() || is_pending_up() || is_pending_down())) {
	error_msg = "the vif state is not UP or PENDING_UP or PENDING_DOWN";
	return (XORP_ERROR);
    }

    if (ProtoUnit::pending_stop() < 0) {
	error_msg = "internal error";
	ret_value = XORP_ERROR;
    }

    //
    // Add the tasks to take care of the PimMre processing
    //
    pim_node().pim_mrt().add_task_stop_vif(vif_index());
    pim_node().pim_mrt().add_task_my_ip_address(vif_index());
    pim_node().pim_mrt().add_task_my_ip_subnet_address(vif_index());

    //
    // Add the shutdown operation of this vif as a shutdown task
    // for the node.
    //
    pim_node().incr_shutdown_requests_n();

    if (! is_pim_register()) {
	//
	// Delete MLD6/IGMP membership tracking
	//
	pim_node().delete_protocol_mld6igmp(vif_index());
	
	set_i_am_dr(false);
    }
    
    _dr_addr = IPvX::ZERO(family());
    
    return (ret_value);
}

/**
 * PimVif::final_stop:
 * @error_msg: The error message (if error).
 * 
 * Completely stop PIM on a single virtual interface.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimVif::final_stop(string& error_msg)
{
    int ret_value = XORP_OK;
    
    if (! (is_up() || is_pending_up() || is_pending_down())) {
	error_msg = "the vif state is not UP or PENDING_UP or PENDING_DOWN";
	return (XORP_ERROR);
    }
    
    if (! is_pim_register()) {
	//
	// Delete MLD6/IGMP membership tracking
	//
	if (is_up() || is_pending_up())
	    pim_node().delete_protocol_mld6igmp(vif_index());
	
	pim_hello_stop();
	
	set_i_am_dr(false);
    }

    //
    // XXX: we don't have to explicitly leave the multicast groups
    // we have joined on that interface, because this will happen
    // automatically when we stop the vif through the MFEA.
    //
    
    if (ProtoUnit::stop() < 0) {
	error_msg = "internal error";
	ret_value = XORP_ERROR;
    }
    
    _dr_addr = IPvX::ZERO(family());
    _hello_timer.unschedule();
    _hello_once_timer.unschedule();
    
    // Remove all PIM neighbor entries
    while (! _pim_nbrs.empty()) {
	PimNbr *pim_nbr = _pim_nbrs.front();
	_pim_nbrs.pop_front();
	// TODO: perform the appropriate actions
	delete_pim_nbr(pim_nbr);
    }
    
    //
    // Stop the vif with the kernel
    //
    if (pim_node().stop_protocol_kernel_vif(vif_index()) != XORP_OK) {
	XLOG_ERROR("Cannot stop protocol vif %s with the kernel",
		   name().c_str());
	ret_value = XORP_ERROR;
    }
    
    XLOG_INFO(pim_node().is_log_info(), "Interface stopped: %s%s",
	      this->str().c_str(), flags_string().c_str());

    //
    // Inform the node that the vif has completed the shutdown
    //
    pim_node().vif_shutdown_completed(name());

    //
    // Remove the shutdown operation of this vif as a shutdown task
    // for the node.
    //
    pim_node().decr_shutdown_requests_n();

    return (ret_value);
}

/**
 * Enable PIM on a single virtual interface.
 * 
 * If an unit is not enabled, it cannot be start, or pending-start.
 */
void
PimVif::enable()
{
    ProtoUnit::enable();

    XLOG_INFO(pim_node().is_log_info(), "Interface enabled: %s%s",
	      this->str().c_str(), flags_string().c_str());
}

/**
 * Disable PIM on a single virtual interface.
 * 
 * If an unit is disabled, it cannot be start or pending-start.
 * If the unit was runnning, it will be stop first.
 */
void
PimVif::disable()
{
    string error_msg;

    stop(error_msg);
    ProtoUnit::disable();

    XLOG_INFO(pim_node().is_log_info(), "Interface disabled: %s%s",
	      this->str().c_str(), flags_string().c_str());
}

/**
 * PimVif::pim_send:
 * @src: The message source address.
 * @dst: The message destination address.
 * @message_type: The PIM type of the message.
 * @buffer: The buffer with the rest of the message.
 * @error_msg: The error message (if error).
 * 
 * Send PIM message.
 * XXX: The beginning of the @buffer must have been reserved
 * for the PIM header.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimVif::pim_send(const IPvX& src, const IPvX& dst,
		 uint8_t message_type, buffer_t *buffer,
		 string& error_msg)
{
    uint8_t pim_vt;
    uint16_t cksum;
    uint16_t cksum2 = 0;
    int ip_tos = pim_node().default_ip_tos().get();
    int ret_value;
    size_t datalen;
    int ttl = MINTTL;
    //
    // XXX: According to newer revisions of the PIM-SM spec, the PIM-SM control
    // messages don't include the IP Router Alert option.
    //
    bool is_router_alert = false;

    if (_passive.get()) {
	XLOG_ERROR("Attempting to send %s from %s to %s on passive vif %s",
		   PIMTYPE2ASCII(message_type),
		   cstring(src), cstring(dst),
		   name().c_str());
	return (XORP_ERROR);
    }

    if (! (is_up() || is_pending_down()))
	return (XORP_ERROR);

    //
    // Some of the messages should never be send via the PIM Register vif
    //
    if (is_pim_register()) {
	switch (message_type) {
	case PIM_HELLO:
	case PIM_JOIN_PRUNE:
	case PIM_BOOTSTRAP:
	case PIM_ASSERT:
	case PIM_GRAFT:
	case PIM_GRAFT_ACK:
	    return (XORP_ERROR);	// Those messages are not allowed
	case PIM_REGISTER:
	case PIM_REGISTER_STOP:
	case PIM_CAND_RP_ADV:
	    break;			// Those messages are probably OK
	default:
	    break;
	}
    }

    //
    // Some of the messages need to be send by unicast across the domain.
    // For those messages we need to modify some of the sending values.
    //
    if (dst.is_unicast()) {
	switch (message_type) {
	case PIM_REGISTER:
	case PIM_REGISTER_STOP:
	case PIM_CAND_RP_ADV:
	    ttl = IPDEFTTL;
	    is_router_alert = false;
	    break;
	default:
	    break;
	}
    }
    
    //
    // If necessary, send first a Hello message
    //
    if (should_send_pim_hello()) {
	switch (message_type) {
	case PIM_JOIN_PRUNE:
	case PIM_BOOTSTRAP:		// XXX: not in the spec yet
	case PIM_ASSERT:
	    pim_hello_first_send();
	    break;
	default:
	    break;
	}
    }
    
    //
    // Compute the TOS
    //
    switch (message_type) {
    case PIM_REGISTER:
	//
	// If PIM Register, then copy the TOS from the inner header
	// to the outer header. Strictly speaking, we need to do it
	// only for Registers with data (i.e., not for Null Registers),
	// but for simplicity we do it for Null Registers as well.
	//
	switch (family()) {
#ifndef HOST_OS_WINDOWS
	case AF_INET:
	{
	    struct ip ip4_header;
	    
	    BUFFER_COPYGET_DATA_OFFSET(&ip4_header, buffer,
				       sizeof(struct pim) + sizeof(uint32_t),
				       sizeof(ip4_header));
	    ip_tos = ip4_header.ip_tos;
	    break;
	}
#endif
	
#ifdef HAVE_IPV6
	case AF_INET6:
	{
	    struct ip6_hdr ip6_header;
	    
	    BUFFER_COPYGET_DATA_OFFSET(&ip6_header, buffer,
				       sizeof(struct pim) + sizeof(uint32_t),
				       sizeof(ip6_header));
	    // Get the Traffic Class
	    ip_tos = (ntohl(ip6_header.ip6_flow) >> 20) & 0xff;
	    break;
	}
#endif // HAVE_IPV6
	
	default:
	    XLOG_UNREACHABLE();
	    return (XORP_ERROR);
	}
	
    default:
	break;
    }
    
    //
    // Prepare the PIM header
    //
    // TODO: XXX: PAVPAVPAV: use 'buffer = buffer_send_prepare()' ???
    // Point the buffer to the protocol header
    datalen = BUFFER_DATA_SIZE(buffer);
    BUFFER_RESET_TAIL(buffer);
    //
    pim_vt = PIM_MAKE_VT(proto_version(), message_type);
    BUFFER_PUT_OCTET(pim_vt, buffer);		// PIM version and message type
    BUFFER_PUT_OCTET(0, buffer);		// Reserved
    BUFFER_PUT_HOST_16(0, buffer);		// Zero the checksum field
    // Restore the buffer to include the data
    BUFFER_RESET_TAIL(buffer);
    BUFFER_PUT_SKIP(datalen, buffer);
    
    //
    // Compute the checksum
    //
    if (is_ipv6()) {
	//
	// XXX: The checksum for IPv6 includes the IPv6 "pseudo-header"
	// as described in RFC 2460.
	//
	size_t ph_len;
	if (message_type == PIM_REGISTER)
	    ph_len = PIM_REGISTER_HEADER_LENGTH;
	else
	    ph_len = BUFFER_DATA_SIZE(buffer);
	cksum2 = calculate_ipv6_pseudo_header_checksum(src, dst, ph_len,
						       IPPROTO_PIM);
    }
    
    // XXX: The checksum for PIM_REGISTER excludes the encapsulated data packet
    switch (message_type) {
    case PIM_REGISTER:
	cksum = inet_checksum(BUFFER_DATA_HEAD(buffer),
			      PIM_REGISTER_HEADER_LENGTH);
	break;
    default:
	cksum = inet_checksum(BUFFER_DATA_HEAD(buffer),
			      BUFFER_DATA_SIZE(buffer));
	break;
    }
    
    cksum = inet_checksum_add(cksum, cksum2);
    BUFFER_COPYPUT_INET_CKSUM(cksum, buffer, 2);	// XXX: the checksum

    XLOG_TRACE(pim_node().is_log_trace(),
	       "TX %s from %s to %s on vif %s",
	       PIMTYPE2ASCII(message_type),
	       cstring(src),
	       cstring(dst),
	       name().c_str());
    
    //
    // Send the message
    //
    ret_value = pim_node().pim_send(vif_index(), src, dst, ttl, ip_tos,
				    is_router_alert, buffer, error_msg);
    
    //
    // Actions after the message is sent
    //
    if (ret_value >= 0) {
	switch (message_type) {
	case PIM_HELLO:
	    set_should_send_pim_hello(false);
	    break;
	default:
	    break;
	}
    }
    
    //
    // Keep statistics per message type
    //
    if (ret_value >= 0) {
	switch (message_type) {
	case PIM_HELLO:
	    ++_pimstat_hello_messages_sent;
	    break;
	case PIM_REGISTER:
	    ++_pimstat_register_messages_sent;
	    break;
	case PIM_REGISTER_STOP:
	    ++_pimstat_register_stop_messages_sent;
	    break;
	case PIM_JOIN_PRUNE:
	    ++_pimstat_join_prune_messages_sent;
	    break;
	case PIM_BOOTSTRAP:
	    ++_pimstat_bootstrap_messages_sent;
	    break;
	case PIM_ASSERT:
	    ++_pimstat_assert_messages_sent;
	    break;
	case PIM_GRAFT:
	    ++_pimstat_graft_messages_sent;
	    break;
	case PIM_GRAFT_ACK:
	    ++_pimstat_graft_ack_messages_sent;
	    break;
	case PIM_CAND_RP_ADV:
	    ++_pimstat_candidate_rp_messages_sent;
	    break;
	default:
	    break;
	}
    }
    
    return (ret_value);
    
 buflen_error:
    XLOG_UNREACHABLE();
    XLOG_ERROR("TX %s from %s to %s on vif %s: "
	       "packet cannot fit into sending buffer",
	       PIMTYPE2ASCII(message_type),
	       cstring(src), cstring(dst),
	       name().c_str());
    return (XORP_ERROR);

#ifndef HOST_OS_WINDOWS
 rcvlen_error:
    // XXX: this should not happen. The only way to jump here
    // is if we are trying to send a PIM Register message that did not
    // contain an IP header, but this is not a valid PIM Register message.
    XLOG_UNREACHABLE();
    return (XORP_ERROR);
#endif
}

/**
 * PimVif::pim_recv:
 * @src: The message source address.
 * @dst: The message destination address.
 * @ip_ttl: The IP TTL of the message. If it has a negative value,
 * it should be ignored.
 * @ip_tos: The IP TOS of the message. If it has a negative value,
 * it should be ignored.
 * @is_router_alert: True if the received IP packet had the Router Alert
 * IP option set.
 * @buffer: The buffer with the received message.
 * 
 * Receive PIM message and pass it for processing.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimVif::pim_recv(const IPvX& src,
		 const IPvX& dst,
		 int ip_ttl,
		 int ip_tos,
		 bool is_router_alert,
		 buffer_t *buffer)
{
    int ret_value = XORP_ERROR;
    
    if (_passive.get())
	return (XORP_OK);

    if (! is_up()) {
	++_pimstat_rx_interface_disabled_messages;
	return (XORP_ERROR);
    }
    
    ret_value = pim_process(src, dst, ip_ttl, ip_tos, is_router_alert,
			    buffer);
    
    return (ret_value);
}

/**
 * PimVif::pim_process:
 * @src: The message source address.
 * @dst: The message destination address.
 * @ip_ttl: The IP TTL of the message. If it has a negative value,
 * it should be ignored.
 * @ip_tos: The IP TOS of the message. If it has a negative value,
 * it should be ignored.
 * @is_router_alert: True if the received IP packet had the Router Alert
 * IP option set.
 * @buffer: The buffer with the message.
 * 
 * Process PIM message and pass the control to the type-specific functions.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimVif::pim_process(const IPvX& src, const IPvX& dst,
		    int ip_ttl,
		    int ip_tos,
		    bool is_router_alert,
		    buffer_t *buffer)
{
    uint8_t pim_vt;
    uint16_t cksum;
    uint16_t cksum2 = 0;
    uint8_t message_type, proto_version;
    PimNbr *pim_nbr;
    int ret_value = XORP_ERROR;
    
    // Ignore my own PIM messages
    if (pim_node().is_my_addr(src))
	return (XORP_OK);
    
    //
    // Message length check.
    //
    if (BUFFER_DATA_SIZE(buffer) < PIM_MINLEN) {
	XLOG_WARNING("RX packet from %s to %s on vif %s: "
		     "too short data field (%u bytes)",
		     cstring(src), cstring(dst),
		     name().c_str(),
		     XORP_UINT_CAST(BUFFER_DATA_SIZE(buffer)));
	++_pimstat_bad_length_messages;
	return (XORP_ERROR);
    }
    
    //
    // Get the message type and PIM protocol version.
    // XXX: First we need the message type to verify correctly the checksum.
    //
    BUFFER_GET_OCTET(pim_vt, buffer);
    BUFFER_GET_SKIP_REVERSE(1, buffer);		// Rewind back
    message_type = PIM_VT_T(pim_vt);
    proto_version = PIM_VT_V(pim_vt);

    //
    // Some of the messages should never be received via the PIM Register vif
    //
    if (is_pim_register()) {
	switch (message_type) {
	case PIM_HELLO:
	case PIM_JOIN_PRUNE:
	case PIM_BOOTSTRAP:
	case PIM_ASSERT:
	case PIM_GRAFT:
	case PIM_GRAFT_ACK:
	    return (XORP_ERROR);	// Those messages are not allowed
	case PIM_REGISTER:
	case PIM_REGISTER_STOP:
	case PIM_CAND_RP_ADV:
	    break;			// Those messages are probably OK
	default:
	    break;
	}
    }
    
    //
    // Checksum verification.
    //
    if (is_ipv6()) {
	//
	// XXX: The checksum for IPv6 includes the IPv6 "pseudo-header"
	// as described in RFC 2460.
	//
	size_t ph_len;
	if (message_type == PIM_REGISTER)
	    ph_len = PIM_REGISTER_HEADER_LENGTH;
	else
	    ph_len = BUFFER_DATA_SIZE(buffer);
	cksum2 = calculate_ipv6_pseudo_header_checksum(src, dst, ph_len,
						       IPPROTO_PIM);
    }
    
    switch (message_type) {
    case PIM_REGISTER:
	cksum = inet_checksum(BUFFER_DATA_HEAD(buffer),
			      PIM_REGISTER_HEADER_LENGTH);
	cksum = inet_checksum_add(cksum, cksum2);
	if (cksum == 0)
	    break;
	//
	// XXX: Some non-spec compliant (the PC name for "buggy" :)
	// PIM-SM implementations compute the PIM_REGISTER
	// checksum over the whole packet instead of only the first 8 octets.
	// Hence, if the checksum fails over the first 8 octets, try over
	// the whole packet.
	//
	// FALLTHROUGH
	
    default:
	cksum = inet_checksum(BUFFER_DATA_HEAD(buffer),
			      BUFFER_DATA_SIZE(buffer));
	cksum = inet_checksum_add(cksum, cksum2);

	if (cksum == 0)
	    break;

	//
	// If this is a PIM Register packet, and if it was truncated
	// by the kernel (e.g., in some *BSD systems), then ignore the
	// checksum error.
	//
	if (message_type == PIM_REGISTER) {
	    bool is_truncated = false;

	    switch (family()) {
	    case AF_INET:
		if (BUFFER_DATA_SIZE(buffer) == PIM_REG_MINLEN)
		    is_truncated = true;
		break;
#ifdef HAVE_IPV6	
	    case AF_INET6:
		if (BUFFER_DATA_SIZE(buffer) == PIM6_REG_MINLEN)
		    is_truncated = true;
		break;
#endif // HAVE_IPV6
	    default:
		XLOG_UNREACHABLE();
		return (XORP_ERROR);
	    }

	    if (is_truncated)
		break;		// XXX: accept the truncated PIM Register
	}

	XLOG_WARNING("RX packet from %s to %s on vif %s: "
		     "checksum error",
		     cstring(src), cstring(dst),
		     name().c_str());
	++_pimstat_bad_checksum_messages;
	return (XORP_ERROR);
    }
    
    //
    // Protocol version check.
    //
    // Note about protocol version checking (based on clarification/suggestion
    // from Mark Handley).
    // The expectation is that any protocol version increase would be
    // signalled in PIM Hello messages, and newer versions would be
    // required to fall back to the version understood by everybody,
    // or refuse to communicate with older versions (as they choose).
    // Hence, we drop everything other than a PIM Hello message
    // with version greather than the largest one we understand
    // (PIM_VERSION_MAX), but we log a warning. On the other hand,
    // we don't understand anything about versions smaller than
    // PIM_VERSION_MIN, hence we drop all messages with that version.
    if ((proto_version < PIM_VERSION_MIN)
	|| ((proto_version > PIM_VERSION_MAX)
	    && (message_type != PIM_HELLO))) {
	XLOG_WARNING("RX %s from %s to %s on vif %s: "
		     "invalid PIM version: %d",
		     PIMTYPE2ASCII(message_type),
		     cstring(src), cstring(dst),
		     name().c_str(),
		     proto_version);
	++_pimstat_unknown_version_messages;
	return (XORP_ERROR);
    }
    
    //
    // IP Router Alert option check
    //
    if (_ip_router_alert_option_check.get()) {
	switch (message_type) {
	case PIM_HELLO:
	case PIM_JOIN_PRUNE:
	case PIM_ASSERT:
	case PIM_GRAFT:
	case PIM_GRAFT_ACK:
	case PIM_BOOTSTRAP:
	    if (! is_router_alert) {
		XLOG_WARNING("RX %s from %s to %s on vif %s: "
			     "missing IP Router Alert option",
			     PIMTYPE2ASCII(message_type),
			     cstring(src), cstring(dst),
			     name().c_str());
		ret_value = XORP_ERROR;
		goto ret_label;
	    }
	    //
	    // TODO: check the TTL and TOS if we are running in secure mode
	    //
	    UNUSED(ip_ttl);
	    UNUSED(ip_tos);
#if 0
	    if (ip_ttl != MINTTL) {
		XLOG_WARNING("RX %s from %s to %s on vif %s: "
			     "ip_ttl = %d instead of %d",
			     PIMTYPE2ASCII(message_type),
			     cstring(src), cstring(dst),
			     name().c_str(),
			     ip_ttl, MINTTL);
		ret_value = XORP_ERROR;
		goto ret_label;
	    }
#endif // 0
	    break;
	case PIM_REGISTER:
	case PIM_REGISTER_STOP:
	case PIM_CAND_RP_ADV:
	    // Destination should be unicast. No TTL and RA check needed.
	    break;
	default:
	    break;
	}
    }
    
    //
    // Source address check.
    //
    if (! src.is_unicast()) {
	// Source address must always be unicast
	// The kernel should have checked that, but just in case
	XLOG_WARNING("RX %s from %s to %s on vif %s: "
		     "source must be unicast",
		     PIMTYPE2ASCII(message_type),
		     cstring(src), cstring(dst),
		     name().c_str());
	ret_value = XORP_ERROR;
	goto ret_label;
    }
    if (src.af() != family()) {
	// Invalid source address family
	XLOG_WARNING("RX %s from %s to %s on vif %s: "
		     "invalid source address family "
		     "(received %d expected %d)",
		     PIMTYPE2ASCII(message_type),
		     cstring(src), cstring(dst),
		     name().c_str(),
		     src.af(), family());
    }
    switch (message_type) {
    case PIM_HELLO:
    case PIM_JOIN_PRUNE:
    case PIM_ASSERT:
    case PIM_GRAFT:
    case PIM_GRAFT_ACK:
    case PIM_BOOTSTRAP:
	// Source address must be directly connected
	if (! pim_node().is_directly_connected(*this, src)) {
	    XLOG_WARNING("RX %s from %s to %s on vif %s: "
			 "source must be directly connected",
			 PIMTYPE2ASCII(message_type),
			 cstring(src), cstring(dst),
			 name().c_str());
	    ret_value = XORP_ERROR;
	    goto ret_label;
	}
#if 0
	// TODO: this check has to be fixed in case we use GRE tunnels
	if (! src.is_linklocal_unicast()) {
	    XLOG_WARNING("RX %s from %s to %s on vif %s: "
			 "source is not a link-local address",
			 PIMTYPE2ASCII(message_type),
			 cstring(src), cstring(dst),
			 name().c_str());
	    ret_value = XORP_ERROR;
	    goto ret_label;
	}
#endif // 0/1
	break;
    case PIM_REGISTER:
    case PIM_REGISTER_STOP:
    case PIM_CAND_RP_ADV:
	// Source address can be anywhere
	// TODO: any source address check?
	break;
    default:
	break;
    }
    
    //
    // Destination address check.
    //
    if (dst.af() != family()) {
	// Invalid destination address family
	XLOG_WARNING("RX %s from %s to %s on vif %s: "
		     "invalid destination address family "
		     "(received %d expected %d)",
		     PIMTYPE2ASCII(message_type),
		     cstring(src), cstring(dst),
		     name().c_str(),
		     dst.af(), family());
    }
    switch (message_type) {
    case PIM_HELLO:
    case PIM_JOIN_PRUNE:
    case PIM_ASSERT:
    case PIM_GRAFT:
	// Destination must be multicast
	if (! dst.is_multicast()) {
	    XLOG_WARNING("RX %s from %s to %s on vif %s: "
			 "destination must be multicast",
			 PIMTYPE2ASCII(message_type),
			 cstring(src), cstring(dst),
			 name().c_str());
	    ret_value = XORP_ERROR;
	    goto ret_label;
	}
#ifdef HAVE_IPV6
	if (is_ipv6()) {
	    //
	    // TODO: Multicast address scope check for IPv6
	    //
	}
#endif  // HAVE_IPV6
	if (dst != IPvX::PIM_ROUTERS(family())) {
	    XLOG_WARNING("RX %s from %s to %s on vif %s: "
			 "destination must be ALL-PIM-ROUTERS multicast group",
			 PIMTYPE2ASCII(message_type),
			 cstring(src), cstring(dst),
			 name().c_str());
	    ret_value = XORP_ERROR;
	    goto ret_label;
	}
	break;
    case PIM_REGISTER:
    case PIM_REGISTER_STOP:
    case PIM_GRAFT_ACK:
    case PIM_CAND_RP_ADV:
	// Destination must be unicast
	if (! dst.is_unicast()) {
	    XLOG_WARNING("RX %s from %s to %s on vif %s: "
			 "destination must be unicast",
			 PIMTYPE2ASCII(message_type),
			 cstring(src), cstring(dst),
			 name().c_str());
	    ret_value = XORP_ERROR;
	    goto ret_label;
	}
	break;
	
    case PIM_BOOTSTRAP:
	// Destination can be either unicast or multicast
	if (! (dst.is_unicast() || dst.is_multicast())) {
	    XLOG_WARNING("RX %s from %s to %s on vif %s: "
			 "destination must be either unicast or multicast",
			 PIMTYPE2ASCII(message_type),
			 cstring(src), cstring(dst),
			 name().c_str());
	    ret_value = XORP_ERROR;
	    goto ret_label;
	}
#ifdef HAVE_IPV6
	if (dst.is_unicast()) {
	    // TODO: address check (if any)
	}
	if (dst.is_multicast()) {
	    if (dst != IPvX::PIM_ROUTERS(family())) {
		XLOG_WARNING("RX %s from %s to %s on vif %s: "
			     "destination must be ALL-PIM-ROUTERS multicast group",
			     PIMTYPE2ASCII(message_type),
			     cstring(src), cstring(dst),
			     name().c_str());
		ret_value = XORP_ERROR;
		goto ret_label;
	    }
	    
	    if (is_ipv6()) {
		//
		// TODO: Multicast address scope check for IPv6
		//
	    }
	}
#endif  // HAVE_IPV6
	break;
    default:
	break;
    }
    
    //
    // Message-specific checks.
    //
    switch (message_type) {
    case PIM_HELLO:
    case PIM_JOIN_PRUNE:
    case PIM_ASSERT:
	// PIM-SM and PIM-DM messages
	break;
    case PIM_REGISTER:
    case PIM_REGISTER_STOP:
    case PIM_BOOTSTRAP:
    case PIM_CAND_RP_ADV:
	// PIM-SM only messages
	if (proto_is_pimdm()) {
	    XLOG_WARNING("RX %s from %s to %s on vif %s: "
			 "message type is PIM-SM specific",
			 PIMTYPE2ASCII(message_type),
			 cstring(src), cstring(dst),
			 name().c_str());
	    ret_value = XORP_ERROR;
	    goto ret_label;
	}
	break;
    case PIM_GRAFT:
    case PIM_GRAFT_ACK:
	// PIM-DM only messages
	if (proto_is_pimsm()) {
	    XLOG_WARNING("RX %s from %s to %s on vif %s: "
			 "message type is PIM-DM specific",
			 PIMTYPE2ASCII(message_type),
			 cstring(src), cstring(dst),
			 name().c_str());
	    ret_value = XORP_ERROR;
	    goto ret_label;
	}
	break;
    default:
	XLOG_WARNING("RX %s from %s to %s on vif %s: "
		     "message type (%d) is unknown",
		     PIMTYPE2ASCII(message_type),
		     cstring(src), cstring(dst),
		     name().c_str(),
		     message_type);
	ret_value = XORP_ERROR;
	goto ret_label;
    }
    
    //
    // Origin router neighbor check.
    //
    pim_nbr = pim_nbr_find(src);
    switch (message_type) {
    case PIM_HELLO:
	// This could be a new neighbor
	break;
    case PIM_JOIN_PRUNE:
    case PIM_BOOTSTRAP:
    case PIM_ASSERT:
    case PIM_GRAFT:
    case PIM_GRAFT_ACK:
	// Those messages must be originated by a neighbor router
	if (((pim_nbr == NULL)
	     || ((pim_nbr != NULL) && (pim_nbr->is_nohello_neighbor())))
	    && accept_nohello_neighbors().get()) {
	    // We are configured to interoperate with neighbors that
	    // do not send Hello messages first.
	    // XXX: fake that we have received a Hello message with
	    // large enough Hello holdtime.
	    buffer_t *tmp_hello_buffer = BUFFER_MALLOC(BUF_SIZE_DEFAULT);
	    uint16_t tmp_default_holdtime
		= max(PIM_HELLO_HELLO_HOLDTIME_DEFAULT,
		      PIM_JOIN_PRUNE_HOLDTIME_DEFAULT);
	    bool is_nohello_neighbor = false;
	    if (pim_nbr == NULL)
		is_nohello_neighbor = true;
	    BUFFER_RESET(tmp_hello_buffer);
	    BUFFER_PUT_HOST_16(PIM_HELLO_HOLDTIME_OPTION, tmp_hello_buffer);
	    BUFFER_PUT_HOST_16(PIM_HELLO_HOLDTIME_LENGTH, tmp_hello_buffer);
	    BUFFER_PUT_HOST_16(tmp_default_holdtime, tmp_hello_buffer);
	    pim_hello_recv(pim_nbr, src, dst, tmp_hello_buffer, proto_version);
	    BUFFER_FREE(tmp_hello_buffer);
	    pim_nbr = pim_nbr_find(src);
	    if ((pim_nbr != NULL) && is_nohello_neighbor)
		pim_nbr->set_is_nohello_neighbor(is_nohello_neighbor);
	}
	if (pim_nbr == NULL) {
	    XLOG_WARNING("RX %s from %s to %s on vif %s: "
			 "sender is not a PIM-neighbor router",
			 PIMTYPE2ASCII(message_type),
			 cstring(src), cstring(dst),
			 name().c_str());
	    ++_pimstat_neighbor_unknown_messages;
	    ret_value = XORP_ERROR;
	    goto ret_label;
	}
	break;
    case PIM_REGISTER:
    case PIM_REGISTER_STOP:
    case PIM_CAND_RP_ADV:
	// Those messages may be originated by a remote router
	break;
    default:
	break;
    }
    
    XLOG_TRACE(pim_node().is_log_trace(),
	       "RX %s from %s to %s on vif %s",
	       PIMTYPE2ASCII(message_type),
	       cstring(src), cstring(dst),
	       name().c_str());
    
    /*
     * Process each message based on its type.
     */
    BUFFER_GET_SKIP(sizeof(struct pim), buffer);
    switch (message_type) {
    case PIM_HELLO:
	ret_value = pim_hello_recv(pim_nbr, src, dst, buffer, proto_version);
	break;
    case PIM_REGISTER:
	ret_value = pim_register_recv(pim_nbr, src, dst, buffer);
	break;
    case PIM_REGISTER_STOP:
	ret_value = pim_register_stop_recv(pim_nbr, src, dst, buffer);
	break;
    case PIM_JOIN_PRUNE:
	ret_value = pim_join_prune_recv(pim_nbr, src, dst, buffer,
					message_type);
	break;
    case PIM_BOOTSTRAP:
	ret_value = pim_bootstrap_recv(pim_nbr, src, dst, buffer);
	break;
    case PIM_ASSERT:
	ret_value = pim_assert_recv(pim_nbr, src, dst, buffer);
	break;
    case PIM_GRAFT:
	ret_value = pim_graft_recv(pim_nbr, src, dst, buffer);
	break;
    case PIM_GRAFT_ACK:
	ret_value = pim_graft_ack_recv(pim_nbr, src, dst, buffer);
	break;
    case PIM_CAND_RP_ADV:
	ret_value = pim_cand_rp_adv_recv(pim_nbr, src, dst, buffer);
	break;
    default:
	break;
    }
    
 ret_label:
    
    //
    // Keep statistics per message type
    //
    if (ret_value >= 0) {
	switch (message_type) {
	case PIM_HELLO:
	    ++_pimstat_hello_messages_received;
	    break;
	case PIM_REGISTER:
	    ++_pimstat_register_messages_received;
	    break;
	case PIM_REGISTER_STOP:
	    ++_pimstat_register_stop_messages_received;
	    break;
	case PIM_JOIN_PRUNE:
	    ++_pimstat_join_prune_messages_received;
	    break;
	case PIM_BOOTSTRAP:
	    ++_pimstat_bootstrap_messages_received;
	    break;
	case PIM_ASSERT:
	    ++_pimstat_assert_messages_received;
	    break;
	case PIM_GRAFT:
	    ++_pimstat_graft_messages_received;
	    break;
	case PIM_GRAFT_ACK:
	    ++_pimstat_graft_ack_messages_received;
	    break;
	case PIM_CAND_RP_ADV:
	    ++_pimstat_candidate_rp_messages_received;
	    break;
	default:
	    ++_pimstat_unknown_type_messages;
	    break;
	}
    } else {
	switch (message_type) {
	case PIM_HELLO:
	    ++_pimstat_hello_messages_rx_errors;
	    break;
	case PIM_REGISTER:
	    ++_pimstat_register_messages_rx_errors;
	    break;
	case PIM_REGISTER_STOP:
	    ++_pimstat_register_stop_messages_rx_errors;
	    break;
	case PIM_JOIN_PRUNE:
	    ++_pimstat_join_prune_messages_rx_errors;
	    break;
	case PIM_BOOTSTRAP:
	    ++_pimstat_bootstrap_messages_rx_errors;
	    break;
	case PIM_ASSERT:
	    ++_pimstat_assert_messages_rx_errors;
	    break;
	case PIM_GRAFT:
	    ++_pimstat_graft_messages_rx_errors;
	    break;
	case PIM_GRAFT_ACK:
	    ++_pimstat_graft_ack_messages_rx_errors;
	    break;
	case PIM_CAND_RP_ADV:
	    ++_pimstat_candidate_rp_messages_rx_errors;
	    break;
	default:
	    ++_pimstat_unknown_type_messages;
	    break;
	}
    }
    
    return (ret_value);
    
 rcvlen_error:    
    XLOG_UNREACHABLE();
    XLOG_WARNING("RX packet from %s to %s on vif %s: "
		 "some fields are too short",
		 cstring(src), cstring(dst),
		 name().c_str());
    ++_pimstat_rx_malformed_packet;
    return (XORP_ERROR);
    
 buflen_error:
    XLOG_UNREACHABLE();
    XLOG_WARNING("RX packet from %s to %s on vif %s: "
		 "internal error",
		 cstring(src), cstring(dst),
		 name().c_str());
    return (XORP_ERROR);
}

/**
 * PimVif::buffer_send_prepare:
 * @: 
 * 
 * Reset and prepare the default buffer for sending data.
 * 
 * Return value: The prepared buffer.
 **/
buffer_t *
PimVif::buffer_send_prepare()
{
    return (buffer_send_prepare(_buffer_send));
}

/**
 * PimVif::buffer_send_prepare:
 * @buffer: The buffer to prepare.
 * 
 * Reset and prepare buffer @buffer for sendign data.
 * 
 * Return value: The prepared buffer.
 **/
buffer_t *
PimVif::buffer_send_prepare(buffer_t *buffer)
{
    BUFFER_RESET(buffer);
    BUFFER_PUT_SKIP_PIM_HEADER(buffer);
    
    return (buffer);
    
 buflen_error:
    XLOG_UNREACHABLE();
    XLOG_ERROR("INTERNAL buffer_send_prepare() ERROR: buffer size too small");
    return (NULL);
}

/**
 * PimVif::update_primary_and_domain_wide_address:
 * @error_msg: The error message (if error).
 * 
 * Update the primary and the domain-wide reachable addresses.
 * 
 * The primary address should be a link-local unicast address, and
 * is used for transmitting the multicast control packets on the LAN.
 * The domain-wide reachable address is the address that should be
 * reachable by all PIM-SM routers in the domain
 * (e.g., the Cand-BSR, or the Cand-RP address).
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimVif::update_primary_and_domain_wide_address(string& error_msg)
{
    bool i_was_dr = false;
    IPvX primary_a(IPvX::ZERO(family()));
    IPvX domain_wide_a(IPvX::ZERO(family()));

    //
    // Reset the primary and the domain-wide addresses if they are not
    // valid anymore.
    //
    if (Vif::find_address(primary_addr()) == NULL) {
	if (primary_addr() == dr_addr()) {
	    // Reset the DR address
	    _dr_addr = IPvX::ZERO(family());
	    i_was_dr = true;
	}
	pim_nbr_me().set_primary_addr(IPvX::ZERO(family()));
    }
    if (Vif::find_address(domain_wide_addr()) == NULL)
	set_domain_wide_addr(IPvX::ZERO(family()));

    list<VifAddr>::const_iterator iter;
    for (iter = addr_list().begin(); iter != addr_list().end(); ++iter) {
	const VifAddr& vif_addr = *iter;
	const IPvX& addr = vif_addr.addr();
	if (! addr.is_unicast())
	    continue;
	if (addr.is_linklocal_unicast()) {
	    if (primary_a.is_zero())
		primary_a = addr;
	    continue;
	}
	if (addr.is_loopback())
	    continue;
	//
	// XXX: assume that everything else can be a domain-wide reachable
	// address.
	//
	if (domain_wide_a.is_zero())
	    domain_wide_a = addr;
    }

    //
    // XXX: In case of IPv6 if there is no link-local address we may try
    // to use the the domain-wide address as a primary address,
    // but the PIM-SM spec is clear that the multicast PIM messages are
    // to be originated from a link-local address.
    // Hence, only in case of IPv4 we assign the domain-wide address
    // to the primary address.
    //
    if (is_ipv4()) {
	if (primary_a.is_zero())
	    primary_a = domain_wide_a;
    }

    //
    // Check that the interface has a primary and a domain-wide reachable
    // addresses.
    //
    if (primary_addr().is_zero() && primary_a.is_zero()) {
	error_msg = "invalid primary address";
	return (XORP_ERROR);
    }
    if (domain_wide_addr().is_zero() && domain_wide_a.is_zero()) {
	error_msg = "invalid domain-wide address";
	return (XORP_ERROR);
    }

    if (primary_addr().is_zero())
	pim_nbr_me().set_primary_addr(primary_a); // Set my PimNbr address
    if (domain_wide_addr().is_zero())
	set_domain_wide_addr(domain_wide_a);

    if (i_was_dr)
	pim_dr_elect();

    return (XORP_OK);
}

void
PimVif::pim_passive()
{
    _dr_addr = IPvX::ZERO(family());
    _hello_timer.unschedule();
    _hello_once_timer.unschedule();

    // Remove all PIM neighbor entries
    while (! _pim_nbrs.empty()) {
	PimNbr *pim_nbr = _pim_nbrs.front();
	_pim_nbrs.pop_front();
	// TODO: perform the appropriate actions
	delete_pim_nbr(pim_nbr);
    }

    pim_dr_elect();
}

/**
 * PimVif::calculate_ipv6_pseudo_header_checksum:
 * @src: the source address of the pseudo-header.
 * @dst: the destination address of the pseudo-header.
 * @len: the upper-layer packet length of the pseudo-header
 * (in host-order).
 * @protocol: the upper-layer protocol number.
 * 
 * Calculate the checksum of an IPv6 "pseudo-header" as described
 * in RFC 2460.
 * 
 * Return value: the checksum of the IPv6 "pseudo-header".
 **/
uint16_t
PimVif::calculate_ipv6_pseudo_header_checksum(const IPvX& src, const IPvX& dst,
					      size_t len, uint8_t protocol)
{
    struct ip6_pseudo_hdr {
	struct in6_addr	ip6_src;	// Source address
	struct in6_addr	ip6_dst;	// Destination address
	uint32_t	ph_len;		// Upper-layer packet length
	uint8_t		ph_zero[3];	// Zero
	uint8_t		ph_next;	// Upper-layer protocol number
    } ip6_pseudo_header;	// TODO: may need __attribute__((__packed__))
    
    src.copy_out(ip6_pseudo_header.ip6_src);
    dst.copy_out(ip6_pseudo_header.ip6_dst);
    ip6_pseudo_header.ph_len = htonl(len);
    ip6_pseudo_header.ph_zero[0] = 0;
    ip6_pseudo_header.ph_zero[1] = 0;
    ip6_pseudo_header.ph_zero[2] = 0;
    ip6_pseudo_header.ph_next = protocol;
    
    uint16_t cksum = inet_checksum(
	reinterpret_cast<const uint8_t *>(&ip6_pseudo_header),
	sizeof(ip6_pseudo_header));
    
    return (cksum);
}

/**
 * PimVif::pim_nbr_find:
 * @nbr_addr: The address of the neighbor to search for.
 * 
 * Find a PIM neighbor by its address.
 * 
 * Return value: The #PimNbr entry for the neighbor if found, otherwise %NULL.
 **/
PimNbr *
PimVif::pim_nbr_find(const IPvX& nbr_addr)
{
    list<PimNbr *>::iterator iter;
    for (iter = _pim_nbrs.begin(); iter != _pim_nbrs.end(); ++iter) {
	PimNbr *pim_nbr = *iter;
	if (pim_nbr->is_my_addr(nbr_addr))
	    return (pim_nbr);
    }
    
    return (NULL);
}

void
PimVif::add_pim_nbr(PimNbr *pim_nbr)
{
    TimeVal now;
    
    TimerList::system_gettimeofday(&now);
    pim_nbr->set_startup_time(now);
    
    _pim_nbrs.push_back(pim_nbr);
}

void
PimVif::delete_pim_nbr_from_nbr_list(PimNbr *pim_nbr)
{
    list<PimNbr *>::iterator iter;
    
    iter = find(_pim_nbrs.begin(), _pim_nbrs.end(), pim_nbr);
    if (iter != _pim_nbrs.end()) {
	XLOG_TRACE(pim_node().is_log_nbr() || pim_node().is_log_trace(),
		   "Delete neighbor %s on vif %s",
		   cstring(pim_nbr->primary_addr()), name().c_str());
	_pim_nbrs.erase(iter);
    }
}

int
PimVif::delete_pim_nbr(PimNbr *pim_nbr)
{
    delete_pim_nbr_from_nbr_list(pim_nbr);
    
    if (find(pim_node().processing_pim_nbr_list().begin(),
	     pim_node().processing_pim_nbr_list().end(),
	     pim_nbr) == pim_node().processing_pim_nbr_list().end()) {
	//
	// The PimNbr is not on the processing list, hence move it there
	//
	if (pim_nbr->pim_mre_rp_list().empty()
	    && pim_nbr->pim_mre_wc_list().empty()
	    && pim_nbr->pim_mre_sg_list().empty()
	    && pim_nbr->pim_mre_sg_rpt_list().empty()
	    && pim_nbr->processing_pim_mre_rp_list().empty()
	    && pim_nbr->processing_pim_mre_wc_list().empty()
	    && pim_nbr->processing_pim_mre_sg_list().empty()
	    && pim_nbr->processing_pim_mre_sg_rpt_list().empty()) {
	    delete pim_nbr;
	} else {
	    pim_node().processing_pim_nbr_list().push_back(pim_nbr);
	    pim_node().pim_mrt().add_task_pim_nbr_changed(Vif::VIF_INDEX_INVALID,
							  pim_nbr->primary_addr());
	}
    }
    
    return (XORP_OK);
}

bool
PimVif::is_lan_delay_enabled() const
{
    list<PimNbr *>::const_iterator iter;
    for (iter = _pim_nbrs.begin(); iter != _pim_nbrs.end(); ++iter) {
	const PimNbr *pim_nbr = *iter;
	if (! pim_nbr->is_lan_prune_delay_present()) {
	    return (false);
	}
    }
    
    return (true);
}

const TimeVal&
PimVif::effective_propagation_delay() const
{
    static TimeVal tv;
    uint16_t delay;

    do {
	if (! is_lan_delay_enabled()) {
	    delay = _propagation_delay.get_initial_value();
	    break;
	}

	delay = _propagation_delay.get();
	list<PimNbr *>::const_iterator iter;
	for (iter = _pim_nbrs.begin(); iter != _pim_nbrs.end(); ++iter) {
	    PimNbr *pim_nbr = *iter;
	    if (pim_nbr->propagation_delay() > delay)
		delay = pim_nbr->propagation_delay();
	}

	break;
    } while (false);

    // XXX: delay is in milliseconds
    tv = TimeVal(delay / 1000, (delay % 1000) * 1000);

    return (tv);
}

const TimeVal&
PimVif::effective_override_interval() const
{
    static TimeVal tv;
    uint16_t delay;

    do {
	if (! is_lan_delay_enabled()) {
	    delay = _override_interval.get_initial_value();
	    break;
	}

	delay = _override_interval.get();
	list<PimNbr *>::const_iterator iter;
	for (iter = _pim_nbrs.begin(); iter != _pim_nbrs.end(); ++iter) {
	    PimNbr *pim_nbr = *iter;
	    if (pim_nbr->override_interval() > delay)
		delay = pim_nbr->override_interval();
	}

	break;
    } while (false);

    // XXX: delay is in milliseconds
    tv = TimeVal(delay / 1000, (delay % 1000) * 1000);

    return (tv);
}

bool
PimVif::is_lan_suppression_state_enabled() const
{
    if (! is_lan_delay_enabled())
	return (true);
    
    list<PimNbr *>::const_iterator iter;
    for (iter = _pim_nbrs.begin(); iter != _pim_nbrs.end(); ++iter) {
	PimNbr *pim_nbr = *iter;
	if (! pim_nbr->is_tracking_support_disabled()) {
	    return (true);
	}
    }
    
    return (false);
}

//
// Compute the randomized 't_suppressed' interval:
// t_suppressed = rand(1.1 * t_periodic, 1.4 * t_periodic) when
//			Suppression_Enabled(I) is true, 0 otherwise
//
const TimeVal&
PimVif::upstream_join_timer_t_suppressed() const
{
    static TimeVal tv;
    
    if (is_lan_suppression_state_enabled()) {
	tv = TimeVal(_join_prune_period.get(), 0);
	tv = random_uniform(
	    tv * PIM_JOIN_PRUNE_SUPPRESSION_TIMEOUT_RANDOM_FACTOR_MIN,
	    tv * PIM_JOIN_PRUNE_SUPPRESSION_TIMEOUT_RANDOM_FACTOR_MAX);
    } else {
	tv = TimeVal::ZERO();
    }
    
    return (tv);
}

//
// Compute the randomized 't_override' interval value for Upstream Join Timer:
// t_override = rand(0, Effective_Override_Interval(I))
//
const struct TimeVal&
PimVif::upstream_join_timer_t_override() const
{
    static TimeVal tv;
    
    // XXX: explicitly assign the value to 'tv' every time this method
    // is called, because 'tv' is static.
    tv = effective_override_interval();
    
    // Randomize
    tv = random_uniform(tv);
    
    return (tv);
}

// Return the J/P_Override_Interval
const TimeVal&
PimVif::jp_override_interval() const
{
    static TimeVal tv;
    TimeVal res1, res2;
    
    res1 = effective_propagation_delay();
    res2 = effective_override_interval();
    tv = res1 + res2;
    
    return (tv);
}

/**
 * PimVif::i_am_dr:
 * @: 
 * 
 * Test if the protocol instance is a DR (Designated Router)
 * on a virtual interface.
 * 
 * Return value: True if the protocol instance is DR on a virtual interface,
 * otherwise false.
 **/
bool
PimVif::i_am_dr() const
{
    if (_proto_flags & PIM_VIF_DR)
	return (true);
    else
	return (false);
}

/**
 * PimVif::set_i_am_dr:
 * @v: If true, set the %PIM_VIF_DR flag, otherwise reset it.
 * 
 * Set/reset the %PIM_VIF_DR (Designated Router) flag on a virtual interface.
 **/
void
PimVif::set_i_am_dr(bool v)
{
    if (v) {
	_proto_flags |= PIM_VIF_DR;
    } else {
	_proto_flags &= ~PIM_VIF_DR;
    }
    pim_node().set_pim_vifs_dr(vif_index(), v);
}

void
PimVif::incr_usage_by_pim_mre_task()
{
    _usage_by_pim_mre_task++;
}

void
PimVif::decr_usage_by_pim_mre_task()
{
    string error_msg;

    XLOG_ASSERT(_usage_by_pim_mre_task > 0);
    _usage_by_pim_mre_task--;
    
    if (_usage_by_pim_mre_task == 0) {
	if (is_pending_down()) {
	    final_stop(error_msg);
	}
    }
}

void
PimVif::add_alternative_subnet(const IPvXNet& subnet)
{
    list<IPvXNet>::iterator iter;

    iter = find(_alternative_subnet_list.begin(),
		_alternative_subnet_list.end(),
		subnet);
    if (iter != _alternative_subnet_list.end())
	return;		// Already added

    _alternative_subnet_list.push_back(subnet);

    //
    // Add the tasks to take care of the PimMre processing
    //
    pim_node().pim_mrt().add_task_my_ip_subnet_address(vif_index());
}

void
PimVif::delete_alternative_subnet(const IPvXNet& subnet)
{
    list<IPvXNet>::iterator iter;

    iter = find(_alternative_subnet_list.begin(),
		_alternative_subnet_list.end(),
		subnet);
    if (iter == _alternative_subnet_list.end())
	return;		// No such subnet

    _alternative_subnet_list.erase(iter);

    //
    // Add the tasks to take care of the PimMre processing
    //
    pim_node().pim_mrt().add_task_my_ip_subnet_address(vif_index());
}

void
PimVif::remove_all_alternative_subnets()
{
    if (_alternative_subnet_list.empty())
	return;		// No alternative subnets to remove

    _alternative_subnet_list.clear();

    //
    // Add the tasks to take care of the PimMre processing
    //
    pim_node().pim_mrt().add_task_my_ip_subnet_address(vif_index());
}

// TODO: temporary here. Should go to the Vif class after the Vif
// class starts using the ProtoUnit class
string
PimVif::flags_string() const
{
    string flags;
    
    if (is_up())
	flags += " UP";
    if (is_down())
	flags += " DOWN";
    if (is_pending_up())
	flags += " PENDING_UP";
    if (is_pending_down())
	flags += " PENDING_DOWN";
    if (is_ipv4())
	flags += " IPv4";
    if (is_ipv6())
	flags += " IPv6";
    if (is_enabled())
	flags += " ENABLED";
    if (is_disabled())
	flags += " DISABLED";
    
    return (flags);
}

void
PimVif::clear_pim_statistics()
{
    _pimstat_hello_messages_received.reset();
    _pimstat_hello_messages_sent.reset();
    _pimstat_hello_messages_rx_errors.reset();
    _pimstat_register_messages_received.reset();
    _pimstat_register_messages_sent.reset();
    _pimstat_register_messages_rx_errors.reset();
    _pimstat_register_stop_messages_received.reset();
    _pimstat_register_stop_messages_sent.reset();
    _pimstat_register_stop_messages_rx_errors.reset();
    _pimstat_join_prune_messages_received.reset();
    _pimstat_join_prune_messages_sent.reset();
    _pimstat_join_prune_messages_rx_errors.reset();
    _pimstat_bootstrap_messages_received.reset();
    _pimstat_bootstrap_messages_sent.reset();
    _pimstat_bootstrap_messages_rx_errors.reset();
    _pimstat_assert_messages_received.reset();
    _pimstat_assert_messages_sent.reset();
    _pimstat_assert_messages_rx_errors.reset();
    _pimstat_graft_messages_received.reset();
    _pimstat_graft_messages_sent.reset();
    _pimstat_graft_messages_rx_errors.reset();
    _pimstat_graft_ack_messages_received.reset();
    _pimstat_graft_ack_messages_sent.reset();
    _pimstat_graft_ack_messages_rx_errors.reset();
    _pimstat_candidate_rp_messages_received.reset();
    _pimstat_candidate_rp_messages_sent.reset();
    _pimstat_candidate_rp_messages_rx_errors.reset();
    //
    _pimstat_unknown_type_messages.reset();
    _pimstat_unknown_version_messages.reset();
    _pimstat_neighbor_unknown_messages.reset();
    _pimstat_bad_length_messages.reset();
    _pimstat_bad_checksum_messages.reset();
    _pimstat_bad_receive_interface_messages.reset();
    _pimstat_rx_interface_disabled_messages.reset();
    _pimstat_rx_register_not_rp.reset();
    _pimstat_rp_filtered_source.reset();
    _pimstat_unknown_register_stop.reset();
    _pimstat_rx_join_prune_no_state.reset();
    _pimstat_rx_graft_graft_ack_no_state.reset();
    _pimstat_rx_graft_on_upstream_interface.reset();
    _pimstat_rx_candidate_rp_not_bsr.reset();
    _pimstat_rx_bsr_when_bsr.reset();
    _pimstat_rx_bsr_not_rpf_interface.reset();
    _pimstat_rx_unknown_hello_option.reset();
    _pimstat_rx_data_no_state.reset();
    _pimstat_rx_rp_no_state.reset();
    _pimstat_rx_aggregate.reset();
    _pimstat_rx_malformed_packet.reset();
    _pimstat_no_rp.reset();
    _pimstat_no_route_upstream.reset();
    _pimstat_rp_mismatch.reset();
    _pimstat_rpf_neighbor_unknown.reset();
    //
    _pimstat_rx_join_rp.reset();
    _pimstat_rx_prune_rp.reset();
    _pimstat_rx_join_wc.reset();
    _pimstat_rx_prune_wc.reset();
    _pimstat_rx_join_sg.reset();
    _pimstat_rx_prune_sg.reset();
    _pimstat_rx_join_sg_rpt.reset();
    _pimstat_rx_prune_sg_rpt.reset();
}
