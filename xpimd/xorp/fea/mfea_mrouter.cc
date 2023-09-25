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

#ident "$XORP: xorp/fea/mfea_mrouter.cc,v 1.50 2007/02/16 22:45:45 pavlin Exp $"

//
// Multicast routing kernel-access specific implementation.
//

#include "mfea_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"
#include "libxorp/debug.h"
#include "libxorp/ipvx.hh"
#include "libxorp/utils.hh"

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif
#ifdef HAVE_NET_IF_VAR_H
#include <net/if_var.h>
#endif
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif
#ifdef HAVE_NETINET_IP_H
#include <netinet/ip.h>
#endif
#ifdef HAVE_NETINET_IP6_H
#include <netinet/ip6.h>
#endif
#ifdef HAVE_NETINET_ICMP6_H
#include <netinet/icmp6.h>
#endif
#ifdef HAVE_NETINET6_IN6_VAR_H
#include <netinet6/in6_var.h>
#endif

#include "libcomm/comm_api.h"

#include "libproto/packet.hh"

#include "mrt/include/ip_mroute.h"
#include "mrt/max_vifs.h"
#include "mrt/multicast_defs.h"

#include "mfea_node.hh"
#include "mfea_vif.hh"
#include "mfea_kernel_messages.hh"
#include "mfea_osdep.hh"
#include "mfea_mrouter.hh"
#include "mfea_proto_comm.hh"


//
// Exported variables
//

//
// Static class members
//

//
// Local constants definitions
//
#define IO_BUF_SIZE		(64*1024)  // I/O buffer(s) size
#define CMSG_BUF_SIZE		(10*1024)  // 'rcvcmsgbuf' and 'sndcmsgbuf'
#define SO_RCV_BUF_SIZE_MIN	(48*1024)  // Min. socket buffer size
#define SO_RCV_BUF_SIZE_MAX	(256*1024) // Desired socket buffer size

//
// Local structures/classes, typedefs and macros
//

#ifdef HOST_OS_WINDOWS
typedef char *caddr_t;
#endif

//
// Local variables
//

//
// Local functions prototypes
//

/**
 * MfeaMrouter::MfeaMrouter:
 * @mfea_node: The MfeaNode I belong to.
 **/
MfeaMrouter::MfeaMrouter(MfeaNode& mfea_node)
    : ProtoUnit(mfea_node.family(), mfea_node.module_id()),
      _mfea_node(mfea_node)
{
    // Allocate the buffers
    _rcvbuf0 = new uint8_t[IO_BUF_SIZE];
    _sndbuf0 = new uint8_t[IO_BUF_SIZE];
    _rcvbuf1 = new uint8_t[IO_BUF_SIZE];
    _sndbuf1 = new uint8_t[IO_BUF_SIZE];
    _rcvcmsgbuf = new uint8_t[CMSG_BUF_SIZE];
    _sndcmsgbuf = new uint8_t[CMSG_BUF_SIZE];

    // Scatter/gatter array initialization
    _rcviov[0].iov_base		= (caddr_t)_rcvbuf0;
    _rcviov[1].iov_base		= (caddr_t)_rcvbuf1;
    _rcviov[0].iov_len		= IO_BUF_SIZE;
    _rcviov[1].iov_len		= IO_BUF_SIZE;
    _sndiov[0].iov_base		= (caddr_t)_sndbuf0;
    _sndiov[1].iov_base		= (caddr_t)_sndbuf1;
    _sndiov[0].iov_len		= 0;
    _sndiov[1].iov_len		= 0;

    // recvmsg() and sendmsg() related initialization
#ifndef HOST_OS_WINDOWS
    switch (family()) {
    case AF_INET:
	_rcvmh.msg_name		= (caddr_t)&_from4;
	_sndmh.msg_name		= (caddr_t)&_to4;
	_rcvmh.msg_namelen	= sizeof(_from4);
	_sndmh.msg_namelen	= sizeof(_to4);
	break;
#ifdef HAVE_IPV6
    case AF_INET6:
	_rcvmh.msg_name		= (caddr_t)&_from6;
	_sndmh.msg_name		= (caddr_t)&_to6;
	_rcvmh.msg_namelen	= sizeof(_from6);
	_sndmh.msg_namelen	= sizeof(_to6);
	break;
#endif // HAVE_IPV6
    default:
	XLOG_UNREACHABLE();
	break;
    }
    _rcvmh.msg_iov		= _rcviov;
    _sndmh.msg_iov		= _sndiov;
    _rcvmh.msg_iovlen		= 1;
    _sndmh.msg_iovlen		= 1;
    _rcvmh.msg_control		= (caddr_t)_rcvcmsgbuf;
    _sndmh.msg_control		= (caddr_t)_sndcmsgbuf;
    _rcvmh.msg_controllen	= CMSG_BUF_SIZE;
    _sndmh.msg_controllen	= 0;
#endif // ! HOST_OS_WINDOWS

    _mrt_api_mrt_mfc_flags_disable_wrongvif = false;
    _mrt_api_mrt_mfc_flags_border_vif = false;
    _mrt_api_mrt_mfc_rp = false;
    _mrt_api_mrt_mfc_bw_upcall = false;
}

MfeaMrouter::~MfeaMrouter()
{
    stop();
    
    // Free the buffers
    delete[] _rcvbuf0;
    delete[] _sndbuf0;
    delete[] _rcvbuf1;
    delete[] _sndbuf1;
    delete[] _rcvcmsgbuf;
    delete[] _sndcmsgbuf;
}

/**
 * MfeaMrouter::start:
 * @: 
 * 
 * Start the MfeaMrouter.
 * 
 * Return value: %XORP_OK on success, otherwize %XORP_ERROR.
 **/
int
MfeaMrouter::start()
{
#ifdef HOST_OS_WINDOWS
    XLOG_ERROR("Multicast routing is not supported on Windows");
    return (XORP_ERROR);
#endif

    // XXX: MfeaMrouter is automatically enabled by default
    ProtoUnit::enable();
    
    if (is_up() || is_pending_up())
	return (XORP_OK);

    if (ProtoUnit::start() < 0)
	return (XORP_ERROR);
    
#ifndef QUAGGA_MULTICAST
#ifndef HOST_OS_WINDOWS
    // Check if we have the necessary permission
    if (geteuid() != 0) {
	XLOG_ERROR("Must be root");
	exit (1);
	// return (XORP_ERROR);
    }
#endif // ! HOST_OS_WINDOWS
#endif // !QUAGGA_MULTICAST
    
    // Open kernel multicast routing access socket
    if (!open_mrouter_socket().is_valid())
	return (XORP_ERROR);
    
    // Start the multicast routing in the kernel
    if (start_mrt() < 0)
	return (XORP_ERROR);
    
    return (XORP_OK);
}

/**
 * MfeaMrouter::stop:
 * @: 
 * 
 * Stop the MfeaMrouter.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::stop()
{
    if (is_down())
	return (XORP_OK);

    if (ProtoUnit::stop() < 0)
	return (XORP_ERROR);
    
    // Stop the multicast routing in the kernel
    stop_mrt();
    
    // Close kernel multicast routing access socket
    close_mrouter_socket();
    
    return (XORP_OK);
}

/**
 * Test if the underlying system supports IPv4 multicast routing.
 * 
 * @return true if the underlying system supports IPv4 multicast routing,
 * otherwise false.
 */
bool
MfeaMrouter::have_multicast_routing4() const
{
#ifndef HAVE_IPV4_MULTICAST_ROUTING
    return (false);
#else
    int s;
    int mrouter_version = 1;	// XXX: hardcoded version
    
    if (! is_ipv4())
	return (false);		// Wrong family
    
    //
    // Test to open and initialize a mrouter socket. If success,
    // then we support multicast routing.
    //
    if (mrouter_socket() >= 0)
	return (true);		// XXX: already have an open mrouter socket
    
    if (kernel_mrouter_ip_protocol() < 0)
	return (false);
    
    s = socket(family(), SOCK_RAW, kernel_mrouter_ip_protocol());
    if (s < 0)
	return (false);		// Failure to open the socket
    
    if (setsockopt(s, IPPROTO_IP, MRT_INIT,
		   (void *)&mrouter_version, sizeof(mrouter_version))
	< 0) {
	close(s);
	return (false);
    }
    
    // Success
    close(s);
    return (true);
#endif // HAVE_IPV4_MULTICAST_ROUTING
}

/**
 * Test if the underlying system supports IPv6 multicast routing.
 * 
 * @return true if the underlying system supports IPv6 multicast routing,
 * otherwise false.
 */
bool
MfeaMrouter::have_multicast_routing6() const
{
#ifndef HAVE_IPV6_MULTICAST_ROUTING
    return (false);
#else
    int s;
    int mrouter_version = 1;	// XXX: hardcoded version
    
    if (! is_ipv6())
	return (false);		// Wrong family
    
    //
    // Test to open and initialize a mrouter socket. If success,
    // then we support multicast routing.
    //
    if (mrouter_socket() >= 0)
	return (true);		// XXX: already have an open mrouter socket
    
    if (kernel_mrouter_ip_protocol() < 0)
	return (false);
    
    s = socket(family(), SOCK_RAW, kernel_mrouter_ip_protocol());
    if (s < 0)
	return (false);		// Failure to open the socket
    
    if (setsockopt(s, IPPROTO_IPV6, MRT6_INIT,
		   (void *)&mrouter_version, sizeof(mrouter_version))
	< 0) {
	close(s);
	return (false);
    }
    
    // Success
    close(s);
    return (true);
#endif // HAVE_IPV6_MULTICAST_ROUTING
}

