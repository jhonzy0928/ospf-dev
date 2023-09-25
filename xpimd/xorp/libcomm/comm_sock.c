/* -*-  Mode:C; c-basic-offset:4; tab-width:8; indent-tabs-mode:t -*- */
/* vim:set sts=4 ts=8: */
/*
 * Copyright (c) 2001
 * YOID Project.
 * University of Southern California/Information Sciences Institute.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ident "$XORP: xorp/libcomm/comm_sock.c,v 1.34 2006/10/12 01:24:45 pavlin Exp $"

/*
 * COMM socket library lower `sock' level implementation.
 */

#include "comm_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"
#include "libxorp/debug.h"

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif
#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif
#ifdef HAVE_WS2TCPIP_H
#include <ws2tcpip.h>
#endif

#include "comm_api.h"
#include "comm_private.h"


/* XXX: Single threaded socket errno, used to record last error code. */
int _comm_serrno;

#if defined(HOST_OS_WINDOWS) && defined(HAVE_IPV6)
/*
 * Windows declares these in <ws2tcpip.h> as externs, but does not
 * supply symbols for them in the -lws2_32 import library or DLL.
 */
const struct in6_addr in6addr_any = { { IN6ADDR_ANY_INIT } };
const struct in6_addr in6addr_loopback = { { IN6ADDR_LOOPBACK_INIT } };
#endif

/**
 * comm_sock_open:
 * @domain: The domain of the socket (e.g., %AF_INET, %AF_INET6).
 * @type: The type of the socket (e.g., %SOCK_STREAM, %SOCK_DGRAM).
 * @protocol: The particular protocol to be used with the socket.
 * @is_blocking: If true, then the socket will be blocking, otherwise
 * non-blocking.
 *
 * Open a socket of domain = @domain, type = @type, and protocol = @protocol.
 * The sending and receiving buffer size are set, and the socket
 * itself is set with %TCP_NODELAY (if a TCP socket).
 *
 * Return value: The open socket on success, otherwise %XORP_BAD_SOCKET.
 **/
xsock_t
comm_sock_open(int domain, int type, int protocol, int is_blocking)
{
    xsock_t sock;

    /* Create the kernel socket */
    sock = socket(domain, type, protocol);
    if (sock == XORP_BAD_SOCKET) {
	_comm_set_serrno();
	XLOG_ERROR("Error opening socket (domain = %d, type = %d, "
		   "protocol = %d): %s",
		   domain, type, protocol,
		   comm_get_error_str(comm_get_last_error()));
	return (XORP_BAD_SOCKET);
    }

    /* Set the receiving and sending socket buffer size in the kernel */
    if (comm_sock_set_rcvbuf(sock, SO_RCV_BUF_SIZE_MAX, SO_RCV_BUF_SIZE_MIN)
	< SO_RCV_BUF_SIZE_MIN) {
	_comm_set_serrno();
	comm_sock_close(sock);
	return (XORP_BAD_SOCKET);
    }
    if (comm_sock_set_sndbuf(sock, SO_SND_BUF_SIZE_MAX, SO_SND_BUF_SIZE_MIN)
	< SO_SND_BUF_SIZE_MIN) {
	_comm_set_serrno();
	comm_sock_close(sock);
	return (XORP_BAD_SOCKET);
    }

    /* Enable TCP_NODELAY */
    if (type == SOCK_STREAM && comm_set_nodelay(sock, 1) != XORP_OK) {
	_comm_set_serrno();
	comm_sock_close(sock);
	return (XORP_BAD_SOCKET);
    }

    /* Set blocking mode */
    if (comm_sock_set_blocking(sock, is_blocking) != XORP_OK) {
	_comm_set_serrno();
	comm_sock_close(sock);
	return (XORP_BAD_SOCKET);
    }

    return (sock);
}

/**
 * comm_sock_pair:
 *
 * Create a pair of connected sockets. The sockets will be created in
 * the blocking state by default, and with no additional socket options set.
 *
 * On Windows platforms, a domain of AF_UNIX, AF_INET, or AF_INET6 must
 * be specified. For the AF_UNIX case, the sockets created will actually
 * be in the AF_INET domain. The protocol field is ignored.
 *
 * On UNIX, this function simply wraps the socketpair() system call.
 *
 * XXX: There may be UNIX platforms lacking socketpair() where we
 * have to emulate it.
 *
 * @param domain the domain of the socket (e.g., AF_INET, AF_INET6).
 * @param type the type of the socket (e.g., SOCK_STREAM, SOCK_DGRAM).
 * @param protocol the particular protocol to be used with the socket.
 * @param sv pointer to an array of two xsock_t handles to receive the
 *        allocated socket pair.
 *
 * @return XORP_OK if the socket pair was created, otherwise if any error
 * is encountered, XORP_ERROR.
 **/
