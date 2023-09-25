// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-
// vim:set sts=4 ts=8:

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

#ident "$XORP: xorp/libxorp/selector.cc,v 1.39 2007/02/16 22:46:22 pavlin Exp $"

#include "libxorp_module.h"

#include "libxorp/xorp.h"

#ifndef HOST_OS_WINDOWS // Entire file is stubbed out on Windows.

#include "libxorp/debug.h"
#include "libxorp/xlog.h"
#include "libxorp/xorpfd.hh"
#include "libxorp/timeval.hh"
#include "libxorp/clock.hh"
#include "libxorp/eventloop.hh"
#include "libxorp/utility.h"

#include "selector.hh"


// ----------------------------------------------------------------------------
// Helper function to deal with translating between old and new
// I/O event notification mechanisms.

static SelectorMask
map_ioevent_to_selectormask(const IoEventType type)
{
    SelectorMask mask = SEL_NONE;

    // Convert new event type to legacy UNIX event mask used by SelectorList.
    switch (type) {
    case IOT_READ:
	mask = SEL_RD;
	break;
    case IOT_WRITE:
	mask = SEL_WR;
	break;
    case IOT_EXCEPTION:
	mask = SEL_EX;
	break;
    case IOT_ACCEPT:
	mask = SEL_RD;
	break;
    case IOT_CONNECT:
	mask = SEL_WR;
	break;
    case IOT_DISCONNECT:
	mask = SEL_EX;	// XXX: Disconnection isn't a distinct event in UNIX
	break;
    case IOT_ANY:
	mask = SEL_ALL;
	break;
    }
    return (mask);
}

// ----------------------------------------------------------------------------
// SelectorList::Node methods

inline
SelectorList::Node::Node()
{
    _mask[SEL_RD_IDX] = _mask[SEL_WR_IDX] = _mask[SEL_EX_IDX] = 0;
}

inline bool
SelectorList::Node::add_okay(SelectorMask m, IoEventType type,
			     const IoEventCb& cb, int priority)
{
    int i;

    // always OK to try to register for nothing
    if (!m)
	return true;

    // Sanity Checks

    // 0. We understand all bits in 'mode'
    assert((m & (SEL_RD | SEL_WR | SEL_EX)) == m);

    // 1. Check that bits in 'mode' are not already registered
    for (i = 0; i < SEL_MAX_IDX; i++) {
	if (_mask[i] & m) {
	    return false;
	}
    }

    // 2. If not already registered, find empty slot and add entry.
    // XXX: TODO: Determine if the node we're about to add is for
    // an accept event, so we know how to map it back.
    for (i = 0; i < SEL_MAX_IDX; i++) {
	if (!_mask[i]) {
	    _mask[i]	= m;
	    _cb[i]	= IoEventCb(cb);
	    _iot[i]	= type;
	    _priority[i] = priority;
	    return true;
	}
    }

    assert(0);
    return false;
}

inline int
SelectorList::Node::run_hooks(SelectorMask m, XorpFd fd)
{
    int n = 0;

    /*
     * This is nasty.  We dispatch the callbacks here associated with
     * the file descriptor fd.  Unfortunately these callbacks can
     * manipulate the mask and callbacks associated with the
     * descriptor, ie the data change beneath our feet.  At no time do
     * we want to call a callback that has been removed so we can't
     * just copy the data before starting the dispatch process.  We do
     * not want to perform another callback here on a masked bit that
     * we have already done a callback on.  We therefore keep track of
     * the bits already matched with the variable already_matched.
     *
     * An alternate fix is to change the semantics of add_ioevent_cb so
     * there is one callback for each I/O event.
     *
     * Yet another alternative is to have an object, let's call it a
     * Selector that is a handle state of a file descriptor: ie an fd, a
     * mask, a callback and an enabled flag.  We would process the Selector
     * state individually.
     */
    SelectorMask already_matched = SelectorMask(0);

    for (int i = 0; i < SEL_MAX_IDX; i++) {
	SelectorMask match = SelectorMask(_mask[i] & m & ~already_matched);
	if (match) {
	    assert(_cb[i].is_empty() == false);
	    _cb[i]->dispatch(fd, _iot[i]);
	    n++;
	}
	already_matched = SelectorMask(already_matched | match);
    }
    return n;
}

