// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright (c) 2006-2007 International Computer Science Institute
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
//

#ident "$XORP: xorp/libxorp/task.cc,v 1.7 2007/02/16 22:46:23 pavlin Exp $"

#include "libxorp_module.h"
#include "xorp.h"

#include "xlog.h"
#include "task.hh"


// ----------------------------------------------------------------------------
// TaskNode methods

TaskNode::TaskNode(TaskList* task_list, BasicTaskCallback cb)
    : _task_list(task_list), _cb(cb), _ref_cnt(0), _priority(0), _weight(0)
{
    debug_msg("TaskNode constructor %p\n", this);
}

TaskNode::~TaskNode()
{
    debug_msg("TaskNode destructor %p\n", this);

    unschedule();
}

void
TaskNode::add_ref()
{
    _ref_cnt++;
    debug_msg("add_ref on %p, now ref_cnt = %d\n", this, _ref_cnt);
}

void
TaskNode::release_ref()
{
    if (--_ref_cnt <= 0)
	delete this;
}

void
TaskNode::schedule(int priority, int weight)
{
    debug_msg("TaskNode schedule %p p = %d, w = %d\n", this, priority, weight);

    XLOG_ASSERT(_task_list != NULL);
    unschedule();
    _priority = priority;
    _weight = weight;
    _task_list->schedule_node(this);
}

void
TaskNode::reschedule()
{
    XLOG_ASSERT(_task_list != NULL);
    unschedule();
    _task_list->schedule_node(this);
}

void
TaskNode::unschedule()
{
    if (scheduled())
	_task_list->unschedule_node(this);
}


// ----------------------------------------------------------------------------
// Specialized Tasks.  These are what the XorpTask objects returned by
// the TaskList XorpTask creation methods (e.g. TaskList::new_oneoff_at(), etc)
// actually refer to.

class OneoffTaskNode2 : public TaskNode {
public:
    OneoffTaskNode2(TaskList* task_list, const OneoffTaskCallback& cb)
	: TaskNode(task_list, callback(this, &OneoffTaskNode2::run)),
	  _cb(cb) {}

private:
    void run(XorpTask& xorp_task) {
	debug_msg("OneoffTaskNode2::run() %p\n", this);
	//
	// XXX: we have to unschedule before the callback dispatch, in case
	// the callback decides to schedules again the task.
	//
	xorp_task.unschedule();
	_cb->dispatch();
    }

    OneoffTaskCallback _cb;
};

class RepeatedTaskNode2 : public TaskNode {
public:
    RepeatedTaskNode2(TaskList* task_list, const RepeatedTaskCallback& cb)
	: TaskNode(task_list, callback(this, &RepeatedTaskNode2::run)),
	  _cb(cb) {}

private:
    void run(XorpTask& xorp_task) {
	if (! _cb->dispatch()) {
	    xorp_task.unschedule();
	}
    }

    RepeatedTaskCallback _cb;
};


// ----------------------------------------------------------------------------
// XorpTask

void
XorpTask::unschedule()
{
    if (_task_node != NULL)
	_task_node->unschedule();
}

bool
XorpTask::scheduled() const
{
    if (_task_node != NULL)
	return _task_node->scheduled();
    else
	return false;
}


// ----------------------------------------------------------------------------
// TaskList

XorpTask
TaskList::new_oneoff_task(const OneoffTaskCallback& cb,
			  int priority, int weight)
{
    debug_msg("new oneoff task %p p = %d, w = %d\n", this, priority, weight);

    TaskNode* task_node = new OneoffTaskNode2(this, cb);
    task_node->schedule(priority, weight);
    return XorpTask(task_node);
}

XorpTask
TaskList::new_task(const RepeatedTaskCallback& cb,
		   int priority, int weight)
{
    debug_msg("new task %p p = %d, w = %d\n", this, priority, weight);

    TaskNode* task_node = new RepeatedTaskNode2(this, cb);
    task_node->schedule(priority, weight);
    return XorpTask(task_node);
}

int
TaskList::get_runnable_priority() const
{
    map<int, RoundRobinQueue*>::const_iterator rri;

    for (rri = _rr_list.begin(); rri != _rr_list.end(); ++rri) {
	if (rri->second->size() != 0) {
	    return rri->first;
	}
    }

    return XorpTask::PRIORITY_INFINITY;
}

bool
TaskList::empty() const
{
    bool result = true;
    map<int, RoundRobinQueue*>::const_iterator rri;

    for (rri = _rr_list.begin(); rri != _rr_list.end(); ++rri) {
	if (rri->second->size() != 0) {
	    result = false;
	    break;
	}
    }

    return result;
}

void
TaskList::run()
{
    map<int, RoundRobinQueue*>::const_iterator rri;

    debug_msg("TaskList run()\n");

    for (rri = _rr_list.begin(); rri != _rr_list.end(); ++rri) {
	RoundRobinQueue* rr = rri->second;
	if (rr->size() != 0) {
	    TaskNode* task_node = static_cast<TaskNode*>(rr->get_next_entry());
	    debug_msg("node to run: %p\n", task_node);
	    XorpTask xorp_task(task_node);
	    task_node->run(xorp_task);
	    return;
	}
    }
}

RoundRobinQueue*
TaskList::find_round_robin(int priority)
{
    map<int, RoundRobinQueue*>::iterator rri = _rr_list.find(priority);

    if (rri == _rr_list.end()) {
	RoundRobinQueue* rr = new RoundRobinQueue();
	_rr_list[priority] = rr;
	return rr;
    } else {
	return rri->second;
    }
}

void 
TaskList::schedule_node(TaskNode* task_node)
{
    debug_msg("TaskList::schedule_node: n = %p\n", task_node);

    RoundRobinObjBase* obj = static_cast<RoundRobinObjBase*>(task_node);
    RoundRobinQueue* rr = find_round_robin(task_node->priority());
    rr->push(obj, task_node->weight());
}

void
TaskList::unschedule_node(TaskNode* task_node)
{
    debug_msg("TaskList::unschedule_node: n = %p\n", task_node);

    RoundRobinObjBase* obj = static_cast<RoundRobinObjBase*>(task_node);
    RoundRobinQueue* rr = find_round_robin(task_node->priority());
    rr->pop_obj(obj);
}