int
comm_sock_pair(int domain, int type, int protocol, xsock_t sv[2])
{
#ifndef HOST_OS_WINDOWS
    if (socketpair(domain, type, protocol, sv) == -1) {
	_comm_set_serrno();
	return (XORP_ERROR);
    }
    return (XORP_OK);

#else /* HOST_OS_WINDOWS */
    struct sockaddr_storage ss;
    struct sockaddr_in	*psin;
    socklen_t		sslen;
    SOCKET		st[3];
    u_long		optval;
    int			numtries, error, intdomain;
    unsigned short	port;
    static const int	CSP_LOWPORT = 40000;
    static const int	CSP_HIGHPORT = 65536;
#ifdef HAVE_IPV6
    struct sockaddr_in6 *psin6;
#endif

    UNUSED(protocol);

    if (domain != AF_UNIX && domain != AF_INET
#ifdef HAVE_IPV6
	&& domain != AF_INET6
#endif
	) {
	_comm_serrno = WSAEAFNOSUPPORT;
	return (XORP_ERROR);
    }

    intdomain = domain;
    if (intdomain == AF_UNIX)
	intdomain = AF_INET;

    st[0] = st[1] = st[2] = INVALID_SOCKET;

    st[2] = socket(intdomain, type, 0);
    if (st[2] == INVALID_SOCKET)
	goto error;

    memset(&ss, 0, sizeof(ss));
    psin = (struct sockaddr_in *)&ss;
#ifdef HAVE_IPV6
    psin6 = (struct sockaddr_in6 *)&ss;
    if (intdomain == AF_INET6) {
	sslen = sizeof(struct sockaddr_in6);
	ss.ss_family = AF_INET6;
	psin6->sin6_addr = in6addr_loopback;
    } else
#endif /* HAVE_IPV6 */
    {
	sslen = sizeof(struct sockaddr_in);
	ss.ss_family = AF_INET;
	psin->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }

    numtries = 3;
    do {
	port = htons((rand() % (CSP_LOWPORT - CSP_HIGHPORT)) + CSP_LOWPORT);
#ifdef HAVE_IPV6
	if (intdomain == AF_INET6)
	    psin6->sin6_port = port;
	else
#endif
	    psin->sin_port = port;
	error = bind(st[2], (struct sockaddr *)&ss, sslen);
	if (error == 0)
	    break;
	if ((error != 0) &&
	    ((WSAGetLastError() != WSAEADDRNOTAVAIL) ||
	     (WSAGetLastError() != WSAEADDRINUSE)))
	    break;
    } while (--numtries > 0);

    if (error != 0)
	goto error;

    error = listen(st[2], 5);
    if (error != 0)
	goto error;

    st[0] = socket(intdomain, type, 0);
    if (st[0] == INVALID_SOCKET)
	goto error;

    optval = 1L;
    error = ioctlsocket(st[0], FIONBIO, &optval);
    if (error != 0)
	goto error;

    error = connect(st[0], (struct sockaddr *)&ss, sslen);
    if (error != 0 && WSAGetLastError() != WSAEWOULDBLOCK)
	goto error;

    numtries = 3;
    do {
	st[1] = accept(st[2], NULL, NULL);
	if (st[1] != INVALID_SOCKET) {
	    break;
	} else {
	    if (WSAGetLastError() == WSAEWOULDBLOCK) {
		SleepEx(100, TRUE);
	    } else {
		break;
	    }
	}
    } while (--numtries > 0);

    if (st[1] == INVALID_SOCKET)
	goto error;

    /* Squelch inherited socket event mask. */
    (void)WSAEventSelect(st[1], NULL, 0);

    /*
     * XXX: Should use getsockname() here to verify that the client socket
     * is connected.
     */
    optval = 0L;
    error = ioctlsocket(st[0], FIONBIO, &optval);
    if (error != 0)
	goto error;

    closesocket(st[2]);
    sv[0] = st[0];
    sv[1] = st[1];
    return (XORP_OK);

error:
    if (st[0] != INVALID_SOCKET)
	closesocket(st[0]);
    if (st[1] != INVALID_SOCKET)
	closesocket(st[1]);
    if (st[2] != INVALID_SOCKET)
	closesocket(st[2]);
    return (XORP_ERROR);
#endif /* HOST_OS_WINDOWS */
}