inline void
SelectorList::Node::clear(SelectorMask zap)
{
    for (size_t i = 0; i < SEL_MAX_IDX; i++) {
	_mask[i] &= ~zap;
	if (_mask[i] == 0) {
	    _cb[i].release();
	    _priority[i] = XorpTask::PRIORITY_INFINITY;
	}
    }
}

inline bool
SelectorList::Node::is_empty()
{
    return ((_mask[SEL_RD_IDX] == 0) && (_mask[SEL_WR_IDX] == 0) &&
	    (_mask[SEL_EX_IDX] == 0));
}

// ----------------------------------------------------------------------------
// SelectorList implementation

SelectorList::SelectorList(ClockBase *clock)
    : _clock(clock), _observer(NULL), _maxfd(0), _descriptor_count(0)
{
    static_assert(SEL_RD == (1 << SEL_RD_IDX) && SEL_WR == (1 << SEL_WR_IDX)
		  && SEL_EX == (1 << SEL_EX_IDX) && SEL_MAX_IDX == 3);
    for (int i = 0; i < SEL_MAX_IDX; i++)
	FD_ZERO(&_fds[i]);
}

SelectorList::~SelectorList()
{
}

bool
SelectorList::add_ioevent_cb(XorpFd		fd,
			   IoEventType		type,
			   const IoEventCb&	cb,
			   int			priority)
{
    SelectorMask mask = map_ioevent_to_selectormask(type);

    if (mask == 0) {
	XLOG_FATAL("SelectorList::add_ioevent_cb: attempt to add invalid event "
		   "type (type = %d)\n", type);
    }

    if (!fd.is_valid()) {
	XLOG_FATAL("SelectorList::add_ioevent_cb: attempt to add invalid file "
		   "descriptor (fd = %s)\n", fd.str().c_str());
    }

    bool resize = false;
    if (fd >= _maxfd) {
	_maxfd = fd;
	size_t entries_n = _selector_entries.size();
	if ((size_t)fd >= entries_n) {
	    _selector_entries.resize(fd + 32);
	    for (size_t j = entries_n; j < _selector_entries.size(); j++) {
		for (int i = 0; i < SEL_MAX_IDX; i++) {
		    _selector_entries[j]._priority[i] = XorpTask::PRIORITY_INFINITY;
		}
	    }
	    resize = true;
	}
    }
    bool no_selectors_with_fd = _selector_entries[fd].is_empty();
    if (_selector_entries[fd].add_okay(mask, type, cb, priority) == false) {
	return false;
    }
    if (no_selectors_with_fd)
	_descriptor_count++;

    for (int i = 0; i < SEL_MAX_IDX; i++) {
	if (mask & (1 << i)) {
	    FD_SET(fd, &_fds[i]);
	    if (_observer) _observer->notify_added(fd, mask);
	}
    }

    return true;
}

void
SelectorList::remove_ioevent_cb(XorpFd fd, IoEventType type)
{
    bool found = false;

    if (fd < 0 || fd >= (int)_selector_entries.size()) {
	XLOG_ERROR("Attempting to remove fd = %d that is outside range of "
		   "file descriptors 0..%u", (int)fd,
		   XORP_UINT_CAST(_selector_entries.size()));
	return;
    }

    SelectorMask mask = map_ioevent_to_selectormask(type);

    for (int i = 0; i < SEL_MAX_IDX; i++) {
	if (mask & (1 << i) && FD_ISSET(fd, &_fds[i])) {
	    found = true;
	    FD_CLR(fd, &_fds[i]);
	    if (_observer)
		_observer->notify_removed(fd, ((SelectorMask) (1 << i)));
	}
    }
    if (! found) {
	// XXX: no event that needs to be removed has been found
	return;
    }

    _selector_entries[fd].clear(mask);
    if (_selector_entries[fd].is_empty()) {
	assert(FD_ISSET(fd, &_fds[SEL_RD_IDX]) == 0);
	assert(FD_ISSET(fd, &_fds[SEL_WR_IDX]) == 0);
	assert(FD_ISSET(fd, &_fds[SEL_EX_IDX]) == 0);
	_descriptor_count--;
    }
}