/**
 * MfeaMrouter::kernel_mrouter_ip_protocol:
 * @: 
 * 
 * Get the protocol that would be used in case of mrouter socket.
 * 
 * Return value: the protocol number on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::kernel_mrouter_ip_protocol() const
{
    switch (family()) {
    case AF_INET:
	return (IPPROTO_IGMP);
#ifdef HAVE_IPV6
    case AF_INET6:
	return (IPPROTO_ICMPV6);
#endif // HAVE_IPV6
    default:
	XLOG_UNREACHABLE();
	return (XORP_ERROR);
    }
    
    return (XORP_ERROR);
}

/**
 * MfeaMrouter::open_mrouter_socket:
 * @: 
 * 
 * Open the mrouter socket (used for various multicast routing kernel calls).
 * 
 * Return value: The socket value.
 **/
XorpFd
MfeaMrouter::open_mrouter_socket()
{
    XorpFd badfd;

    if (_mrouter_socket.is_valid())
	return (_mrouter_socket);

    if (kernel_mrouter_ip_protocol() < 0)
	return (badfd);
    
    //
    // XXX: if we have already IGMP or ICMPV6 socket, then reuse it
    //
    do {
	ProtoComm *proto_comm;
	proto_comm = mfea_node().proto_comm_find_by_ip_protocol(kernel_mrouter_ip_protocol());
	if (proto_comm == NULL)
	    break;
	_mrouter_socket = proto_comm->proto_socket_in();
	if (_mrouter_socket.is_valid())
	    return (_mrouter_socket);
	break;
    } while (false);
    
#ifdef QUAGGA_MULTICAST
    if (mfea_node().raise_privileges())
	XLOG_ERROR("could not raise privileges");
#endif	// QUAGGA_MULTICAST
    _mrouter_socket = socket(family(), SOCK_RAW, kernel_mrouter_ip_protocol());
#ifdef QUAGGA_MULTICAST
    if (mfea_node().lower_privileges())
	XLOG_ERROR("could not lower privileges");
#endif	// QUAGGA_MULTICAST
    if (!_mrouter_socket.is_valid()) {
	XLOG_ERROR("Cannot open mrouter socket");
	return (badfd);
    }
    
    if (!adopt_mrouter_socket().is_valid())
	return (badfd);
    
    return (_mrouter_socket);
}

/**
 * MfeaMrouter::adopt_mrouter_socket:
 * @: 
 * 
 * Adopt control over the mrouter socket.
 * When the #MfeaMrouter adopts control over the mrouter socket,
 * it is the one that will be reading from that socket.
 * 
 * Return value: The adopted socket value.
 **/
XorpFd
MfeaMrouter::adopt_mrouter_socket()
{
    XorpFd badfd;

    if (!_mrouter_socket.is_valid())
	return (badfd);
    
    XLOG_ASSERT(is_up());
    
    // Set receiving buffer size
    if (comm_sock_set_rcvbuf(_mrouter_socket, SO_RCV_BUF_SIZE_MAX,
			     SO_RCV_BUF_SIZE_MIN)
	< SO_RCV_BUF_SIZE_MIN) {
	comm_close(_mrouter_socket);
	_mrouter_socket.clear();
	return (badfd);
    }
    
    // Protocol-specific setup
    switch (family()) {
    case AF_INET:
#ifndef HAVE_IPV4_MULTICAST_ROUTING
	XLOG_ERROR("adopt_mrouter_socket() failed: "
		   "IPv4 multicast routing not supported");
	return (badfd);
#endif
	break;
#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	XLOG_ERROR("adopt_mrouter_socket() failed: "
		   "IPv6 multicast routing not supported");
	return (badfd);
#else
	struct icmp6_filter filter;
	// Filter all ICMPv6 messages
	ICMP6_FILTER_SETBLOCKALL(&filter);
	if (setsockopt(_mrouter_socket, IPPROTO_ICMPV6, ICMP6_FILTER,
		       (void *)&filter, sizeof(filter)) < 0) {
	    XLOG_ERROR("setsockopt(ICMP6_FILTER) failed: %s", strerror(errno));
	    comm_close(_mrouter_socket);
	    _mrouter_socket.clear();
	    return (badfd);
	}
#endif // HAVE_IPV6_MULTICAST_ROUTING
    }
    break;
#endif // HAVE_IPV6
    default:
	XLOG_UNREACHABLE();
	return (badfd);
    }
    
    // Assign a method to read from this socket
    if (mfea_node().eventloop().add_ioevent_cb(_mrouter_socket, IOT_READ,
				callback(this,
					 &MfeaMrouter::mrouter_socket_read))
	== false) {
	XLOG_ERROR("Cannot add mrouter socket to the set of sockets "
		   "to read from in the event loop");
	comm_close(_mrouter_socket);
	_mrouter_socket.clear();
	return (badfd);
    }
    
    return (_mrouter_socket);
}