/**
 * comm_sock_bind4:
 * @sock: The socket to bind.
 * @my_addr: The address to bind to (in network order).
 * If it is NULL, will bind to `any' local address.
 * @my_port: The port to bind to (in network order).
 *
 * Bind an IPv4 socket to an address and a port.
 *
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
comm_sock_bind4(xsock_t sock, const struct in_addr *my_addr,
		unsigned short my_port)
{
    int family;
    struct sockaddr_in sin_addr;

    family = comm_sock_get_family(sock);
    if (family != AF_INET) {
	XLOG_ERROR("Invalid family of socket %d: family = %d (expected %d)",
		   sock, family, AF_INET);
	return (XORP_ERROR);
    }

    memset(&sin_addr, 0, sizeof(sin_addr));
#ifdef HAVE_SIN_LEN
    sin_addr.sin_len = sizeof(sin_addr);
#endif
    sin_addr.sin_family = (u_char)family;
    sin_addr.sin_port = my_port;		/* XXX: in network order */
    if (my_addr != NULL)
	sin_addr.sin_addr.s_addr = my_addr->s_addr; /* XXX: in network order */
    else
	sin_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *)&sin_addr, sizeof(sin_addr)) < 0) {
	_comm_set_serrno();
	XLOG_ERROR("Error binding socket (family = %d, "
		   "my_addr = %s, my_port = %d): %s",
		   family,
		   (my_addr)? inet_ntoa(*my_addr) : "ANY",
		   ntohs(my_port),
		   comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return (XORP_OK);
}

/**
 * comm_sock_bind6:
 * @sock: The socket to bind.
 * @my_addr: The address to bind to (in network order).
 * If it is NULL, will bind to `any' local address.
 * @my_port: The port to bind to (in network order).
 *
 * Bind an IPv6 socket to an address and a port.
 *
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
comm_sock_bind6(xsock_t sock, const struct in6_addr *my_addr,
		unsigned short my_port)
{
#ifdef HAVE_IPV6
    int family;
    struct sockaddr_in6 sin6_addr;

    family = comm_sock_get_family(sock);
    if (family != AF_INET6) {
	XLOG_ERROR("Invalid family of socket %d: family = %d (expected %d)",
		   sock, family, AF_INET6);
	return (XORP_ERROR);
    }

    memset(&sin6_addr, 0, sizeof(sin6_addr));
#ifdef HAVE_SIN6_LEN
    sin6_addr.sin6_len = sizeof(sin6_addr);
#endif
    sin6_addr.sin6_family = (u_char)family;
    sin6_addr.sin6_port = my_port;		/* XXX: in network order     */
    sin6_addr.sin6_flowinfo = 0;		/* XXX: unused (?)	     */
    if (my_addr != NULL)
	memcpy(&sin6_addr.sin6_addr, my_addr, sizeof(sin6_addr.sin6_addr));
    else
	memcpy(&sin6_addr.sin6_addr, &in6addr_any, sizeof(sin6_addr.sin6_addr));

    sin6_addr.sin6_scope_id = 0;		/* XXX: unused (?)	     */

    if (bind(sock, (struct sockaddr *)&sin6_addr, sizeof(sin6_addr)) < 0) {
	char addr_str[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255"];
	_comm_set_serrno();
	XLOG_ERROR("Error binding socket (family = %d, "
		   "my_addr = %s, my_port = %d): %s",
		   family,
		   (my_addr)?
		   inet_ntop(family, my_addr, addr_str, sizeof(addr_str))
		   : "ANY",
		   ntohs(my_port), comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return (XORP_OK);
#else /* ! HAVE_IPV6 */
    comm_sock_no_ipv6("comm_sock_bind6", sock, my_addr, my_port);
    return (XORP_ERROR);
#endif /* ! HAVE_IPV6 */
}

/**
 * Bind a socket (IPv4 or IPv6) to an address and a port.
 *
 * @param sock the socket to bind.
 * @param sin agnostic sockaddr containing the local address (If it is
 * NULL, will bind to `any' local address.)  and the local port to
 * bind to all in network order.
 * @return XORP_OK on success, otherwise XORP_ERROR.
 */
int
comm_sock_bind(xsock_t sock, const struct sockaddr *sin)
{
    switch (sin->sa_family) {
    case AF_INET:
	{
	    const struct sockaddr_in *sin4 = (const struct sockaddr_in *)((const void *)sin);
	    return comm_sock_bind4(sock, &sin4->sin_addr, sin4->sin_port);
	}
	break;
#ifdef HAVE_IPV6
    case AF_INET6:
	{
	    const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)((const void *)sin);
	    return comm_sock_bind6(sock, &sin6->sin6_addr, sin6->sin6_port);
	}
	break;
#endif /* HAVE_IPV6 */
    default:
	XLOG_FATAL("Error comm_sock_bind invalid family = %d", sin->sa_family);
	return (XORP_ERROR);
    }

    XLOG_UNREACHABLE();

    return XORP_ERROR;
}

/**
 * comm_sock_join4:
 * @sock: The socket to join the group.
 * @mcast_addr: The multicast address to join.
 * @my_addr: The local unicast address of an interface to join.
 * If it is NULL, the interface is chosen by the kernel.
 *
 * Join an IPv4 multicast group on a socket (and an interface).
 *
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
comm_sock_join4(xsock_t sock, const struct in_addr *mcast_addr,
		const struct in_addr *my_addr)
{
    int family;
    struct ip_mreq imr;		/* the multicast join address */

    family = comm_sock_get_family(sock);
    if (family != AF_INET) {
	XLOG_ERROR("Invalid family of socket %d: family = %d (expected %d)",
		   sock, family, AF_INET);
	return (XORP_ERROR);
    }

    memset(&imr, 0, sizeof(imr));
    imr.imr_multiaddr.s_addr = mcast_addr->s_addr;
    if (my_addr != NULL)
	imr.imr_interface.s_addr = my_addr->s_addr;
    else
	imr.imr_interface.s_addr = INADDR_ANY;
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		   XORP_SOCKOPT_CAST(&imr), sizeof(imr)) < 0) {
	char mcast_addr_str[32], my_addr_str[32];
	_comm_set_serrno();
	strncpy(mcast_addr_str, inet_ntoa(*mcast_addr),
		sizeof(mcast_addr_str) - 1);
	mcast_addr_str[sizeof(mcast_addr_str) - 1] = '\0';
	if (my_addr != NULL)
	    strncpy(my_addr_str, inet_ntoa(*my_addr),
		    sizeof(my_addr_str) - 1);
	else
	    strncpy(my_addr_str, "ANY", sizeof(my_addr_str) - 1);
	my_addr_str[sizeof(my_addr_str) - 1] = '\0';
	XLOG_ERROR("Error joining mcast group (family = %d, "
		   "mcast_addr = %s my_addr = %s): %s",
		   family, mcast_addr_str, my_addr_str,
		   comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return (XORP_OK);
}

/**
 * comm_sock_join6:
 * @sock: The socket to join the group.
 * @mcast_addr: The multicast address to join.
 * @my_ifindex: The local unicast interface index to join.
 * If it is 0, the interface is chosen by the kernel.
 *
 * Join an IPv6 multicast group on a socket (and an interface).
 *
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
comm_sock_join6(xsock_t sock, const struct in6_addr *mcast_addr,
		unsigned int my_ifindex)
{
#ifdef HAVE_IPV6
    int family;
    struct ipv6_mreq imr6;	/* the multicast join address */

    family = comm_sock_get_family(sock);
    if (family != AF_INET6) {
	XLOG_ERROR("Invalid family of socket %d: family = %d (expected %d)",
		   sock, family, AF_INET6);
	return (XORP_ERROR);
    }

    memset(&imr6, 0, sizeof(imr6));
    memcpy(&imr6.ipv6mr_multiaddr, mcast_addr, sizeof(*mcast_addr));
    imr6.ipv6mr_interface = my_ifindex;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
		   XORP_SOCKOPT_CAST(&imr6), sizeof(imr6)) < 0) {
	char addr_str[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255"];
	_comm_set_serrno();
	XLOG_ERROR("Error joining mcast group (family = %d, "
		   "mcast_addr = %s my_ifindex = %d): %s",
		   family,
		   inet_ntop(family, mcast_addr, addr_str, sizeof(addr_str)),
		   my_ifindex, comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return (XORP_OK);

#else /* ! HAVE_IPV6 */
    comm_sock_no_ipv6("comm_sock_join6", sock, mcast_addr, my_ifindex);
    return (XORP_ERROR);
#endif /* ! HAVE_IPV6 */
}

/**
 * comm_sock_leave4:
 * @sock: The socket to leave the group.
 * @mcast_addr: The multicast address to leave.
 * @my_addr: The local unicast address of an interface to leave.
 * If it is NULL, the interface is chosen by the kernel.
 *
 * Leave an IPv4 multicast group on a socket (and an interface).
 *
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
comm_sock_leave4(xsock_t sock, const struct in_addr *mcast_addr,
		const struct in_addr *my_addr)
{
    int family;
    struct ip_mreq imr;		/* the multicast leave address */

    family = comm_sock_get_family(sock);
    if (family != AF_INET) {
	XLOG_ERROR("Invalid family of socket %d: family = %d (expected %d)",
		   sock, family, AF_INET);
	return (XORP_ERROR);
    }

    memset(&imr, 0, sizeof(imr));
    imr.imr_multiaddr.s_addr = mcast_addr->s_addr;
    if (my_addr != NULL)
	imr.imr_interface.s_addr = my_addr->s_addr;
    else
	imr.imr_interface.s_addr = INADDR_ANY;
    if (setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP,
		   XORP_SOCKOPT_CAST(&imr), sizeof(imr)) < 0) {
	char mcast_addr_str[32], my_addr_str[32];
	_comm_set_serrno();
	strncpy(mcast_addr_str, inet_ntoa(*mcast_addr),
		sizeof(mcast_addr_str) - 1);
	mcast_addr_str[sizeof(mcast_addr_str) - 1] = '\0';
	if (my_addr != NULL)
	    strncpy(my_addr_str, inet_ntoa(*my_addr),
		    sizeof(my_addr_str) - 1);
	else
	    strncpy(my_addr_str, "ANY", sizeof(my_addr_str) - 1);
	my_addr_str[sizeof(my_addr_str) - 1] = '\0';
	XLOG_ERROR("Error leaving mcast group (family = %d, "
		   "mcast_addr = %s my_addr = %s): %s",
		   family, mcast_addr_str, my_addr_str,
		   comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return (XORP_OK);
}

/**
 * comm_sock_leave6:
 * @sock: The socket to leave the group.
 * @mcast_addr: The multicast address to leave.
 * @my_ifindex: The local unicast interface index to leave.
 * If it is 0, the interface is chosen by the kernel.
 *
 * Leave an IPv6 multicast group on a socket (and an interface).
 *
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
comm_sock_leave6(xsock_t sock, const struct in6_addr *mcast_addr,
		unsigned int my_ifindex)
{
#ifdef HAVE_IPV6
    int family;
    struct ipv6_mreq imr6;	/* the multicast leave address */

    family = comm_sock_get_family(sock);
    if (family != AF_INET6) {
	XLOG_ERROR("Invalid family of socket %d: family = %d (expected %d)",
		   sock, family, AF_INET6);
	return (XORP_ERROR);
    }

    memset(&imr6, 0, sizeof(imr6));
    memcpy(&imr6.ipv6mr_multiaddr, mcast_addr, sizeof(*mcast_addr));
    imr6.ipv6mr_interface = my_ifindex;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_LEAVE_GROUP,
		   XORP_SOCKOPT_CAST(&imr6), sizeof(imr6)) < 0) {
	char addr_str[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255"];
	_comm_set_serrno();
	XLOG_ERROR("Error leaving mcast group (family = %d, "
		   "mcast_addr = %s my_ifindex = %d): %s",
		   family,
		   inet_ntop(family, mcast_addr, addr_str, sizeof(addr_str)),
		   my_ifindex, comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return (XORP_OK);

#else /* ! HAVE_IPV6 */
    comm_sock_no_ipv6("comm_sock_leave6", sock, mcast_addr, my_ifindex);
    return (XORP_ERROR);
#endif /* ! HAVE_IPV6 */
}

/**
 * Connect to a remote IPv4 address.
 *
 * Note that we can use this function not only for TCP, but for UDP sockets
 * as well.
 *
 * @param sock the socket to use to connect.
 * @param remote_addr the remote address to connect to.
 * @param remote_port the remote port to connect to.
 * @param is_blocking if true, the socket is blocking, otherwise non-blocking.
 * @param in_progress if the socket is non-blocking and the connect cannot be
 * completed immediately, then the referenced value is set to 1, and the
 * return value is XORP_ERROR. For all other errors or if the non-blocking
 * socket was connected, the referenced value is set to 0. If the return value
 * is XORP_OK or if the socket is blocking, then the return value is undefined.
 * @return XORP_OK on success, otherwise XORP_ERROR.
 */
int
comm_sock_connect4(xsock_t sock, const struct in_addr *remote_addr,
		   unsigned short remote_port, int is_blocking,
		   int *in_progress)
{
    int family;
    struct sockaddr_in sin_addr;

    if (in_progress != NULL)
	*in_progress = 0;

    family = comm_sock_get_family(sock);
    if (family != AF_INET) {
	XLOG_ERROR("Invalid family of socket %d: family = %d (expected %d)",
		   sock, family, AF_INET);
	return (XORP_ERROR);
    }

    memset(&sin_addr, 0, sizeof(sin_addr));
#ifdef HAVE_SIN_LEN
    sin_addr.sin_len = sizeof(sin_addr);
#endif
    sin_addr.sin_family = (u_char)family;
    sin_addr.sin_port = remote_port;		/* XXX: in network order */
    sin_addr.sin_addr.s_addr = remote_addr->s_addr; /* XXX: in network order */

    if (connect(sock, (struct sockaddr *)&sin_addr, sizeof(sin_addr)) < 0) {
	_comm_set_serrno();
	if (! is_blocking) {
#ifdef HOST_OS_WINDOWS
	    if (comm_get_last_error() == WSAEWOULDBLOCK) {
#else
	    if (comm_get_last_error() == EINPROGRESS) {
#endif
		/*
		 * XXX: The connection is non-blocking, and the connection
		 * cannot be completed immediately, therefore set the
		 * in_progress flag to 1 and return an error.
		 */
		if (in_progress != NULL)
		    *in_progress = 1;
		return (XORP_ERROR);
	    }
	}

	XLOG_ERROR("Error connecting socket (family = %d, "
		   "remote_addr = %s, remote_port = %d): %s",
		   family, inet_ntoa(*remote_addr), ntohs(remote_port),
		   comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return (XORP_OK);
}

/**
 * Connect to a remote IPv6 address.
 *
 * Note that we can use this function not only for TCP, but for UDP sockets
 * as well.
 *
 * @param sock the socket to use to connect.
 * @param remote_addr the remote address to connect to.
 * @param remote_port the remote port to connect to.
 * @param is_blocking if true, the socket is blocking, otherwise non-blocking.
 * @param in_progress if the socket is non-blocking and the connect cannot be
 * completed immediately, then the referenced value is set to 1, and the
 * return value is XORP_ERROR. For all other errors or if the non-blocking
 * socket was connected, the referenced value is set to 0. If the return value
 * is XORP_OK or if the socket is blocking, then the return value is undefined.
 * @return XORP_OK on success, otherwise XORP_ERROR.
 */
int
comm_sock_connect6(xsock_t sock, const struct in6_addr *remote_addr,
		   unsigned short remote_port, int is_blocking,
		   int *in_progress)
{
#ifdef HAVE_IPV6
    int family;
    struct sockaddr_in6 sin6_addr;

    if (in_progress != NULL)
	*in_progress = 0;

    family = comm_sock_get_family(sock);
    if (family != AF_INET6) {
	XLOG_ERROR("Invalid family of socket %d: family = %d (expected %d)",
		   sock, family, AF_INET6);
	return (XORP_ERROR);
    }

    memset(&sin6_addr, 0, sizeof(sin6_addr));
#ifdef HAVE_SIN6_LEN
    sin6_addr.sin6_len = sizeof(sin6_addr);
#endif
    sin6_addr.sin6_family = (u_char)family;
    sin6_addr.sin6_port = remote_port;		/* XXX: in network order     */
    sin6_addr.sin6_flowinfo = 0;		/* XXX: unused (?)	     */
    memcpy(&sin6_addr.sin6_addr, remote_addr, sizeof(sin6_addr.sin6_addr));
    sin6_addr.sin6_scope_id = 0;		/* XXX: unused (?)	     */

    if (connect(sock, (struct sockaddr *)&sin6_addr, sizeof(sin6_addr)) < 0) {
	char addr_str[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255"];
	_comm_set_serrno();
	if (! is_blocking) {
#ifdef HOST_OS_WINDOWS
	    if (comm_get_last_error() == WSAEWOULDBLOCK) {
#else
	    if (comm_get_last_error() == EINPROGRESS) {
#endif
		/*
		 * XXX: The connection is non-blocking, and the connection
		 * cannot be completed immediately, therefore set the
		 * in_progress flag to 1 and return an error.
		 */
		if (in_progress != NULL)
		    *in_progress = 1;
		return (XORP_ERROR);
	    }
	}

	XLOG_ERROR("Error connecting socket (family = %d, "
		   "remote_addr = %s, remote_port = %d): %s",
		   family,
		   (remote_addr)?
		   inet_ntop(family, remote_addr, addr_str, sizeof(addr_str))
		   : "ANY",
		   ntohs(remote_port),
		   comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return (XORP_OK);

#else /* ! HAVE_IPV6 */
    if (in_progress != NULL)
	*in_progress = 0;

    comm_sock_no_ipv6("comm_sock_connect6", sock, remote_addr, remote_port,
		      is_blocking);
    return (XORP_ERROR);
#endif /* ! HAVE_IPV6 */
}

/**
 * Connect to a remote address (IPv4 or IPv6).
 *
 * Note that we can use this function not only for TCP, but for UDP sockets
 * as well.
 *
 * @param sock the socket to use to connect.
 * @param sin agnostic sockaddr containing the local address (If it is
 * NULL, will bind to `any' local address.)  and the local port to
 * bind to all in network order.
 * @param is_blocking if true, the socket is blocking, otherwise non-blocking.
 * @param in_progress if the socket is non-blocking and the connect cannot be
 * completed immediately, then the referenced value is set to 1, and the
 * return value is XORP_ERROR. For all other errors or if the non-blocking
 * socket was connected, the referenced value is set to 0. If the return value
 * is XORP_OK or if the socket is blocking, then the return value is undefined.
 * @return XORP_OK on success, otherwise XORP_ERROR.
 */
int
comm_sock_connect(xsock_t sock, const struct sockaddr *sin, int is_blocking,
		  int *in_progress)
{
    switch (sin->sa_family) {
    case AF_INET:
	{
	    const struct sockaddr_in *sin4 = (const struct sockaddr_in *)((const void *)sin);
	    return comm_sock_connect4(sock, &sin4->sin_addr, sin4->sin_port,
				      is_blocking, in_progress);
	}
	break;
#ifdef HAVE_IPV6
    case AF_INET6:
	{
	    const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)((const void *)sin);
	    return comm_sock_connect6(sock, &sin6->sin6_addr, sin6->sin6_port,
				      is_blocking, in_progress);
	}
	break;
#endif /* HAVE_IPV6 */
    default:
	XLOG_FATAL("Error comm_sock_connect invalid family = %d",
		   sin->sa_family);
	return (XORP_ERROR);
    }

    XLOG_UNREACHABLE();

    return XORP_ERROR;
}

/**
 * comm_sock_accept:
 * @sock: The listening socket to accept on.
 *
 * Accept a connection on a listening socket.
 *
 * Return value: The accepted socket on success, otherwise %XORP_BAD_SOCKET.
 **/
xsock_t
comm_sock_accept(xsock_t sock)
{
    xsock_t sock_accept;
    struct sockaddr_in addr;
    socklen_t socklen = sizeof(addr);

    sock_accept = accept(sock, (struct sockaddr *)&addr, &socklen);
    if (sock_accept == XORP_BAD_SOCKET) {
	_comm_set_serrno();
	XLOG_ERROR("Error accepting socket %d: %s",
		   sock, comm_get_error_str(comm_get_last_error()));
	return (XORP_BAD_SOCKET);
    }

#ifdef HOST_OS_WINDOWS
    /*
     * Squelch Winsock event notifications on the new socket which may
     * have been inherited from the parent listening socket.
     */
    (void)WSAEventSelect(sock_accept, NULL, 0);
#endif

    /* Enable TCP_NODELAY */
    if (comm_set_nodelay(sock_accept, 1) != XORP_OK) {
	comm_sock_close(sock_accept);
	return (XORP_BAD_SOCKET);
    }

    return (sock_accept);
}

/**
 * comm_sock_close:
 * @sock: The socket to close.
 *
 * Close a socket.
 *
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
comm_sock_close(xsock_t sock)
{
    int ret;

#ifndef HOST_OS_WINDOWS
    ret = close(sock);
#else
    (void)WSAEventSelect(sock, NULL, 0);
    ret = closesocket(sock);
#endif

    if (ret < 0) {
	_comm_set_serrno();
	XLOG_ERROR("Error closing socket (socket = %d) : %s",
		   sock, comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return (XORP_OK);
}

/**
 * comm_set_nodelay:
 * @sock: The socket whose option we want to set/reset.
 * @val: If non-zero, the option will be set, otherwise will be reset.
 *
 * Set/reset the %TCP_NODELAY option on a TCP socket.
 *
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
comm_set_nodelay(xsock_t sock, int val)
{
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
		   XORP_SOCKOPT_CAST(&val), sizeof(val)) < 0) {
	_comm_set_serrno();
	XLOG_ERROR("Error %s TCP_NODELAY on socket %d: %s",
		   (val)? "set": "reset",  sock,
		   comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return (XORP_OK);
}

/**
 * comm_set_reuseaddr:
 * @sock: The socket whose option we want to set/reset.
 * @val: If non-zero, the option will be set, otherwise will be reset.
 *
 * Set/reset the %SO_REUSEADDR option on a socket.
 * XXX: if the OS doesn't support this option, %XORP_ERROR is returned.
 *
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
comm_set_reuseaddr(xsock_t sock, int val)
{
#ifdef SO_REUSEADDR
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
		   XORP_SOCKOPT_CAST(&val), sizeof(val)) < 0) {
	_comm_set_serrno();
	XLOG_ERROR("Error %s SO_REUSEADDR on socket %d: %s",
		   (val)? "set": "reset", sock,
		   comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return (XORP_OK);

#else /* ! SO_REUSEADDR */
    UNUSED(sock);
    UNUSED(val);
    XLOG_WARNING("SO_REUSEADDR Undefined!");

    return (XORP_ERROR);
#endif /* ! SO_REUSEADDR */
}

/**
 * comm_set_reuseport:
 * @sock: The socket whose option we want to set/reset.
 * @val: If non-zero, the option will be set, otherwise will be reset.
 *
 * Set/reset the %SO_REUSEPORT option on a socket.
 * XXX: if the OS doesn't support this option, %XORP_OK is returned.
 *
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
comm_set_reuseport(xsock_t sock, int val)
{
#ifdef SO_REUSEPORT
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT,
	XORP_SOCKOPT_CAST(&val), sizeof(val)) < 0) {
	_comm_set_serrno();
	XLOG_ERROR("Error %s SO_REUSEPORT on socket %d: %s",
		   (val)? "set": "reset", sock,
		   comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return (XORP_OK);

#else /* ! SO_REUSEPORT */
    UNUSED(sock);
    UNUSED(val);
    XLOG_WARNING("SO_REUSEPORT Undefined!");

    return (XORP_OK);
#endif /* ! SO_REUSEPORT */
}

/**
 * comm_set_loopback:
 * @sock: The socket whose option we want to set/reset.
 * @val: If non-zero, the option will be set, otherwise will be reset.
 *
 * Set/reset the multicast loopback option on a socket.
 *
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
comm_set_loopback(xsock_t sock, int val)
{
    int family = comm_sock_get_family(sock);

    switch (family) {
    case AF_INET:
    {
	u_char loop = val;

	if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP,
		       XORP_SOCKOPT_CAST(&loop), sizeof(loop)) < 0) {
	    _comm_set_serrno();
	    XLOG_ERROR("setsockopt IP_MULTICAST_LOOP %u: %s",
		       loop,
		       comm_get_error_str(comm_get_last_error()));
	    return (XORP_ERROR);
	}
	break;
    }
#ifdef HAVE_IPV6
    case AF_INET6:
    {
	u_int loop6 = val;

	if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
		       XORP_SOCKOPT_CAST(&loop6), sizeof(loop6)) < 0) {
	    _comm_set_serrno();
	    XLOG_ERROR("setsockopt IPV6_MULTICAST_LOOP %u: %s",
		       loop6, comm_get_error_str(comm_get_last_error()));
	    return (XORP_ERROR);
	}
	break;
    }
#endif /* HAVE_IPV6 */
    default:
	XLOG_FATAL("Error %s setsockopt IP_MULTICAST_LOOP/IPV6_MULTICAST_LOOP "
		   "on socket %d: invalid family = %d",
		   (val)? "set": "reset", sock, family);
	return (XORP_ERROR);
    }

    return (XORP_OK);
}

/**
 * comm_set_tcpmd5:
 * @sock: The socket whose option we want to set/reset.
 * @val: If non-zero, the option will be set, otherwise will be reset.
 *
 * Set/reset the %TCP_MD5SIG option on a TCP socket.
 * XXX: if the OS doesn't support this option, %XORP_ERROR is returned.
 *
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
comm_set_tcpmd5(xsock_t sock, int val)
{
#ifdef TCP_MD5SIG /* XXX: Defined in <netinet/tcp.h> across Free/Net/OpenBSD */
    if (setsockopt(sock, IPPROTO_TCP, TCP_MD5SIG,
		   XORP_SOCKOPT_CAST(&val), sizeof(val)) < 0) {
	_comm_set_serrno();
	XLOG_ERROR("Error %s TCP_MD5SIG on socket %d: %s",
		   (val)? "set": "reset",  sock,
		   comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return (XORP_OK);

#else /* ! TCP_MD5SIG */
    UNUSED(sock);
    UNUSED(val);
    XLOG_WARNING("TCP_MD5SIG Undefined!");

    return (XORP_ERROR);
#endif /* ! TCP_MD5SIG */
}

/**
 * comm_set_ttl:
 * @sock: The socket whose TTL we want to set.
 * @val: The TTL of the outgoing multicast packets.
 *
 * Set the TTL of the outgoing multicast packets on a socket.
 *
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
comm_set_ttl(xsock_t sock, int val)
{
    int family = comm_sock_get_family(sock);

    switch (family) {
    case AF_INET:
    {
	u_char ip_ttl = val;	/* XXX: In IPv4 the value arg is 'u_char' */

	if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL,
		       XORP_SOCKOPT_CAST(&ip_ttl), sizeof(ip_ttl)) < 0) {
	    _comm_set_serrno();
	    XLOG_ERROR("setsockopt IP_MULTICAST_TTL %u: %s",
		       ip_ttl, comm_get_error_str(comm_get_last_error()));
	    return (XORP_ERROR);
	}
	break;
    }
#ifdef HAVE_IPV6
    case AF_INET6:
    {
	int ip_ttl = val;

	if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
		       XORP_SOCKOPT_CAST(&ip_ttl), sizeof(ip_ttl)) < 0) {
	    _comm_set_serrno();
	    XLOG_ERROR("setsockopt IPV6_MULTICAST_HOPS %u: %s",
		       ip_ttl, comm_get_error_str(comm_get_last_error()));
	    return (XORP_ERROR);
	}
	break;
    }