bool
SelectorList::ready()
{
    fd_set testfds[SEL_MAX_IDX];
    int n = 0;

    memcpy(testfds, _fds, sizeof(_fds));
    struct timeval tv_zero;
    tv_zero.tv_sec = 0;
    tv_zero.tv_usec = 0;

    n = ::select(_maxfd + 1,
		 &testfds[SEL_RD_IDX],
		 &testfds[SEL_WR_IDX],
		 &testfds[SEL_EX_IDX],
		 &tv_zero);

    if (n < 0) {
	switch (errno) {
	case EBADF:
	    callback_bad_descriptors();
	    break;
	case EINVAL:
	    XLOG_FATAL("Bad select argument");
	    break;
	case EINTR:
	    // The system call was interrupted by a signal, hence return
	    // immediately to the event loop without printing an error.
	    debug_msg("SelectorList::ready() interrupted by a signal\n");
	    break;
	default:
	    XLOG_ERROR("SelectorList::ready() failed: %s", strerror(errno));
	    break;
	}
	return false;
    }
    if (n == 0)
	return false;
    else
	return true;
}

int
SelectorList::get_ready_priority()
{
    fd_set testfds[SEL_MAX_IDX];
    int n = 0;

    memcpy(testfds, _fds, sizeof(_fds));
    struct timeval tv_zero;
    tv_zero.tv_sec = 0;
    tv_zero.tv_usec = 0;

    n = ::select(_maxfd + 1,
		 &testfds[SEL_RD_IDX],
		 &testfds[SEL_WR_IDX],
		 &testfds[SEL_EX_IDX],
		 &tv_zero);

    if (n < 0) {
	switch (errno) {
	case EBADF:
	    callback_bad_descriptors();
	    break;
	case EINVAL:
	    XLOG_FATAL("Bad select argument");
	    break;
	case EINTR:
	    // The system call was interrupted by a signal, hence return
	    // immediately to the event loop without printing an error.
	    debug_msg("SelectorList::ready() interrupted by a signal\n");
	    break;
	default:
	    XLOG_ERROR("SelectorList::ready() failed: %s", strerror(errno));
	    break;
	}
	return XorpTask::PRIORITY_INFINITY;
    }
    if (n == 0)
	return XorpTask::PRIORITY_INFINITY;

    int max_priority = XorpTask::PRIORITY_INFINITY;

    for (int fd = 0; fd <= _maxfd; fd++) {
	for (int sel_idx = 0; sel_idx < SEL_MAX_IDX; sel_idx++) {
	    if (FD_ISSET(fd, &testfds[sel_idx])) {
		int p = _selector_entries[fd]._priority[sel_idx];
		if (p < max_priority)
		    max_priority = p;
	    }
	}
    }
    return max_priority;
}