/**
 * MfeaMrouter::close_mrouter_socket:
 * @: 
 * 
 * Close the mrouter socket.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::close_mrouter_socket()
{
    if (!_mrouter_socket.is_valid())
	return (XORP_ERROR);
    
    if (kernel_mrouter_ip_protocol() < 0)
	return (XORP_ERROR);
    
    //
    // XXX: if we have already IGMP or ICMPV6 socket, then we must be
    // using the same socket, hence don't close it.
    //
    do {
	ProtoComm *proto_comm;
	proto_comm = mfea_node().proto_comm_find_by_ip_protocol(kernel_mrouter_ip_protocol());
	if (proto_comm == NULL)
	    break;
	// If there is already IGMP or ICMPV6 socket, then don't close it.
	if (_mrouter_socket == proto_comm->proto_socket_in()) {
	    // Transfer the I/O event callback to the protocol socket
	    mfea_node().eventloop().remove_ioevent_cb(_mrouter_socket);
	    proto_comm->add_proto_socket_in_callback();
	    _mrouter_socket.clear();
	    return (XORP_OK);
	}
	break;
    } while (false);
    
    // Remove the function for reading from this socket    
    mfea_node().eventloop().remove_ioevent_cb(_mrouter_socket);
    
    // Close the socket and set its handle to an invalid value.
    if (comm_close(_mrouter_socket) == XORP_ERROR) {
	XLOG_ERROR("Cannot close mrouter socket: %s",
		    comm_get_last_error_str());
	_mrouter_socket.clear();
	return (XORP_ERROR);
    }
    
    _mrouter_socket.clear();
    
    return (XORP_OK);
}

/**
 * MfeaMrouter::start_mrt:
 * @: 
 * 
 * Start/enable the multicast routing in the kernel.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::start_mrt()
{
    int mrouter_version = 1;	// XXX: hardcoded version
#if !defined(HAVE_IPV4_MULTICAST_ROUTING) && !defined(HAVE_IPV6_MULTICAST_ROUTING)
    UNUSED(mrouter_version)
#endif
    
    switch (family()) {
    case AF_INET:
#ifndef HAVE_IPV4_MULTICAST_ROUTING
	XLOG_ERROR("start_mrt() failed: "
		   "IPv4 multicast routing not supported");
	return (XORP_ERROR);
#else
	if (setsockopt(_mrouter_socket, IPPROTO_IP, MRT_INIT,
		       (void *)&mrouter_version, sizeof(mrouter_version))
	    < 0) {
	    XLOG_ERROR("setsockopt(MRT_INIT, %u) failed: %s",
		       mrouter_version, strerror(errno));
	    return (XORP_ERROR);
	}
#endif // HAVE_IPV4_MULTICAST_ROUTING
	break;

#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	XLOG_ERROR("start_mrt() failed: "
		   "IPv6 multicast routing not supported");
	return (XORP_ERROR);
#else
	if (setsockopt(_mrouter_socket, IPPROTO_IPV6, MRT6_INIT,
		       (void *)&mrouter_version, sizeof(mrouter_version))
	    < 0) {
	    XLOG_ERROR("setsockopt(MRT6_INIT, %u) failed: %s",
		       mrouter_version, strerror(errno));
	    return (XORP_ERROR);
	}
#endif // HAVE_IPV6_MULTICAST_ROUTING
    }
    break;
#endif // HAVE_IPV6

    default:
	XLOG_UNREACHABLE();
	return (XORP_ERROR);
    }
    
    //
    // Configure advanced multicast API
    //
#if defined(MRT_API_CONFIG) && defined(ENABLE_ADVANCED_MULTICAST_API)
    if (family() == AF_INET) {
#ifndef HAVE_IPV4_MULTICAST_ROUTING
	XLOG_ERROR("start_mrt() failed: "
		   "IPv4 multicast routing not supported");
	return (XORP_ERROR);
#else
	uint32_t mrt_api = 0;
	
	//
	// Set the desired API
	//
#ifdef MRT_MFC_FLAGS_DISABLE_WRONGVIF
	// Try to enable the support for disabling WRONGVIF signals per vif
	mrt_api |= MRT_MFC_FLAGS_DISABLE_WRONGVIF;
#endif
#ifdef MRT_MFC_FLAGS_BORDER_VIF
	// Try to enable the border bit flag (per MFC per vif)
	mrt_api |= MRT_MFC_FLAGS_BORDER_VIF;
#endif
#ifdef MRT_MFC_RP
	// Try to enable kernel-level PIM Register encapsulation
	mrt_api |= MRT_MFC_RP;
#endif
#ifdef MRT_MFC_BW_UPCALL
	// Try to enable bandwidth-related upcalls from the kernel
	mrt_api |= MRT_MFC_BW_UPCALL;
#endif
	
	//
	// Try to configure the kernel with the desired API
	//
	if (setsockopt(_mrouter_socket, IPPROTO_IP, MRT_API_CONFIG,
		       (void *)&mrt_api, sizeof(mrt_api)) < 0) {
	    XLOG_ERROR("setsockopt(MRT_API_CONFIG) failed: %s",
		       strerror(errno));
	    return (XORP_ERROR);
	}
	
	//
	// Test which of the desired API support succeded
	//
#ifdef MRT_MFC_FLAGS_DISABLE_WRONGVIF
	// Test the support for disabling WRONGVIF signals per vif
	if (mrt_api & MRT_MFC_FLAGS_DISABLE_WRONGVIF)
	    _mrt_api_mrt_mfc_flags_disable_wrongvif = true;
#endif
#ifdef MRT_MFC_FLAGS_BORDER_VIF
	// Test the support for the border bit flag (per MFC per vif)
	if (mrt_api & MRT_MFC_FLAGS_BORDER_VIF)
	    _mrt_api_mrt_mfc_flags_border_vif = true;
#endif
#ifdef MRT_MFC_RP
	// Test the support for kernel-level PIM Register encapsulation
	if (mrt_api & MRT_MFC_RP)
	    _mrt_api_mrt_mfc_rp = true;
#endif
#ifdef MRT_MFC_BW_UPCALL
	// Test the support for bandwidth-related upcalls from the kernel
	if (mrt_api & MRT_MFC_BW_UPCALL)
	    _mrt_api_mrt_mfc_bw_upcall = true;
#endif

#endif // HAVE_IPV4_MULTICAST_ROUTING	
	
    }
#endif // MRT_API_CONFIG && ENABLE_ADVANCED_MULTICAST_API
    
#if defined(MRT6_API_CONFIG) && defined(ENABLE_ADVANCED_MULTICAST_API)
#ifdef HAVE_IPV6
    if (family == AF_INET6) {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	XLOG_ERROR("start_mrt() failed: "
		   "IPv6 multicast routing not supported");
	return (XORP_ERROR);
#else
	
	uint32_t mrt_api = 0;
	
	//
	// Set the desired API
	//
#ifdef MRT6_MFC_FLAGS_DISABLE_WRONGVIF
	// Try to enable the support for disabling WRONGVIF signals per vif
	mrt_api |= MRT6_MFC_FLAGS_DISABLE_WRONGVIF;
#endif
#ifdef MRT6_MFC_FLAGS_BORDER_VIF
	// Try to enable the border bit flag (per MFC per vif)
	mrt_api |= MRT6_MFC_FLAGS_BORDER_VIF;
#endif
#ifdef MRT6_MFC_RP
	// Try to enable kernel-level PIM Register encapsulation
	mrt_api |= MRT6_MFC_RP;
#endif
#ifdef MRT6_MFC_BW_UPCALL
	// Try to enable bandwidth-related upcalls from the kernel
	mrt_api |= MRT6_MFC_BW_UPCALL;
#endif
	
	//
	// Try to configure the kernel with the desired API
	//
	if (setsockopt(_mrouter_socket, IPPROTO_IP, MRT6_API_CONFIG,
		       (void *)&mrt_api, sizeof(mrt_api)) < 0) {
	    XLOG_ERROR("setsockopt(MRT6_API_CONFIG) failed: %s",
		       strerror(errno));
	    return (XORP_ERROR);
	}
	
	//
	// Test which of the desired API support succeded
	//
#ifdef MRT6_MFC_FLAGS_DISABLE_WRONGVIF
	// Test the support for disabling WRONGVIF signals per vif
	if (mrt_api & MRT6_MFC_FLAGS_DISABLE_WRONGVIF)
	    _mrt_api_mrt_mfc_flags_disable_wrongvif = true;
#endif
#ifdef MRT6_MFC_FLAGS_BORDER_VIF
	// Test the support for the border bit flag (per MFC per vif)
	if (mrt_api & MRT6_MFC_FLAGS_BORDER_VIF)
	    _mrt_api_mrt_mfc_flags_border_vif = true;
#endif
#ifdef MRT6_MFC_RP
	// Test the support for kernel-level PIM Register encapsulation
	if (mrt_api & MRT6_MFC_RP)
	    _mrt_api_mrt_mfc_rp = true;
#endif
#ifdef MRT6_MFC_BW_UPCALL
	// Test the support for bandwidth-related upcalls from the kernel
	if (mrt_api & MRT6_MFC_BW_UPCALL)
	    _mrt_api_mrt_mfc_bw_upcall = true;
#endif
	
#endif // HAVE_IPV6_MULTICAST_ROUTING	
    }
#endif // HAVE_IPV6
#endif // MRT6_API_CONFIG && ENABLE_ADVANCED_MULTICAST_API
    
    return (XORP_OK);
}


/**
 * MfeaMrouter::stop_mrt:
 * @: 
 * 
 * Stop/disable the multicast routing in the kernel.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::stop_mrt()
{
    _mrt_api_mrt_mfc_flags_disable_wrongvif = false;
    _mrt_api_mrt_mfc_flags_border_vif = false;
    _mrt_api_mrt_mfc_rp = false;
    _mrt_api_mrt_mfc_bw_upcall = false;
    
    if (!_mrouter_socket.is_valid())
	return (XORP_ERROR);
    
    switch (family()) {
    case AF_INET:
#ifndef HAVE_IPV4_MULTICAST_ROUTING
	XLOG_ERROR("stop_mrt() failed: "
		   "IPv4 multicast routing not supported");
	return (XORP_ERROR);
#else
	if (setsockopt(_mrouter_socket, IPPROTO_IP, MRT_DONE, NULL, 0)
	    < 0) {
	    XLOG_ERROR("setsockopt(MRT_DONE) failed: %s", strerror(errno));
	    return (XORP_ERROR);
	}
#endif // HAVE_IPV4_MULTICAST_ROUTING
	break;
#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	XLOG_ERROR("stop_mrt() failed: "
		   "IPv6 multicast routing not supported");
	return (XORP_ERROR);
#else
	if (setsockopt(_mrouter_socket, IPPROTO_IPV6, MRT6_DONE, NULL, 0)
	    < 0) {
	    XLOG_ERROR("setsockopt(MRT6_DONE) failed: %s", strerror(errno));
	    return (XORP_ERROR);
	}
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


/**
 * MfeaMrouter::start_pim:
 * @: 
 * 
 * Start/enable PIM routing in the kernel.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::start_pim()
{
    int v = 1;

    switch (family()) {
    case AF_INET:
#ifndef HAVE_IPV4_MULTICAST_ROUTING
	XLOG_ERROR("start_pim() failed: "
		   "IPv4 multicast routing not supported");
	return (XORP_ERROR);
#else
	if (setsockopt(_mrouter_socket, IPPROTO_IP, MRT_PIM,
		       (void *)&v, sizeof(v)) < 0) {
	    XLOG_ERROR("setsockopt(MRT_PIM, %u) failed: %s",
		       v, strerror(errno));
	    return (XORP_ERROR);
	}
#endif // HAVE_IPV4_MULTICAST_ROUTING
	break;

#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	XLOG_ERROR("start_pim() failed: "
		   "IPv6 multicast routing not supported");
	return (XORP_ERROR);
#else
	if (setsockopt(_mrouter_socket, IPPROTO_IPV6, MRT6_PIM,
		       (void *)&v, sizeof(v)) < 0) {
	    XLOG_ERROR("setsockopt(MRT6_PIM, %u) failed: %s",
		       v, strerror(errno));
	    return (XORP_ERROR);
	}
#endif // HAVE_IPV6_MULTICAST_ROUTING
    }
    break;
#endif // HAVE_IPV6

    default:
	UNUSED(v);
	XLOG_UNREACHABLE();
	return (XORP_ERROR);
    }

    return (XORP_OK);
}


/**
 * MfeaMrouter::stop_pim:
 * @: 
 * 
 * Stop/disable PIM routing in the kernel.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::stop_pim()
{
    int v = 0;

    if (!_mrouter_socket.is_valid())
	return (XORP_ERROR);
    
    switch (family()) {
    case AF_INET:
#ifndef HAVE_IPV4_MULTICAST_ROUTING
	XLOG_ERROR("stop_pim() failed: "
		   "IPv4 multicast routing not supported");
	return (XORP_ERROR);
#else
	v = 0;
	if (setsockopt(_mrouter_socket, IPPROTO_IP, MRT_PIM,
		       (void *)&v, sizeof(v)) < 0) {
	    XLOG_ERROR("setsockopt(MRT_PIM, %u) failed: %s",
		       v, strerror(errno));
	    return (XORP_ERROR);
	}
#endif
	break;

#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	XLOG_ERROR("stop_pim() failed: "
		   "IPv6 multicast routing not supported");
	return (XORP_ERROR);
#else
	v = 0;
	if (setsockopt(_mrouter_socket, IPPROTO_IPV6, MRT6_PIM,
		       (void *)&v, sizeof(v)) < 0) {
	    XLOG_ERROR("setsockopt(MRT6_PIM, %u) failed: %s",
		       v, strerror(errno));
	    return (XORP_ERROR);
	}
#endif // HAVE_IPV6_MULTICAST_ROUTING
    }
    break;
#endif // HAVE_IPV6

    default:
	UNUSED(v);
	XLOG_UNREACHABLE();
	return (XORP_ERROR);
    }

    return (XORP_OK);
}

/**
 * MfeaMrouter::add_multicast_vif:
 * @vif_index: The vif index of the virtual interface to add.
 * 
 * Add a virtual multicast interface to the kernel.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::add_multicast_vif(uint32_t vif_index)
{
    MfeaVif *mfea_vif = mfea_node().vif_find_by_vif_index(vif_index);
    
    if (mfea_vif == NULL)
	return (XORP_ERROR);
    
    switch (family()) {
    case AF_INET:
    {
#ifndef HAVE_IPV4_MULTICAST_ROUTING
	XLOG_ERROR("add_multicast_vif() failed: "
		   "IPv4 multicast routing not supported");
	return (XORP_ERROR);
#else
	struct vifctl vc;
	
	memset(&vc, 0, sizeof(vc));
	vc.vifc_vifi = mfea_vif->vif_index();
	// XXX: we don't (need to) support VIFF_TUNNEL; VIFF_SRCRT is obsolete
	vc.vifc_flags = 0;
	if (mfea_vif->is_pim_register())
	    vc.vifc_flags	|= VIFF_REGISTER;
	vc.vifc_threshold	= mfea_vif->min_ttl_threshold();
	vc.vifc_rate_limit	= mfea_vif->max_rate_limit();
	
	if (!mfea_vif->is_pim_register()) {
#if HAVE_VIFF_USE_IFINDEX
	    vc.vifc_lcl_ifindex = mfea_vif->pif_index();
	    vc.vifc_flags |= VIFF_USE_IFINDEX;
#else
	    if (mfea_vif->addr_ptr() == NULL) {
		XLOG_ERROR("add_multicast_vif() failed: vif %s has no address",
			   mfea_vif->name().c_str());
		return (XORP_ERROR);
	    }
	    mfea_vif->addr_ptr()->copy_out(vc.vifc_lcl_addr);
#endif // HAVE_VIFF_USE_IFINDEX
	}
	//
	// XXX: no need to copy any remote address to vc.vifc_rmt_addr,
	// because we don't (need to) support IPIP tunnels.
	//
	if (setsockopt(_mrouter_socket, IPPROTO_IP, MRT_ADD_VIF,
		       (void *)&vc, sizeof(vc)) < 0) {
	    XLOG_ERROR("setsockopt(MRT_ADD_VIF, vif %s) failed: %s",
		       mfea_vif->name().c_str(), strerror(errno));
	    return (XORP_ERROR);
	}
#endif // HAVE_IPV4_MULTICAST_ROUTING
    }
    break;
    
#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	XLOG_ERROR("add_multicast_vif() failed: "
		   "IPv6 multicast routing not supported");
	return (XORP_ERROR);
#else
	struct mif6ctl mc;
	
	memset(&mc, 0, sizeof(mc));
	mc.mif6c_mifi = mfea_vif->vif_index();
	mc.mif6c_flags = 0;
	if (mfea_vif->is_pim_register())
	    mc.mif6c_flags |= MIFF_REGISTER;
	mc.mif6c_pifi = mfea_vif->pif_index();
#if 0		// TODO: enable it after KAME's stack defines it
	mc.mif6c_threshold = mfea_vif->min_ttl_threshold();
	mc.mif6c_rate_limit = mfea_vif->max_rate_limit();
#endif // 0
	if (setsockopt(_mrouter_socket, IPPROTO_IPV6, MRT6_ADD_MIF,
		       (void *)&mc, sizeof(mc)) < 0) {
	    XLOG_ERROR("setsockopt(MRT6_ADD_MIF, vif %s) failed: %s",
		       mfea_vif->name().c_str(), strerror(errno));
	    return (XORP_ERROR);
	}
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


/**
 * MfeaMrouter::delete_multicast_vif:
 * @vif_index: The vif index of the interface to delete.
 * 
 * Delete a virtual multicast interface from the kernel.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::delete_multicast_vif(uint32_t vif_index)
{
    MfeaVif *mfea_vif = mfea_node().vif_find_by_vif_index(vif_index);
    
    if (mfea_vif == NULL)
	return (XORP_ERROR);
    
    switch (family()) {
    case AF_INET:
    {
#ifndef HAVE_IPV4_MULTICAST_ROUTING
	XLOG_ERROR("delete_multicast_vif() failed: "
		   "IPv4 multicast routing not supported");
	return (XORP_ERROR);
#else
	int ret_value = -1;

	//
	// XXX: In case of Linux, MRT_DEL_VIF expects an argument
	// of type "struct vifctl", while other systems expect
	// an argument of type "vifi_t".
	//
#ifdef HOST_OS_LINUX
	struct vifctl vc;
	memset(&vc, 0, sizeof(vc));
	vc.vifc_vifi = mfea_vif->vif_index();
	ret_value = setsockopt(_mrouter_socket, IPPROTO_IP, MRT_DEL_VIF,
			       (void *)&vc, sizeof(vc));
#else
	vifi_t vifi = mfea_vif->vif_index();
	ret_value = setsockopt(_mrouter_socket, IPPROTO_IP, MRT_DEL_VIF,
			       (void *)&vifi, sizeof(vifi));
#endif

	if (ret_value < 0) {
	    XLOG_ERROR("setsockopt(MRT_DEL_VIF, vif %s) failed: %s",
		       mfea_vif->name().c_str(), strerror(errno));
	    return (XORP_ERROR);
	}
#endif // HAVE_IPV4_MULTICAST_ROUTING
    }
    break;
    
#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	XLOG_ERROR("delete_multicast_vif() failed: "
		   "IPv6 multicast routing not supported");
	return (XORP_ERROR);
#else
	int ret_value = -1;

	//
	// XXX: In case of Linux, MRT_DEL_VIF expects an argument
	// of type "struct vifctl", while other systems expect
	// an argument of type "vifi_t".
	//
	// TODO: note that currently (2004/06/09) Linux doesn't support
	// IPv6 multicast routing, hence the above is a guess based on
	// the difference in case of IPv4.
	//
#ifdef HOST_OS_LINUX
	struct mif6ctl mc;
	memset(&mc, 0, sizeof(mc));
	mc.mif6c_mifi = mfea_vif->vif_index();
	mc.mif6c_pifi = mfea_vif->pif_index();
	ret_value = setsockopt(_mrouter_socket, IPPROTO_IP, MRT_DEL_VIF,
			       (void *)&mc, sizeof(mc));
#else
	mifi_t vifi = mfea_vif->vif_index();
	ret_value = setsockopt(_mrouter_socket, IPPROTO_IPV6, MRT6_DEL_MIF,
			       (void *)&vifi, sizeof(vifi));
#endif
	if (ret_value < 0) {
	    XLOG_ERROR("setsockopt(MRT6_DEL_MIF, vif %s) failed: %s",
		       mfea_vif->name().c_str(), strerror(errno));
	    return (XORP_ERROR);
	}
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


/**
 * MfeaMrouter::add_mfc:
 * @source: The MFC source address.
 * @group: The MFC group address.
 * @iif_vif_index: The MFC incoming interface index.
 * @oifs_ttl: An array with the min. TTL a packet should have to be forwarded.
 * @oifs_flags: An array with misc. flags for the MFC to install.
 * Note that those flags are supported only by the advanced multicast API.
 * @rp_addr: The RP address.
 * 
 * Install/modify a Multicast Forwarding Cache (MFC) entry in the kernel.
 * If the MFC entry specified by (@source, @group) pair was not
 * installed before, a new MFC entry will be created in the kernel;
 * otherwise, the existing entry's fields will be modified.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::add_mfc(const IPvX& source, const IPvX& group,
		     uint32_t iif_vif_index, uint8_t *oifs_ttl,
		     uint8_t *oifs_flags,
		     const IPvX& rp_addr)
{
    if (iif_vif_index >= mfea_node().maxvifs())
	return (XORP_ERROR);
    
    oifs_ttl[iif_vif_index] = 0;		// Pre-caution

#if !defined(HAVE_IPV4_MULTICAST_ROUTING) && !defined(HAVE_IPV6_MULTICAST_ROUTING)
    UNUSED(source);
    UNUSED(group);
#endif

    UNUSED(oifs_flags);
    UNUSED(rp_addr);

    if (mfea_node().is_log_trace()) {
	string res;
	for (uint32_t i = 0; i < mfea_node().maxvifs(); i++) {
	    if (oifs_ttl[i] > 0)
		res += "O";
	    else
		res += ".";
	}
	XLOG_TRACE(mfea_node().is_log_trace(),
		   "Add MFC entry: (%s, %s) iif = %d olist = %s",
		   cstring(source),
		   cstring(group),
		   iif_vif_index,
		   res.c_str());
    }

    switch (family()) {
    case AF_INET:
    {
#ifndef HAVE_IPV4_MULTICAST_ROUTING
	XLOG_ERROR("add_mfc() failed: "
		   "IPv4 multicast routing not supported");
	return (XORP_ERROR);
#else

#if defined(HAVE_MFCCTL2) && defined(ENABLE_ADVANCED_MULTICAST_API)
	struct mfcctl2 mc;
#else
	struct mfcctl mc;
#endif
	
	memset(&mc, 0, sizeof(mc));
	source.copy_out(mc.mfcc_origin);
	group.copy_out(mc.mfcc_mcastgrp);
	mc.mfcc_parent = iif_vif_index;
	for (uint32_t i = 0; i < mfea_node().maxvifs(); i++) {
	    mc.mfcc_ttls[i] = oifs_ttl[i];
#if defined(HAVE_MFCC_FLAGS) && defined(ENABLE_ADVANCED_MULTICAST_API)
	    mc.mfcc_flags[i] = oifs_flags[i];
#endif
	}
#if defined(HAVE_MFCC_RP) && defined(ENABLE_ADVANCED_MULTICAST_API)
	if (_mrt_api_mrt_mfc_rp)
	    rp_addr.copy_out(mc.mfcc_rp);
#endif
	
	if (setsockopt(_mrouter_socket, IPPROTO_IP, MRT_ADD_MFC,
		       (void *)&mc, sizeof(mc)) < 0) {
	    XLOG_ERROR("setsockopt(MRT_ADD_MFC, (%s, %s)) failed: %s",
		       cstring(source), cstring(group), strerror(errno));
	    return (XORP_ERROR);
	}
#endif // HAVE_IPV4_MULTICAST_ROUTING
    }
    break;
    
#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	XLOG_ERROR("add_mfc() failed: "
		   "IPv6 multicast routing not supported");
	return (XORP_ERROR);
#else
	
#if defined(HAVE_MF6CCTL2) && defined(ENABLE_ADVANCED_MULTICAST_API)
	struct mf6cctl2 mc;
#else
	struct mf6cctl mc;
#endif
	
	memset(&mc, 0, sizeof(mc));
	IF_ZERO(&mc.mf6cc_ifset);	
	source.copy_out(mc.mf6cc_origin);
	group.copy_out(mc.mf6cc_mcastgrp);
	mc.mf6cc_parent = iif_vif_index;
	for (uint32_t i = 0; i < mfea_node().maxvifs(); i++) {
	    if (oifs_ttl[i] > 0)
		IF_SET(i, &mc.mf6cc_ifset);
#if defined(HAVE_MF6CC_FLAGS) && defined(ENABLE_ADVANCED_MULTICAST_API)
	    mc.mf6cc_flags[i] = oifs_flags[i];
#endif
	}
#if defined(HAVE_MF6CC_RP) && defined(ENABLE_ADVANCED_MULTICAST_API)
	if (_mrt_api_mrt_mfc_rp)
	    rp_addr.copy_out(mc.mf6cc_rp);
#endif
	
	if (setsockopt(_mrouter_socket, IPPROTO_IPV6, MRT6_ADD_MFC,
		       (void *)&mc, sizeof(mc)) < 0) {
	    XLOG_ERROR("setsockopt(MRT6_ADD_MFC, (%s, %s)) failed: %s",
		       cstring(source), cstring(group), strerror(errno));
	    return (XORP_ERROR);
	}
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


/**
 * MfeaMrouter::delete_mfc:
 * @source: The MFC source address.
 * @group: The MFC group address.
 * 
 * Delete a Multicast Forwarding Cache (MFC) entry in the kernel.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::delete_mfc(const IPvX& source, const IPvX& group)
{
#if !defined(HAVE_IPV4_MULTICAST_ROUTING) && !defined(HAVE_IPV6_MULTICAST_ROUTING)
    UNUSED(source);
    UNUSED(group);
#endif

    XLOG_TRACE(mfea_node().is_log_trace(),
	       "Delete MFC entry: (%s, %s)",
	       cstring(source),
	       cstring(group));

    switch (family()) {
    case AF_INET:
    {
#ifndef HAVE_IPV4_MULTICAST_ROUTING
	XLOG_ERROR("delete_mfc() failed: "
		   "IPv4 multicast routing not supported");
	return (XORP_ERROR);
#else
	struct mfcctl mc;
	
	source.copy_out(mc.mfcc_origin);
	group.copy_out(mc.mfcc_mcastgrp);
	
	if (setsockopt(_mrouter_socket, IPPROTO_IP, MRT_DEL_MFC,
		       (void *)&mc, sizeof(mc)) < 0) {
	    XLOG_ERROR("setsockopt(MRT_DEL_MFC, (%s, %s)) failed: %s",
		       cstring(source), cstring(group), strerror(errno));
	    return (XORP_ERROR);
	}
#endif // HAVE_IPV4_MULTICAST_ROUTING
    }
    break;
    
#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	XLOG_ERROR("delete_mfc() failed: "
		   "IPv6 multicast routing not supported");
	return (XORP_ERROR);
#else
	
	struct mf6cctl mc;

	source.copy_out(mc.mf6cc_origin);
	group.copy_out(mc.mf6cc_mcastgrp);
	
	if (setsockopt(_mrouter_socket, IPPROTO_IPV6, MRT6_DEL_MFC,
		       (void *)&mc, sizeof(mc)) < 0) {
	    XLOG_ERROR("setsockopt(MRT6_DEL_MFC, (%s, %s)) failed: %s",
		       cstring(source), cstring(group), strerror(errno));
	    return (XORP_ERROR);
	}
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

/**
 * MfeaMrouter::add_bw_upcall:
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
 * Add a dataflow monitor entry in the kernel.
 * Note: either @is_threshold_in_packets or @is_threshold_in_bytes (or both)
 * must be true.
 * Note: either @is_geq_upcall or @is_leq_upcall (but not both) must be true.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::add_bw_upcall(const IPvX& source, const IPvX& group,
			   const TimeVal& threshold_interval,
			   uint32_t threshold_packets,
			   uint32_t threshold_bytes,
			   bool is_threshold_in_packets,
			   bool is_threshold_in_bytes,
			   bool is_geq_upcall,
			   bool is_leq_upcall,
			   string& error_msg)
{
    XLOG_TRACE(mfea_node().is_log_trace(),
	       "Add dataflow monitor: "
	       "source = %s group = %s "
	       "threshold_interval_sec = %d threshold_interval_usec = %d "
	       "threshold_packets = %d threshold_bytes = %d "
	       "is_threshold_in_packets = %d is_threshold_in_bytes = %d "
	       "is_geq_upcall = %d is_leq_upcall = %d",
	       cstring(source), cstring(group),
	       threshold_interval.sec(), threshold_interval.usec(),
	       threshold_packets, threshold_bytes,
	       is_threshold_in_packets, is_threshold_in_bytes,
	       is_geq_upcall, is_leq_upcall);

#if ! defined(ENABLE_ADVANCED_MULTICAST_API)
    UNUSED(threshold_interval);
    UNUSED(threshold_packets);
    UNUSED(threshold_bytes);
    UNUSED(is_threshold_in_packets);
    UNUSED(is_threshold_in_bytes);
    UNUSED(is_geq_upcall);
    UNUSED(is_leq_upcall);
#endif

    //
    // Check if the kernel supports the bandwidth-upcall mechanism.
    //
    if (! mrt_api_mrt_mfc_bw_upcall()) {
	error_msg = c_format("add_bw_upcall(%s, %s) failed: "
			     "dataflow monitor entry in the kernel "
			     "is not supported",
			     cstring(source), cstring(group));
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }

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
    // Do the job
    //
    switch (family()) {
    case AF_INET:
    {
#if defined(MRT_ADD_BW_UPCALL) && defined(ENABLE_ADVANCED_MULTICAST_API)
	struct bw_upcall bw_upcall;
	
	//
	// Set the argument
	//
	memset(&bw_upcall, 0, sizeof(bw_upcall));
	source.copy_out(bw_upcall.bu_src);
	group.copy_out(bw_upcall.bu_dst);
	threshold_interval.copy_out(bw_upcall.bu_threshold.b_time);
	bw_upcall.bu_threshold.b_packets = threshold_packets;
	bw_upcall.bu_threshold.b_bytes = threshold_bytes;
	if (is_threshold_in_packets)
	    bw_upcall.bu_flags |= BW_UPCALL_UNIT_PACKETS;
	if (is_threshold_in_bytes)
	    bw_upcall.bu_flags |= BW_UPCALL_UNIT_BYTES;
	do {
	    if (is_geq_upcall) {
		bw_upcall.bu_flags |= BW_UPCALL_GEQ;
		break;
	    }
	    if (is_leq_upcall) {
		bw_upcall.bu_flags |= BW_UPCALL_LEQ;
		break;
	    }
	    XLOG_UNREACHABLE();
	    return (XORP_ERROR);
	} while (false);
	
	if (setsockopt(_mrouter_socket, IPPROTO_IP, MRT_ADD_BW_UPCALL,
		       (void *)&bw_upcall, sizeof(bw_upcall)) < 0) {
	    error_msg = c_format("setsockopt(MRT_ADD_BW_UPCALL, (%s, %s)) failed: %s",
				 cstring(source), cstring(group),
				 strerror(errno));
	    XLOG_ERROR("%s", error_msg.c_str());
	    return (XORP_ERROR);
	}
#endif // MRT_ADD_BW_UPCALL && ENABLE_ADVANCED_MULTICAST_API
    }
    break;
    
#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	error_msg = c_format("add_bw_upcall() failed: "
			     "IPv6 multicast routing not supported");
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
#else
	
#if defined(MRT6_ADD_BW_UPCALL) && defined(ENABLE_ADVANCED_MULTICAST_API)
	struct bw6_upcall bw_upcall;
	
	//
	// Set the argument
	//
	memset(&bw_upcall, 0, sizeof(bw_upcall));
	source.copy_out(bw_upcall.bu6_src);
	group.copy_out(bw_upcall.bu6_dst);
	threshold_interval.copy_out(bw_upcall.bu6_threshold.b_time);
	bw_upcall.bu6_threshold.b_packets = threshold_packets;
	bw_upcall.bu6_threshold.b_bytes = threshold_bytes;
	if (is_threshold_in_packets)
	    bw_upcall.bu6_flags |= BW_UPCALL_UNIT_PACKETS;
	if (is_threshold_in_bytes)
	    bw_upcall.bu6_flags |= BW_UPCALL_UNIT_BYTES;
	do {
	    if (is_geq_upcall) {
		bw_upcall.bu6_flags |= BW_UPCALL_GEQ;
		break;
	    }
	    if (is_leq_upcall) {
		bw_upcall.bu6_flags |= BW_UPCALL_LEQ;
		break;
	    }
	    XLOG_UNREACHABLE();
	    return (XORP_ERROR);
	} while (false);
	
	if (setsockopt(_mrouter_socket, IPPROTO_IP, MRT6_ADD_BW_UPCALL,
		       (void *)&bw_upcall, sizeof(bw_upcall)) < 0) {
	    error_msg = c_format("setsockopt(MRT6_ADD_BW_UPCALL, (%s, %s)) failed: %s",
				 cstring(source), cstring(group),
				 strerror(errno));
	    XLOG_ERROR(("%s", error_msg.c_str());
	    return (XORP_ERROR);
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

/**
 * MfeaMrouter::delete_bw_upcall:
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
 * Delete a dataflow monitor entry from the kernel.
 * Note: either @is_threshold_in_packets or @is_threshold_in_bytes (or both)
 * must be true.
 * Note: either @is_geq_upcall or @is_leq_upcall (but not both) must be true.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::delete_bw_upcall(const IPvX& source, const IPvX& group,
			      const TimeVal& threshold_interval,
			      uint32_t threshold_packets,
			      uint32_t threshold_bytes,
			      bool is_threshold_in_packets,
			      bool is_threshold_in_bytes,
			      bool is_geq_upcall,
			      bool is_leq_upcall,
			      string& error_msg)
{
    XLOG_TRACE(mfea_node().is_log_trace(),
	       "Delete dataflow monitor: "
	       "source = %s group = %s "
	       "threshold_interval_sec = %d threshold_interval_usec = %d "
	       "threshold_packets = %d threshold_bytes = %d "
	       "is_threshold_in_packets = %d is_threshold_in_bytes = %d "
	       "is_geq_upcall = %d is_leq_upcall = %d",
	       cstring(source), cstring(group),
	       threshold_interval.sec(), threshold_interval.usec(),
	       threshold_packets, threshold_bytes,
	       is_threshold_in_packets, is_threshold_in_bytes,
	       is_geq_upcall, is_leq_upcall);

#if ! defined(ENABLE_ADVANCED_MULTICAST_API)
    UNUSED(threshold_interval);
    UNUSED(threshold_packets);
    UNUSED(threshold_bytes);
    UNUSED(is_threshold_in_packets);
    UNUSED(is_threshold_in_bytes);
    UNUSED(is_geq_upcall);
    UNUSED(is_leq_upcall);
#endif

    //
    // Check if the kernel supports the bandwidth-upcall mechanism.
    //
    if (! mrt_api_mrt_mfc_bw_upcall()) {
	error_msg = c_format("add_bw_upcall(%s, %s) failed: "
			     "dataflow monitor entry in the kernel "
			     "is not supported",
			     cstring(source), cstring(group));
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }

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
    // Do the job
    //
    switch (family()) {
    case AF_INET:
    {
#if defined(MRT_ADD_BW_UPCALL) && defined(ENABLE_ADVANCED_MULTICAST_API)
	struct bw_upcall bw_upcall;
	
	//
	// Set the argument
	//
	memset(&bw_upcall, 0, sizeof(bw_upcall));
	source.copy_out(bw_upcall.bu_src);
	group.copy_out(bw_upcall.bu_dst);
	threshold_interval.copy_out(bw_upcall.bu_threshold.b_time);
	bw_upcall.bu_threshold.b_packets = threshold_packets;
	bw_upcall.bu_threshold.b_bytes = threshold_bytes;
	if (is_threshold_in_packets)
	    bw_upcall.bu_flags |= BW_UPCALL_UNIT_PACKETS;
	if (is_threshold_in_bytes)
	    bw_upcall.bu_flags |= BW_UPCALL_UNIT_BYTES;
	do {
	    if (is_geq_upcall) {
		bw_upcall.bu_flags |= BW_UPCALL_GEQ;
		break;
	    }
	    if (is_leq_upcall) {
		bw_upcall.bu_flags |= BW_UPCALL_LEQ;
		break;
	    }
	    XLOG_UNREACHABLE();
	    return (XORP_ERROR);
	} while (false);
	
	if (setsockopt(_mrouter_socket, IPPROTO_IP, MRT_DEL_BW_UPCALL,
		       (void *)&bw_upcall, sizeof(bw_upcall)) < 0) {
	    error_msg = c_format("setsockopt(MRT_DEL_BW_UPCALL, (%s, %s)) failed: %s",
				 cstring(source), cstring(group),
				 strerror(errno));
	    XLOG_ERROR("%s", error_msg.c_str());
	    return (XORP_ERROR);
	}
#endif // MRT_ADD_BW_UPCALL && ENABLE_ADVANCED_MULTICAST_API
    }
    break;
    
#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	error_msg = ("delete_bw_upcall() failed: "
		     "IPv6 multicast routing not supported");
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
#else
	
#if defined(MRT6_ADD_BW_UPCALL) && defined(ENABLE_ADVANCED_MULTICAST_API)
	struct bw6_upcall bw_upcall;
	
	//
	// Set the argument
	//
	memset(&bw_upcall, 0, sizeof(bw_upcall));
	source.copy_out(bw_upcall.bu6_src);
	group.copy_out(bw_upcall.bu6_dst);
	threshold_interval.copy_out(bw_upcall.bu6_threshold.b_time);
	bw_upcall.bu6_threshold.b_packets = threshold_packets;
	bw_upcall.bu6_threshold.b_bytes = threshold_bytes;
	if (is_threshold_in_packets)
	    bw_upcall.bu6_flags |= BW_UPCALL_UNIT_PACKETS;
	if (is_threshold_in_bytes)
	    bw_upcall.bu6_flags |= BW_UPCALL_UNIT_BYTES;
	do {
	    if (is_geq_upcall) {
		bw_upcall.bu6_flags |= BW_UPCALL_GEQ;
		break;
	    }
	    if (is_leq_upcall) {
		bw_upcall.bu6_flags |= BW_UPCALL_LEQ;
		break;
	    }
	    XLOG_UNREACHABLE();
	    return (XORP_ERROR);
	} while (false);
	
	if (setsockopt(_mrouter_socket, IPPROTO_IP, MRT6_DEL_BW_UPCALL,
		       (void *)&bw_upcall, sizeof(bw_upcall)) < 0) {
	    error_msg = c_format("setsockopt(MRT6_DEL_BW_UPCALL, (%s, %s)) failed: %s",
				 cstring(source), cstring(group),
				 strerror(errno));
	    XLOG_ERROR("%s", error_msg.c_str());
	    return (XORP_ERROR);
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

/**
 * MfeaMrouter::delete_all_bw_upcall:
 * @source: The source address.
 * @group: The group address.
 * @error_msg: The error message (if error).
 * 
 * Delete all dataflow monitor entries from the kernel
 * for a given @source and @group address.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::delete_all_bw_upcall(const IPvX& source, const IPvX& group,
				  string& error_msg)
{
    XLOG_TRACE(mfea_node().is_log_trace(),
	       "Delete all dataflow monitors: "
	       "source = %s group = %s",
	       cstring(source), cstring(group));

    //
    // Check if the kernel supports the bandwidth-upcall mechanism.
    //
    if (! mrt_api_mrt_mfc_bw_upcall()) {
	error_msg = c_format("add_bw_upcall(%s, %s) failed: "
			     "dataflow monitor entry in the kernel "
			     "is not supported",
			     cstring(source), cstring(group));
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
    }
    
    //
    // Do the job
    //
    switch (family()) {
    case AF_INET:
    {
#ifndef HAVE_IPV4_MULTICAST_ROUTING
	error_msg = c_format("delete_all_bw_upcall() failed: "
			     "IPv4 multicast routing not supported");
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
#else

#if defined(MRT_ADD_BW_UPCALL) && defined(ENABLE_ADVANCED_MULTICAST_API)
	struct bw_upcall bw_upcall;
	
	//
	// Set the argument
	//
	memset(&bw_upcall, 0, sizeof(bw_upcall));
	source.copy_out(bw_upcall.bu_src);
	group.copy_out(bw_upcall.bu_dst);
	bw_upcall.bu_flags |= BW_UPCALL_DELETE_ALL;
	
	if (setsockopt(_mrouter_socket, IPPROTO_IP, MRT_DEL_BW_UPCALL,
		       (void *)&bw_upcall, sizeof(bw_upcall)) < 0) {
	    error_msg = c_format("setsockopt(MRT_DEL_BW_UPCALL, (%s, %s)) failed: %s",
				 cstring(source), cstring(group),
				 strerror(errno));
	    XLOG_ERROR("%s", error_msg.c_str());
	    return (XORP_ERROR);
	}
#endif // MRT_ADD_BW_UPCALL && ENABLE_ADVANCED_MULTICAST_API

#endif // HAVE_IPV4_MULTICAST_ROUTING
    }
    break;
    
#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	error_msg = c_format("delete_all_bw_upcall() failed: "
			     "IPv6 multicast routing not supported");
	XLOG_ERROR("%s", error_msg.c_str());
	return (XORP_ERROR);
#else
	
#if defined(MRT6_ADD_BW_UPCALL) && defined(ENABLE_ADVANCED_MULTICAST_API)
	struct bw6_upcall bw_upcall;
	
	//
	// Set the argument
	//
	memset(&bw_upcall, 0, sizeof(bw_upcall));
	source.copy_out(bw_upcall.bu6_src);
	group.copy_out(bw_upcall.bu6_dst);
	bw_upcall.bu6_flags |= BW_UPCALL_DELETE_ALL;
	
	if (setsockopt(_mrouter_socket, IPPROTO_IP, MRT6_DEL_BW_UPCALL,
		       (void *)&bw_upcall, sizeof(bw_upcall)) < 0) {
	    error_msg = c_format("setsockopt(MRT6_DEL_BW_UPCALL, (%s, %s)) failed: %s",
				 cstring(source), cstring(group),
				 strerror(errno));
	    XLOG_ERROR("%s", error_msg.c_str());
	    return (XORP_ERROR);
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

/**
 * MfeaMrouter::get_sg_count:
 * @source: The MFC source address.
 * @group: The MFC group address.
 * @sg_count: A reference to a #SgCount class to place the result.
 * 
 * Get various counters per (S,G) entry.
 * Get the number of packets and bytes forwarded by a particular
 * Multicast Forwarding Cache (MFC) entry in the kernel, and the number
 * of packets arrived on wrong interface for that entry.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::get_sg_count(const IPvX& source, const IPvX& group,
			  SgCount& sg_count)
{
#if !defined(HAVE_IPV4_MULTICAST_ROUTING) && !defined(HAVE_IPV6_MULTICAST_ROUTING)
    UNUSED(source);
    UNUSED(group);
    UNUSED(sg_count);
#endif

    switch (family()) {
    case AF_INET:
    {
#ifndef HAVE_IPV4_MULTICAST_ROUTING
	XLOG_ERROR("get_sg_count() failed: "
		   "IPv4 multicast routing not supported");
	return (XORP_ERROR);
#else
	struct sioc_sg_req sgreq;
	
	memset(&sgreq, 0, sizeof(sgreq));
	source.copy_out(sgreq.src);
	group.copy_out(sgreq.grp);
	//
	// XXX: some older mcast code has bug in ip_mroute.c, get_sg_cnt():
	// the return code is always 0, so this is why we need to check
	// if all values are 0xffffffffU (the indication for error).
	// TODO: remove the 0xffffffffU check in the future.
	//
	if ((ioctl(_mrouter_socket, SIOCGETSGCNT, &sgreq) < 0)
	    || ((sgreq.pktcnt == 0xffffffffU)
		&& (sgreq.bytecnt == 0xffffffffU)
		&& (sgreq.wrong_if == 0xffffffffU))) {
	    XLOG_ERROR("ioctl(SIOCGETSGCNT, (%s %s)) failed: %s",
		       cstring(source), cstring(group), strerror(errno));
	    sg_count.set_pktcnt(~0U);
	    sg_count.set_bytecnt(~0U);
	    sg_count.set_wrong_if(~0U);
	    return (XORP_ERROR);
	}
	sg_count.set_pktcnt(sgreq.pktcnt);
	sg_count.set_bytecnt(sgreq.bytecnt);
	sg_count.set_wrong_if(sgreq.wrong_if);
#endif // HAVE_IPV4_MULTICAST_ROUTING
    }
    break;
    
#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	XLOG_ERROR("get_sg_count() failed: "
		   "IPv6 multicast routing not supported");
	return (XORP_ERROR);
#else
	struct sioc_sg_req6 sgreq;

	memset(&sgreq, 0, sizeof(sgreq));
	source.copy_out(sgreq.src);
	group.copy_out(sgreq.grp);
	if (ioctl(_mrouter_socket, SIOCGETSGCNT_IN6, &sgreq) < 0) {
	    XLOG_ERROR("ioctl(SIOCGETSGCNT_IN6, (%s %s)) failed: %s",
		       cstring(source), cstring(group), strerror(errno));
	    sg_count.set_pktcnt(~0U);
	    sg_count.set_bytecnt(~0U);
	    sg_count.set_wrong_if(~0U);
	    return (XORP_ERROR);
	}
	sg_count.set_pktcnt(sgreq.pktcnt);
	sg_count.set_bytecnt(sgreq.bytecnt);
	sg_count.set_wrong_if(sgreq.wrong_if);
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


/**
 * MfeaMrouter::get_vif_count:
 * @vif_index: The vif index of the virtual multicast interface whose
 * statistics we need.
 * @vif_count: A reference to a #VifCount class to store the result.
 * 
 * Get various counters per virtual interface.
 * Get the number of packets and bytes received on, or forwarded on
 * a particular multicast interface.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::get_vif_count(uint32_t vif_index, VifCount& vif_count)
{
    MfeaVif *mfea_vif = mfea_node().vif_find_by_vif_index(vif_index);
    
    if (mfea_vif == NULL)
	return (XORP_ERROR);

#if !defined(HAVE_IPV4_MULTICAST_ROUTING) && !defined(HAVE_IPV6_MULTICAST_ROUTING)
    UNUSED(vif_count);
#endif
    
    switch (family()) {
    case AF_INET:
    {
#ifndef HAVE_IPV4_MULTICAST_ROUTING
	XLOG_ERROR("get_vif_count() failed: "
		   "IPv4 multicast routing not supported");
	return (XORP_ERROR);
#else
	struct sioc_vif_req vreq;

	memset(&vreq, 0, sizeof(vreq));
	vreq.vifi = mfea_vif->vif_index();
	if (ioctl(_mrouter_socket, SIOCGETVIFCNT, &vreq) < 0) {
	    XLOG_ERROR("ioctl(SIOCGETVIFCNT, vif %s) failed: %s",
		       mfea_vif->name().c_str(), strerror(errno));
	    vif_count.set_icount(~0U);
	    vif_count.set_ocount(~0U);
	    vif_count.set_ibytes(~0U);
	    vif_count.set_obytes(~0U);
	    return (XORP_ERROR);
	}
	vif_count.set_icount(vreq.icount);
	vif_count.set_ocount(vreq.ocount);
	vif_count.set_ibytes(vreq.ibytes);
	vif_count.set_obytes(vreq.obytes);
#endif // HAVE_IPV4_MULTICAST_ROUTING
    }
    break;
    
#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	XLOG_ERROR("get_vif_count() failed: "
		   "IPv6 multicast routing not supported");
	return (XORP_ERROR);
#else
	struct sioc_mif_req6 mreq;
	
	memset(&mreq, 0, sizeof(mreq));
	mreq.mifi = mfea_vif->vif_index();
	if (ioctl(_mrouter_socket, SIOCGETMIFCNT_IN6, &mreq) < 0) {
	    XLOG_ERROR("ioctl(SIOCGETMIFCNT_IN6, vif %s) failed: %s",
		       mfea_vif->name().c_str(), strerror(errno));
	    vif_count.set_icount(~0U);
	    vif_count.set_ocount(~0U);
	    vif_count.set_ibytes(~0U);
	    vif_count.set_obytes(~0U);
	    return (XORP_ERROR);
	}
	vif_count.set_icount(mreq.icount);
	vif_count.set_ocount(mreq.ocount);
	vif_count.set_ibytes(mreq.ibytes);
	vif_count.set_obytes(mreq.obytes);
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

/**
 * MfeaMrouter::mrouter_socket_read:
 * @fd: file descriptor of arriving data.
 * @type: The event type that describes the status of @fd.
 * 
 * Read data from the mrouter socket, and then call the appropriate method
 * to process it.
 **/