#endif /* HAVE_IPV6 */
    default:
	XLOG_FATAL("Error %s setsockopt IP_MULTICAST_TTL/IPV6_MULTICAST_HOPS "
		   "on socket %d: invalid family = %d",
		   (val)? "set": "reset", sock, family);
	return (XORP_ERROR);
    }

    return (XORP_OK);
}

/**
 * comm_set_iface4:
 * @sock: The socket whose default multicast interface to set.
 * @in_addr: The IPv4 address of the default interface to set.
 * If @in_addr is NULL, the system will choose the interface each time
 * a datagram is sent.
 *
 * Set default interface for IPv4 outgoing multicast on a socket.
 *
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
comm_set_iface4(xsock_t sock, const struct in_addr *in_addr)
{
    int family = comm_sock_get_family(sock);
    struct in_addr my_addr;

    if (family != AF_INET) {
	XLOG_ERROR("Invalid family of socket %d: family = %d (expected %d)",
		   sock, family, AF_INET);
	return (XORP_ERROR);
    }

    if (in_addr != NULL)
	my_addr.s_addr = in_addr->s_addr;
    else
	my_addr.s_addr = INADDR_ANY;
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF,
		   XORP_SOCKOPT_CAST(&my_addr), sizeof(my_addr)) < 0) {
	_comm_set_serrno();
	XLOG_ERROR("setsockopt IP_MULTICAST_IF %s: %s",
		   (in_addr)? inet_ntoa(my_addr) : "ANY",
		   comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return (XORP_OK);
}

/**
 * comm_set_iface6:
 * @sock: The socket whose default multicast interface to set.
 * @ifindex: The IPv6 interface index of the default interface to set.
 * If @ifindex is 0, the system will choose the interface each time
 * a datagram is sent.
 *
 * Set default interface for IPv6 outgoing multicast on a socket.
 *
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
comm_set_iface6(xsock_t sock, u_int ifindex)
{
#ifdef HAVE_IPV6
    int family = comm_sock_get_family(sock);

    if (family != AF_INET6) {
	XLOG_ERROR("Invalid family of socket %d: family = %d (expected %d)",
		   sock, family, AF_INET6);
	return (XORP_ERROR);
    }

    if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_IF,
		   XORP_SOCKOPT_CAST(&ifindex), sizeof(ifindex)) < 0) {
	_comm_set_serrno();
	XLOG_ERROR("setsockopt IPV6_MULTICAST_IF for ifindex %d: %s",
		   ifindex, comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return (XORP_OK);

#else /* ! HAVE_IPV6 */
    comm_sock_no_ipv6("comm_set_iface6", sock, ifindex);
    return (XORP_ERROR);
