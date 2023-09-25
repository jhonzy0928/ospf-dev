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

#ident "$XORP: xorp/libxorp/eventloop.cc,v 1.22 2007/03/08 00:03:55 atanu Exp $"

#include "libxorp_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"
#include "libxorp/debug.h"

#include "eventloop.hh"


//
// Number of EventLoop instances.
// XXX: Warning: static instance. Not suitable for shared library use.
//
static int instance_count = 0;

//
// Last call to EventLoop::run.  0 is a special value that indicates
// EventLoop::run has not been called.
//
// XXX: Warning: static instance. Not suitable for shared library use.
//
static time_t last_ev_run;

EventLoop::EventLoop()
    : _clock(new SystemClock), _timer_list(_clock),
#ifdef HOST_OS_WINDOWS
      _win_dispatcher(_clock)
#else
      _selector_list(_clock)
#endif
{
    instance_count++;
    XLOG_ASSERT(instance_count == 1);
    last_ev_run = 0;
}

EventLoop::~EventLoop()
{
    instance_count--;
    XLOG_ASSERT(instance_count == 0);
    delete _clock;
}

void
EventLoop::run()
{
    const time_t MAX_ALLOWED = 2;
    static time_t last_warned;

    if (last_ev_run == 0)
	last_ev_run = time(0);

    time_t now = time(0);
    time_t diff = now - last_ev_run;

    if (now - last_warned > 0 && (diff > MAX_ALLOWED)) {
	XLOG_WARNING("%d seconds between calls to EventLoop::run", (int)diff);
	last_warned = now;
    }

    TimeVal t;

    _timer_list.advance_time();
    _timer_list.get_next_delay(t);

    int timer_priority = XorpTask::PRIORITY_INFINITY;
    int selector_priority = XorpTask::PRIORITY_INFINITY;
    int task_priority = XorpTask::PRIORITY_INFINITY;

    if (t == TimeVal::ZERO()) {
	timer_priority = _timer_list.get_expired_priority();
    }

#ifdef HOST_OS_WINDOWS
    if (_win_dispatcher.ready())
	selector_priority = _win_dispatcher.get_ready_priority();
#else
    if (_selector_list.ready()) {
	selector_priority = _selector_list.get_ready_priority();
    }
#endif

    if (!_task_list.empty()) {
	task_priority = _task_list.get_runnable_priority();
    }

    debug_msg("Priorities: timer = %d selector = %d task = %d\n",
	      timer_priority, selector_priority, task_priority);

    if ( (timer_priority != XorpTask::PRIORITY_INFINITY)
	 && (timer_priority <= selector_priority)
	 && (timer_priority <= task_priority)) {

	// the most important thing to run next is a timer
	_timer_list.run();

    } else if ( (selector_priority != XorpTask::PRIORITY_INFINITY)
		&& (selector_priority <= task_priority) ) {

	// the most important thing to run next is a selector
#ifdef HOST_OS_WINDOWS
	_win_dispatcher.wait_and_dispatch(&t);
#else
	_selector_list.wait_and_dispatch(&t);
#endif

    } else if (task_priority != XorpTask::PRIORITY_INFINITY) {

	// the most important thing to run next is a task
	_task_list.run();

    } else {
	// there's nothing immediate to run, so go to sleep until the
	// next selector or timer goes off
#ifdef HOST_OS_WINDOWS
	_win_dispatcher.wait_and_dispatch(&t);
#else
	_selector_list.wait_and_dispatch(&t);
#endif
    }

    last_ev_run = time(0);
}

void
EventLoop::run_pending_tasks()
{
    while (!_task_list.empty()) {
	_task_list.run();
    }
}

bool
EventLoop::add_ioevent_cb(XorpFd fd, IoEventType type, const IoEventCb& cb,
			  int priority)
{
#ifdef HOST_OS_WINDOWS
    return _win_dispatcher.add_ioevent_cb(fd, type, cb, priority);
#else
    return _selector_list.add_ioevent_cb(fd, type, cb, priority);
#endif
}

bool
EventLoop::remove_ioevent_cb(XorpFd fd, IoEventType type)
{
#ifdef HOST_OS_WINDOWS
    return _win_dispatcher.remove_ioevent_cb(fd, type);
#else
    _selector_list.remove_ioevent_cb(fd, type);
    return true;
#endif
}

size_t
EventLoop::descriptor_count() const
{
#ifdef HOST_OS_WINDOWS
    return _win_dispatcher.descriptor_count();
#else
    return _selector_list.descriptor_count();
#endif
}

XorpTask
EventLoop::new_oneoff_task(const OneoffTaskCallback& cb, int priority,
			   int weight)
{
    return _task_list.new_oneoff_task(cb, priority, weight);
}

XorpTask
EventLoop::new_task(const RepeatedTaskCallback& cb, int priority, int weight)
{
    return _task_list.new_task(cb, priority, weight);
}