void
MfeaMrouter::mrouter_socket_read(XorpFd fd, IoEventType type)
{
    ssize_t nbytes;
    
    UNUSED(fd);
    UNUSED(type);
 
#ifndef HOST_OS_WINDOWS
    // Zero and reset various fields
    _rcvmh.msg_controllen = CMSG_BUF_SIZE;

    // TODO: when resetting _from4 and _from6 do we need to set the address
    // family and the sockaddr len?
    switch (family()) {
    case AF_INET:
	memset(&_from4, 0, sizeof(_from4));
	_rcvmh.msg_namelen = sizeof(_from4);
	break;

#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	XLOG_FATAL("mrouter_socket_read() failed: "
		   "IPv6 multicast routing not supported");
	return;
#else
	memset(&_from6, 0, sizeof(_from6));
	_rcvmh.msg_namelen = sizeof(_from6);
#endif // HAVE_IPV6_MULTICAST_ROUTING
    }
    break;
#endif // HAVE_IPV6

    default:
	XLOG_UNREACHABLE();
	return;			// Error
    }

    // Read from the socket
    nbytes = recvmsg(_mrouter_socket, &_rcvmh, 0);
    if (nbytes < 0) {
	if (errno == EINTR)
	    return;		// OK: restart receiving
	XLOG_ERROR("recvmsg() on socket %p failed: %s",
		   _mrouter_socket.str().c_str(), strerror(errno));
	return;			// Error
    }