#endif /* ! HAVE_IPV6 */
}

/**
 * comm_sock_set_sndbuf:
 * @sock: The socket whose sending buffer size to set.
 * @desired_bufsize: The preferred buffer size.
 * @min_bufsize: The smallest acceptable buffer size.
 *
 * Set the sending buffer size of a socket.
 *
 * Return value: The successfully set buffer size on success,
 * otherwise %XORP_ERROR.
 **/
int
comm_sock_set_sndbuf(xsock_t sock, int desired_bufsize, int min_bufsize)
{
    int delta = desired_bufsize / 2;

    /*
     * Set the socket buffer size.  If we can't set it as large as we
     * want, search around to try to find the highest acceptable
     * value.  The highest acceptable value being smaller than
     * minsize is a fatal error.
     */
    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
		   XORP_SOCKOPT_CAST(&desired_bufsize),
		   sizeof(desired_bufsize)) < 0) {
	desired_bufsize -= delta;
	while (1) {
	    if (delta > 1)
		delta /= 2;

	    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
			   XORP_SOCKOPT_CAST(&desired_bufsize),
			   sizeof(desired_bufsize)) < 0) {
		_comm_set_serrno();
		desired_bufsize -= delta;
		if (desired_bufsize <= 0)
		    break;
	    } else {
		if (delta < 1024)
		    break;
		desired_bufsize += delta;
	    }
	}
	if (desired_bufsize < min_bufsize) {
	    XLOG_ERROR("Cannot set sending buffer size of socket %d: "
		       "desired buffer size %u < minimum allowed %u",
		       sock, desired_bufsize, min_bufsize);
	    return (XORP_ERROR);
	}
    }

    return (desired_bufsize);
}

