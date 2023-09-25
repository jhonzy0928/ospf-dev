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

// $XORP: xorp/mld6igmp/mld6igmp_source_record.hh,v 1.11 2007/02/16 22:46:37 pavlin Exp $

#ifndef __MLD6IGMP_MLD6IGMP_SOURCE_RECORD_HH__
#define __MLD6IGMP_MLD6IGMP_SOURCE_RECORD_HH__


//
// IGMP and MLD source record.
//


#include <map>
#include <set>

#include "libxorp/ipvx.hh"
#include "libxorp/timer.hh"


//
// Constants definitions
//

//
// Structures/classes, typedefs and macros
//

class EventLoop;
class Mld6igmpGroupRecord;

/**
 * @short A class to store information about a source (within a given
 * multicast group).
 */
class Mld6igmpSourceRecord {
public:
    /**
     * Constructor for a given group record and source address.
     * 
     * @param group_record the group record this entry belongs to.
     * @param source the source address.
     */
    Mld6igmpSourceRecord(Mld6igmpGroupRecord& group_record,
			 const IPvX& source);
    
    /**
     * Destructor.
     */
    ~Mld6igmpSourceRecord();

    /**
     * Get the group record this entry belongs to.
     * 
     * @return a reference to the group record this entry belongs to.
     */
    Mld6igmpGroupRecord& group_record()	const	{ return (_group_record); }
    
    /**
     * Get the source address.
     * 
     * @return the source address.
     */
    const IPvX&	source() const		{ return (_source); }

    /**
     * Get the address family.
     *
     * @return the address family.
     */
    int family() const { return _source.af(); }

    /**
     * Set the source timer.
     *
     * @param timeval the timeout interval of the source timer.
     */
    void set_source_timer(const TimeVal& timeval);

    /**
     * Cancel the source timer.
     */
    void cancel_source_timer();

    /**
     * Lower the source timer.
     *
     * @param timeval the timeout interval the source timer should be
     * lowered to.
     */
    void lower_source_timer(const TimeVal& timeval);

    /**
     * Get a reference to the source timer.
     * 
     * @return a reference to the source timer.
     */
    XorpTimer& source_timer() { return _source_timer; }

    /**
     * Get the number of seconds until the source timer expires.
     * 
     * @return the number of seconds until the source timer expires.
     */
    uint32_t	timeout_sec()	const;

    /**
     * Get the Query retransmission count.
     *
     * @return the Query retransmission count.
     */
    size_t query_retransmission_count() const {
	return _query_retransmission_count;
    }

    /**
     * Set the Query retransmission count.
     *
     * @param v the value to set.
     */
    void set_query_retransmission_count(size_t v) {
	_query_retransmission_count = v;
    }

private:
    /**
     * Timeout: the source timer has expired.
     */
    void source_timer_timeout();

    Mld6igmpGroupRecord& _group_record;	// The group record we belong to
    IPvX	_source;		// The source address
    XorpTimer	_source_timer;		// The source timer
    size_t	_query_retransmission_count; // Count for periodic Queries
};

/**
 * @short A class to store information about a set of sources.
 */
class Mld6igmpSourceSet : public map<IPvX, Mld6igmpSourceRecord *> {
public:
    /**
     * Constructor for a given group record.
     *
     * @param group_record the group record this set belongs to.
     */
    Mld6igmpSourceSet(Mld6igmpGroupRecord& group_record);

    /**
     * Destructor.
     */
    ~Mld6igmpSourceSet();

    /**
     * Find a source record.
     *
     * @param source the source address.
     * @return the corresponding source record (@ref Mld6igmpSourceRecord)
     * if found, otherwise NULL.
     */
    Mld6igmpSourceRecord* find_source_record(const IPvX& source);

    /**
     * Delete the payload of the set, and clear the set itself.
     */
    void delete_payload_and_clear();

    /**
     * Assignment operator for sets.
     *
     * @param other the right-hand operand.
     * @return the assigned set.
     */
    Mld6igmpSourceSet& operator=(const Mld6igmpSourceSet& other);

    /**
     * UNION operator for sets.
     *
     * @param other the right-hand operand.
     * @return the union of two sets. Note that if an element is in
     * both sets, we use the value from the first set.
     */
    Mld6igmpSourceSet operator+(const Mld6igmpSourceSet& other);

    /**
     * UNION operator for sets when the second operand is a set of IPvX
     * addresses.
     *
     * @param other the right-hand operand.
     * @return the union of two sets. Note that if an element is not in the
     * first set, then it is created (see @ref Mld6igmpSourceRecord).
     */
    Mld6igmpSourceSet operator+(const set<IPvX>& other);

    /**
     * INTERSECTION operator for sets.
     *
     * @param other the right-hand operand.
     * @return the intersection of two sets. Note that we use the values
     * from the first set.
     */
    Mld6igmpSourceSet operator*(const Mld6igmpSourceSet& other);

    /**
     * INTERSECTION operator for sets when the second operand is a set of IPvX
     * addresses.
     *
     * @param other the right-hand operand.
     * @return the intersection of two sets. Note that we use the values
     * from the first set.
     */
    Mld6igmpSourceSet operator*(const set<IPvX>& other);

    /**
     * REMOVAL operator for sets.
     *
     * @param other the right-hand operand.
     * @return the elements from the first set (after the elements from
     * the right-hand set have been removed).
     */
    Mld6igmpSourceSet operator-(const Mld6igmpSourceSet& other);

    /**
     * REMOVAL operator for sets when the second operand is a set of IPvX
     * addresses.
     *
     * @param other the right-hand operand.
     * @return the elements from the first set (after the elements from
     * the right-hand set have been removed).
     */
    Mld6igmpSourceSet operator-(const set<IPvX>& other);

    /**
     * Set the source timer for a set of source addresses.
     *
     * @param sources the set of source addresses whose source timer will
     * be set.
     * @param timeval the timeout interval of the source timer.
     */
    void set_source_timer(const set<IPvX>& sources, const TimeVal& timeval);

    /**
     * Set the source timer for all source addresses.
     *
     * @param timeval the timeout interval of the source timer.
     */
    void set_source_timer(const TimeVal& timeval);

    /**
     * Cancel the source timer for a set of source addresses.
     *
     * @param sources the set of source addresses whose source timer will
     * be canceled.
     */
    void cancel_source_timer(const set<IPvX>& sources);

    /**
     * Cancel the source timer for all source addresses.
     */
    void cancel_source_timer();

    /**
     * Lower the source timer for a set of sources.
     *
     * @param sources the source addresses.
     * @param timeval the timeout interval the source timer should be
     * lowered to.
     */
    void lower_source_timer(const set<IPvX>& sources, const TimeVal& timeval);

    /**
     * Extract the set of source addresses.
     *
     * @return the set with the source addresses.
     */
    set<IPvX> extract_source_addresses() const;

private:
    Mld6igmpGroupRecord& _group_record;	// The group record this set belongs to
};


//
// Global variables
//

//
// Global functions prototypes
//

#endif // __MLD6IGMP_MLD6IGMP_SOURCE_RECORD_HH__