#else // HOST_OS_WINDOWS
    UNUSED(nbytes);
    XLOG_FATAL("Multicast routing is not supported on Windows");
#endif
    
    // Check if it is a signal from the kernel to the user-level
    switch (family()) {
    case AF_INET:
    {
#ifndef HAVE_IPV4_MULTICAST_ROUTING
	XLOG_FATAL("mrouter_socket_read() failed: "
		   "IPv4 multicast routing not supported");
	return;
#else
	if (nbytes < (ssize_t)sizeof(struct igmpmsg)) {
	    XLOG_WARNING("mrouter_socket_read() failed: "
			 "kernel signal packet size %d is smaller than minimum size %u",
			 XORP_INT_CAST(nbytes),
			 XORP_UINT_CAST(sizeof(struct igmpmsg)));
	    return;		// Error
	}
	struct igmpmsg igmpmsg;
	memcpy(&igmpmsg, _rcvbuf0, sizeof(igmpmsg));
	if (igmpmsg.im_mbz == 0) {
	    //
	    // XXX: Packets sent up from kernel to daemon have
	    //      igmpmsg.im_mbz = ip->ip_p = 0
	    //
	    kernel_call_process(_rcvbuf0, nbytes);
	    return;		// OK
	}
#endif // HAVE_IPV4_MULTICAST_ROUTING
    }
    break;
    
#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	XLOG_FATAL("mrouter_socket_read() failed: "
		   "IPv6 multicast routing not supported");
	return;