/**
 * comm_sock_set_rcvbuf:
 * @sock: The socket whose receiving buffer size to set.
 * @desired_bufsize: The preferred buffer size.
 * @min_bufsize: The smallest acceptable buffer size.
 *
 * Set the receiving buffer size of a socket.
 *
 * Return value: The successfully set buffer size on success,
 * otherwise %XORP_ERROR.
 **/
int
comm_sock_set_rcvbuf(xsock_t sock, int desired_bufsize, int min_bufsize)
{
    int delta = desired_bufsize / 2;

    /*
     * Set the socket buffer size.  If we can't set it as large as we
     * want, search around to try to find the highest acceptable
     * value.  The highest acceptable value being smaller than
     * minsize is a fatal error.
     */
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
		   XORP_SOCKOPT_CAST(&desired_bufsize),
		   sizeof(desired_bufsize)) < 0) {
	desired_bufsize -= delta;
	while (1) {
	    if (delta > 1)
		delta /= 2;

	    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
			   XORP_SOCKOPT_CAST(&desired_bufsize),
			   sizeof(desired_bufsize)) < 0) {
		_comm_set_serrno();
		desired_bufsize -= delta;
		if (desired_bufsize <= 0)
		    break;
	    } else {
		if (delta < 1024)
		    break;
		desired_bufsize += delta;
	    }
	}
	if (desired_bufsize < min_bufsize) {
	    XLOG_ERROR("Cannot set receiving buffer size of socket %d: "
		       "desired buffer size %u < minimum allowed %u",
		       sock, desired_bufsize, min_bufsize);
	    return (XORP_ERROR);
	}
    }

    return (desired_bufsize);
}