int
SelectorList::wait_and_dispatch(TimeVal* timeout)
{
    fd_set testfds[SEL_MAX_IDX];
    int n = 0;

    memcpy(testfds, _fds, sizeof(_fds));

    if (timeout == 0 || *timeout == TimeVal::MAXIMUM()) {
	n = ::select(_maxfd + 1,
		     &testfds[SEL_RD_IDX],
		     &testfds[SEL_WR_IDX],
		     &testfds[SEL_EX_IDX],
		     0);
    } else {
	struct timeval tv_to;
	timeout->copy_out(tv_to);
	n = ::select(_maxfd + 1,
		     &testfds[SEL_RD_IDX],
		     &testfds[SEL_WR_IDX],
		     &testfds[SEL_EX_IDX],
		     &tv_to);
    }

    _clock->advance_time();

    if (n < 0) {
	switch (errno) {
	case EBADF:
	    callback_bad_descriptors();
	    break;
	case EINVAL:
	    XLOG_FATAL("Bad select argument (probably timeval)");
	    break;
	case EINTR:
	    // The system call was interrupted by a signal, hence return
	    // immediately to the event loop without printing an error.
	    debug_msg("SelectorList::wait_and_dispatch() interrupted by a signal\n");
	    break;
	default:
	    XLOG_ERROR("SelectorList::wait_and_dispatch() failed: %s", strerror(errno));
	    break;
	}
	return 0;
    }

    for (int fd = 0; fd <= _maxfd; fd++) {
	int mask = 0;
	if (FD_ISSET(fd, &testfds[SEL_RD_IDX])) {
	    mask |= SEL_RD;
	    FD_CLR(fd, &testfds[SEL_RD_IDX]);	// paranoia
	}
	if (FD_ISSET(fd, &testfds[SEL_WR_IDX])) {
	    mask |= SEL_WR;
	    FD_CLR(fd, &testfds[SEL_WR_IDX]);	// paranoia
	}
	if (FD_ISSET(fd, &testfds[SEL_EX_IDX])) {
	    mask |= SEL_EX;
	    FD_CLR(fd, &testfds[SEL_EX_IDX]);	// paranoia
	}
	if (mask) {
	    _selector_entries[fd].run_hooks(SelectorMask(mask), fd);
	}
    }

    for (int i = 0; i <= _maxfd; i++) {
	assert(!FD_ISSET(i, &testfds[SEL_RD_IDX]));	// paranoia
	assert(!FD_ISSET(i, &testfds[SEL_WR_IDX]));	// paranoia
	assert(!FD_ISSET(i, &testfds[SEL_EX_IDX]));	// paranoia
    }

    return n;
}

int
SelectorList::wait_and_dispatch(int millisecs)
{
    TimeVal t(millisecs / 1000, (millisecs % 1000) * 1000);
    return wait_and_dispatch(&t);
}

void
SelectorList::get_fd_set(SelectorMask selected_mask, fd_set& fds) const
{
    if ((SEL_RD != selected_mask) && (SEL_WR != selected_mask) &&
	(SEL_EX != selected_mask)) return;
    if (SEL_RD == selected_mask) fds = _fds [SEL_RD_IDX];
    if (SEL_WR == selected_mask) fds = _fds [SEL_WR_IDX];
    if (SEL_EX == selected_mask) fds = _fds [SEL_EX_IDX];
    return;
}

int
SelectorList::get_max_fd() const
{
    return _maxfd;
}

//
// Note that this method should be called only if there are bad file
// descriptors.
//
void
SelectorList::callback_bad_descriptors()
{
    int bc = 0;	/* bad descriptor count */

    for (int fd = 0; fd <= _maxfd; fd++) {
	if (_selector_entries[fd].is_empty() == true)
	    continue;
	/*
	 * Check whether fd is valid.
	 */
	struct stat sb;
	if ((fstat(fd, &sb) < 0) && (errno == EBADF)) {
	    //
	    // Force callbacks, should force read/writes that fail and
	    // client should remove descriptor from list.
	    //
	    XLOG_ERROR("SelectorList found file descriptor %d no longer "
		       "valid.", fd);
	    _selector_entries[fd].run_hooks(SEL_ALL, fd);
	    bc++;
	}
    }
    //
    // Assert should only fail if we called this method when there were
    // no bad file descriptors, or if fstat() didn't return the appropriate
    // error.
    //
    XLOG_ASSERT(bc != 0);
}

void
SelectorList::set_observer(SelectorListObserverBase& obs)
{
    _observer = &obs;
    _observer->_observed = this;
    return;
}

void
SelectorList::remove_observer()
{
    if (_observer) _observer->_observed = NULL;
    _observer = NULL;
    return;
}

SelectorListObserverBase::~SelectorListObserverBase()
{
    if (_observed) _observed->remove_observer();
}

#endif // !HOST_OS_WINDOWS