#else
	if ((nbytes < (ssize_t)sizeof(struct mrt6msg))
	    && (nbytes < (ssize_t)MLD_MINLEN)) {
	    XLOG_WARNING("mrouter_socket_read() failed: "
			 "kernel signal packet size %d is smaller than minimum size %u",
			 XORP_INT_CAST(nbytes),
			 XORP_UINT_CAST(min(sizeof(struct mrt6msg),
					    (size_t)MLD_MINLEN)));
	    return;		// Error
	}
	struct mrt6msg mrt6msg;
	memcpy(&mrt6msg, _rcvbuf0, sizeof(mrt6msg));
	if ((mrt6msg.im6_mbz == 0) || (_rcvmh.msg_controllen == 0)) {
	    //
	    // XXX: Packets sent up from kernel to daemon have
	    //      mrt6msg.im6_mbz = icmp6_hdr->icmp6_type = 0
	    // Because we set ICMP6 filters on the socket,
	    // we should never see a real ICMPv6 packet
	    // with icmp6_type = 0 .
	    //
	    //
	    // TODO: XXX: (msg_controllen == 0) is presumably
	    // true for older IPv6 systems (e.g. KAME circa
	    // April 2000, FreeBSD-4.0) which don't have the
	    //     'icmp6_type = 0' mechanism.
	    //
	    kernel_call_process(_rcvbuf0, nbytes);
	    return;		// OK
	}