/**
 * comm_sock_get_family:
 * @sock: The socket whose address family we need to get.
 *
 * Get the address family of a socket.
 * XXX: idea taken from W. Stevens' UNPv1, 2e (pp 109)
 *
 * Return value: The address family on success, otherwise %XORP_ERROR.
 **/
int
comm_sock_get_family(xsock_t sock)
{
#ifdef HOST_OS_WINDOWS
    WSAPROTOCOL_INFO wspinfo;
    int err, len;

    len = sizeof(wspinfo);
    err = getsockopt(sock, SOL_SOCKET, SO_PROTOCOL_INFO,
			   XORP_SOCKOPT_CAST(&wspinfo), &len);
    if (err != 0)  {
	_comm_set_serrno();
	XLOG_ERROR("Error getsockopt(SO_PROTOCOL_INFO) for socket %d: %s",
		   sock, comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return ((int)wspinfo.iAddressFamily);

#else /* ! HOST_OS_WINDOWS */
    /* XXX: Should use struct sockaddr_storage. */
#ifndef MAXSOCKADDR
#define MAXSOCKADDR	128	/* max socket address structure size */
#endif
    union {
	struct sockaddr	sa;
	char		data[MAXSOCKADDR];
    } un;
    socklen_t len;

    len = MAXSOCKADDR;
    if (getsockname(sock, &un.sa, &len) < 0) {
	_comm_set_serrno();
	XLOG_ERROR("Error getsockname() for socket %d: %s",
		   sock, comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    return (un.sa.sa_family);
#endif /* ! HOST_OS_WINDOWS */
}

/**
 * comm_sock_set_blocking:
 *
 * Set the blocking or non-blocking mode of an existing socket.
 * @sock: The socket whose blocking mode is to be set.
 * @is_blocking: If non-zero, then the socket will be blocking, otherwise
 * non-blocking.
 *
 * Return value: XORP_OK if the operation was successful, otherwise
 *               if any error is encountered, XORP_ERROR.
 **/
int
comm_sock_set_blocking(xsock_t sock, int is_blocking)
{
#ifdef HOST_OS_WINDOWS
    u_long opt;
    int flags;

    if (is_blocking)
	opt = 0;
    else
	opt = 1;

    flags = ioctlsocket(sock, FIONBIO, &opt);
    if (flags != 0) {
	_comm_set_serrno();
	XLOG_ERROR("FIONBIO error: %s",
		   comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

#else /* ! HOST_OS_WINDOWS */
    int flags;
    if ( (flags = fcntl(sock, F_GETFL, 0)) < 0) {
	_comm_set_serrno();
	XLOG_ERROR("F_GETFL error: %s",
		   comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }

    if (is_blocking)
	flags &= ~O_NONBLOCK;
    else
	flags |= O_NONBLOCK;

    if (fcntl(sock, F_SETFL, flags) < 0) {
	_comm_set_serrno();
	XLOG_ERROR("F_SETFL error: %s",
		   comm_get_error_str(comm_get_last_error()));
	return (XORP_ERROR);
    }
#endif /* ! HOST_OS_WINDOWS */

    return (XORP_OK);
}

/**
 * comm_sock_is_connected:
 *
 * Determine if an existing socket is in the connected state.
 *
 * Return value: XORP_OK if the socket is in the connected state, otherwise
 * if it is not, or any other error is encountered, XORP_ERROR.
 **/
int
comm_sock_is_connected(xsock_t sock)
{
    struct sockaddr_storage ss;
    int err;
    socklen_t sslen;

    sslen = sizeof(ss);
    memset(&ss, 0, sslen);
    err = getpeername(sock, (struct sockaddr *)&ss, &sslen);
    if (err != 0) {
	_comm_set_serrno();
	return (XORP_ERROR);
    }

    return (XORP_OK);
}

/**
 * comm_sock_no_ipv6:
 *
 * Log an error when an IPv6 specific method is called when IPv6 is
 * not preset. Set an appropriate error code relevant to the platform
 * we're running under.
 *
 * XXX This is currently done with knowledge of how the error code is
 * internally stored, which is a design bug (we're not thread friendly).
 *
 * @param method C-style string denoting the ipv6 function called.
 **/
void
comm_sock_no_ipv6(const char* method, ...)
{
#ifdef HOST_OS_WINDOWS
    _comm_serrno = WSAEAFNOSUPPORT;
#else
    _comm_serrno = EAFNOSUPPORT;
#endif
    XLOG_ERROR("%s: IPv6 support not present.", method);
}

/**
 * _comm_set_serrno:
 *
 * Fetch the socket layer error code from the underlying system, clear
 * the general error condition, and record it.
 *
 * XXX: Currently not thread-safe. Internal use only.
 **/
void
_comm_set_serrno(void)
{
#ifdef HOST_OS_WINDOWS
    _comm_serrno = WSAGetLastError();
    WSASetLastError(0);
#else
    _comm_serrno = errno;
    /*
     * TODO: XXX - Temporarily don't set errno to 0 we still have code
     * using errno 2005-05-09 Atanu.
     */
    /* errno = 0; */
#endif
}