#endif // HAVE_IPV6_MULTICAST_ROUTING
    }
    break;
#endif // HAVE_IPV6
    
    default:
	XLOG_UNREACHABLE();
	break;
    }
    
    //
    // Not a kernel signal. Ignore it.
    //
    
    return;			// OK
}

/**
 * kernel_call_process:
 * @databuf: The data buffer.
 * @datalen: The length of the data in 'databuf'.
 * 
 * Process a call from the kernel (e.g., "nocache", "wrongiif", "wholepkt")
 * XXX: It is OK for im_src/im6_src to be 0 (for 'nocache' or 'wrongiif'),
 *	just in case the kernel supports (*,G) MFC.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
MfeaMrouter::kernel_call_process(uint8_t *databuf, size_t datalen)
{
    uint32_t	iif_vif_index;
    int		message_type;
    IPvX	src(family()), dst(family());

#if !defined(HAVE_IPV4_MULTICAST_ROUTING) && !defined(HAVE_IPV6_MULTICAST_ROUTING)
    UNUSED(iif_vif_index);
    UNUSED(message_type);
    UNUSED(databuf);
    UNUSED(datalen);
#endif
    
    switch (family()) {
    case AF_INET:
    {
#ifndef HAVE_IPV4_MULTICAST_ROUTING
	XLOG_ERROR("kernel_call_process() failed: "
		   "IPv4 multicast routing not supported");
	return (XORP_ERROR);
#else
	struct igmpmsg igmpmsg;
	memcpy(&igmpmsg, databuf, sizeof(igmpmsg));
	
	//
	// Get the message type, the iif, and source and destination address
	//
	message_type = igmpmsg.im_msgtype;
	iif_vif_index = igmpmsg.im_vif;
	if (message_type == IGMPMSG_WHOLEPKT) {
	    //
	    // If a WHOLEPKT message, then get the inner source and
	    // destination addresses
	    //
	    IpHeader4 ip4(databuf + sizeof(struct igmpmsg));
	    if (datalen - sizeof(struct igmpmsg) < ip4.size()) {
		// The inner packet is too small
		return (XORP_ERROR);
	    }
	    src = ip4.ip_src();
	    dst = ip4.ip_dst();
	} else {
	    src.copy_in(igmpmsg.im_src);
	    dst.copy_in(igmpmsg.im_dst);
	}

	//
	// Check if the iif is valid and UP
	//
	switch (message_type) {
	case IGMPMSG_NOCACHE:
	case IGMPMSG_WRONGVIF:
	case IGMPMSG_WHOLEPKT:
	{
	    MfeaVif *mfea_vif = mfea_node().vif_find_by_vif_index(iif_vif_index);
	    if ((mfea_vif == NULL) || (! mfea_vif->is_up())) {
		// Silently ignore the packet
		return (XORP_ERROR);
	    }
	}
	break;
#if defined(IGMPMSG_BW_UPCALL) && defined(ENABLE_ADVANCED_MULTICAST_API)
	case IGMPMSG_BW_UPCALL:
	    break;
#endif
	default:
	    break;
	}
	
	//
	// Check 'src' and 'dst' addresses
	//
	switch (message_type) {
	case IGMPMSG_NOCACHE:
	case IGMPMSG_WRONGVIF:
	case IGMPMSG_WHOLEPKT:
	    if ((! src.is_unicast())
		|| (! dst.is_multicast())
		|| (dst.is_linklocal_multicast())) {
		// XXX: LAN-scoped addresses are not routed
		return (XORP_ERROR);
	    }
	    break;
#if defined(IGMPMSG_BW_UPCALL) && defined(ENABLE_ADVANCED_MULTICAST_API)
	case IGMPMSG_BW_UPCALL:
	    break;
#endif
	default:
	    break;
	}
	//
	// Deliver the signal
	//
	mfea_node().signal_message_recv("",
					module_id(),
					message_type,
					iif_vif_index, src, dst,
					databuf + sizeof(struct igmpmsg),
					datalen - sizeof(struct igmpmsg));
#endif // HAVE_IPV4_MULTICAST_ROUTING
    }
    break;
    
#ifdef HAVE_IPV6
    case AF_INET6:
    {
#ifndef HAVE_IPV6_MULTICAST_ROUTING
	XLOG_ERROR("kernel_call_process() failed: "
		   "IPv6 multicast routing not supported");
	return (XORP_ERROR);
#else
	struct mrt6msg mrt6msg;
	memcpy(&mrt6msg, databuf, sizeof(mrt6msg));
	//
	// Get the message type, the iif, and source and destination address
	//
	message_type = mrt6msg.im6_msgtype;
	iif_vif_index = mrt6msg.im6_mif;
	if (message_type == MRT6MSG_WHOLEPKT) {
	    //
	    // If a WHOLEPKT message, then get the inner source and
	    // destination addresses
	    //
	    IpHeader6 ip6(databuf + sizeof(struct mrt6msg));
	    if (datalen - sizeof(struct mrt6msg) < ip6.size()) {
		// The inner packet is too small
		return (XORP_ERROR);
	    }
	    src = ip6.ip_src();
	    dst = ip6.ip_dst();
	} else {
	    src.copy_in(mrt6msg.im6_src);
	    dst.copy_in(mrt6msg.im6_dst);
	}
	
	//
	// Check if the iif is valid and UP
	//
	switch (message_type) {
	case MRT6MSG_NOCACHE:
	case MRT6MSG_WRONGMIF:
	case MRT6MSG_WHOLEPKT:
	{
	    // Check if the iif is valid and UP
	    MfeaVif *mfea_vif = mfea_node().vif_find_by_vif_index(iif_vif_index);
	    if ((mfea_vif == NULL) || (! mfea_vif->is_up())) {
		// Silently ignore the packet
		return (XORP_ERROR);
	    }
	}
	break;
#if defined(MRT6MSG_BW_UPCALL) && defined(ENABLE_ADVANCED_MULTICAST_API)
	case MRT6MSG_BW_UPCALL:
	    break;
#endif
	default:
	    break;
	}
	
	//
	// Check 'src' and 'dst' addresses
	//
	switch (message_type) {
	case MRT6MSG_NOCACHE:
	case MRT6MSG_WRONGMIF:
	case MRT6MSG_WHOLEPKT:
	    if ((! src.is_unicast())
		|| (! dst.is_multicast())
		|| dst.is_linklocal_multicast()) {
		// XXX: LAN-scoped addresses are not routed
		return (XORP_ERROR);
	    }
	    break;
#if defined(MRT6MSG_BW_UPCALL) && defined(ENABLE_ADVANCED_MULTICAST_API)
	case MRT6MSG_BW_UPCALL:
	    break;
#endif
	default:
	    break;
	}
	//
	// Deliver the signal
	//
	mfea_node().signal_message_recv("",
					module_id(),
					message_type,
					iif_vif_index, src, dst,
					databuf + sizeof(struct mrt6msg),
					datalen - sizeof(struct mrt6msg));
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
