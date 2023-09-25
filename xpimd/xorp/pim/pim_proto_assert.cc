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

#ident "$XORP: xorp/pim/pim_proto_assert.cc,v 1.29 2007/02/16 22:46:49 pavlin Exp $"


//
// PIM PIM_ASSERT messages processing.
//


#include "pim_module.h"
#include "libxorp/xorp.h"
#include "libxorp/xlog.h"
#include "libxorp/debug.h"
#include "libxorp/ipvx.hh"

#include "pim_mre.hh"
#include "pim_mrt.hh"
#include "pim_proto_assert.hh"
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
 * PimVif::pim_assert_recv:
 * @pim_nbr: The PIM neighbor message originator (or NULL if not a neighbor).
 * @src: The message source address.
 * @dst: The message destination address.
 * @buffer: The buffer with the message.
 * 
 * Receive PIM_ASSERT message.
 * 
 * Return value: %XORP_OK on success, otherwise %XORP_ERROR.
 **/
int
PimVif::pim_assert_recv(PimNbr *pim_nbr,
			const IPvX& src,
			const IPvX& dst,
			buffer_t *buffer)
{
    int			rcvd_family;
    uint8_t		group_mask_len;
    uint8_t		group_addr_reserved_flags;
    IPvX		assert_source_addr(family());
    IPvX		assert_group_addr(family());
    uint32_t		metric_preference, metric;
    AssertMetric	assert_metric(src);
    bool		rpt_bit;
    
    //
    // Parse the message
    //
    GET_ENCODED_GROUP_ADDR(rcvd_family, assert_group_addr, group_mask_len,
			   group_addr_reserved_flags, buffer);
    GET_ENCODED_UNICAST_ADDR(rcvd_family, assert_source_addr, buffer);
    BUFFER_GET_HOST_32(metric_preference, buffer);
    BUFFER_GET_HOST_32(metric, buffer);
    // The RPTbit
    if (metric_preference & PIM_ASSERT_RPT_BIT)
	rpt_bit = true;
    else
	rpt_bit = false;
    metric_preference &= ~PIM_ASSERT_RPT_BIT;
    
    // The assert metrics
    assert_metric.set_rpt_bit_flag(rpt_bit);
    assert_metric.set_metric_preference(metric_preference);
    assert_metric.set_metric(metric);
    assert_metric.set_addr(src);
    
    //
    // Process the assert data
    //
    pim_assert_process(pim_nbr, src, dst,
		       assert_source_addr, assert_group_addr,
		       group_mask_len, &assert_metric);
    
    // UNUSED(dst);
    return (XORP_OK);
    
    // Various error processing
 rcvlen_error:
    XLOG_WARNING("RX %s from %s to %s: "
		 "invalid message length",
		 PIMTYPE2ASCII(PIM_ASSERT),
		 cstring(src), cstring(dst));
    ++_pimstat_rx_malformed_packet;
    return (XORP_ERROR);
    
 rcvd_mask_len_error:
    XLOG_WARNING("RX %s from %s to %s: "
		 "invalid group mask length = %d",
		 PIMTYPE2ASCII(PIM_ASSERT),
		 cstring(src), cstring(dst), group_mask_len);
    return (XORP_ERROR);
    
 rcvd_family_error:
    XLOG_WARNING("RX %s from %s to %s: "
		 "invalid address family inside = %d",
		 PIMTYPE2ASCII(PIM_ASSERT),
		 cstring(src), cstring(dst), rcvd_family);
    return (XORP_ERROR);
}

int
PimVif::pim_assert_process(PimNbr *pim_nbr,
			   const IPvX& src, const IPvX& dst,
			   const IPvX& assert_source_addr,
			   const IPvX& assert_group_addr,
			   uint8_t group_mask_len, AssertMetric *assert_metric)
{
    PimMre	*pim_mre_sg, *pim_mre_wc;
    int ret_value;
    
    if (group_mask_len != IPvX::addr_bitlen(family())) {
	XLOG_WARNING("RX %s from %s to %s: "
		     "invalid group mask length = %d "
		     "instead of %u",
		     PIMTYPE2ASCII(PIM_ASSERT),
		     cstring(src), cstring(dst),
		     group_mask_len,
		     XORP_UINT_CAST(IPvX::addr_bitlen(family())));
	return (XORP_ERROR);
    }
    
    if (! assert_group_addr.is_multicast()) {
	XLOG_WARNING("RX %s from %s to %s: "
		     "invalid assert group address = %s",
		     PIMTYPE2ASCII(PIM_ASSERT),
		     cstring(src), cstring(dst),
		     cstring(assert_group_addr));
	return (XORP_ERROR);
    }
    
    if (! ((assert_source_addr == IPvX::ZERO(family()))
	   || assert_source_addr.is_unicast())) {
	XLOG_WARNING("RX %s from %s to %s: "
		     "invalid assert source address = %s",
		     PIMTYPE2ASCII(PIM_ASSERT),
		     cstring(src), cstring(dst),
		     cstring(assert_source_addr));
	return (XORP_ERROR);
    }
    
    if (! assert_metric->rpt_bit_flag()) {
	// (S,G) Assert received. The assert source address must be unicast.
	if (! assert_source_addr.is_unicast()) {
	    XLOG_WARNING("RX %s from %s to %s: "
			 "invalid unicast assert source address = %s",
			 PIMTYPE2ASCII(PIM_ASSERT),
			 cstring(src), cstring(dst),
			 cstring(assert_source_addr));
	    return (XORP_ERROR);
	}
    }
    
    if (assert_metric->rpt_bit_flag()) {
	//
	// (*,G) Assert received
	//
	// If the source address is not zero, then first try to apply
	// this assert to the (S,G) assert state machine.
	// Only if the (S,G) assert state machine is in NoInfo state before
	// and after consideration of the received message, then apply the
	// message to the (*,G) assert state machine.
	//
	do {
	    bool is_sg_noinfo_old, is_sg_noinfo_new;
	    
	    if (assert_source_addr == IPvX::ZERO(family())) {
		//
		// Assert source address is zero, hence don't try to apply
		// it to the (S,G) assert state machine.
		//
		break;
	    }
	    
	    //
	    // XXX: strictly speaking, we should try to create
	    // the (S,G) state, and explicitly test the old
	    // and new (S,G) assert state.
	    // However, we use the observation that if there is no (S,G)
	    // routing state, then the (*,G) assert message will not change
	    // the (S,G) assert state mchine. Note that this observation is
	    // based on the specific details of the (S,G) assert state machine.
	    // In particular, the action in NoInfo state when
	    // "Receive Assert with RPTbit set and CouldAssert(S,G,I)".
	    // If there is no (S,G) routing state, then the SPTbit cannot
	    // be true, and therefore CouldAssert(S,G,I) also cannot be true.
	    //
	    pim_mre_sg = pim_mrt().pim_mre_find(assert_source_addr,
						assert_group_addr,
						PIM_MRE_SG, 0);
	    if (pim_mre_sg == NULL)
		break;		// XXX: see the above comment about not
				// creating the (S,G) routing state.
	    
	    // Compute the old and new (S,G) assert state.
	    is_sg_noinfo_old = pim_mre_sg->is_assert_noinfo_state(vif_index());
	    ret_value = pim_mre_sg->assert_process(this, assert_metric);
	    is_sg_noinfo_new = pim_mre_sg->is_assert_noinfo_state(vif_index());
	    
	    //
	    // Only if both the old and new state in the (S,G) assert state
	    // machine are in the NoInfo state, then we apply the (*,G) assert
	    // message to the (*,G) assert state machine.
	    //
	    if (is_sg_noinfo_old && is_sg_noinfo_new)
		break;
	    return (ret_value);
	} while (false);
	
	//
	// No transaction occured in the (S,G) assert state machine, and 
	// it is in NoInfo state.
	// Apply the assert to the (*,G) assert state machine.
	//
	pim_mre_wc = pim_mrt().pim_mre_find(assert_source_addr,
					    assert_group_addr,
					    PIM_MRE_WC, PIM_MRE_WC);
	if (pim_mre_wc == NULL) {
	    XLOG_ERROR("Internal error lookup/creating PIM multicast routing "
		       "entry for source = %s group = %s",
		       cstring(assert_source_addr),
		       cstring(assert_group_addr));
	    return (XORP_ERROR);
	}
	
	ret_value = pim_mre_wc->assert_process(this, assert_metric);
	// Try to remove the entry in case we don't need it
	pim_mre_wc->entry_try_remove();
	
	return (ret_value);
    }
    
    //
    // (S,G) Assert received
    //
    pim_mre_sg = pim_mrt().pim_mre_find(assert_source_addr, assert_group_addr,
					PIM_MRE_SG, PIM_MRE_SG);
    if (pim_mre_sg == NULL) {
	XLOG_ERROR("Internal error lookup/creating PIM multicast routing "
		   "entry for source = %s group = %s",
		   cstring(assert_source_addr), cstring(assert_group_addr));
	return (XORP_ERROR);
    }
    
    ret_value = pim_mre_sg->assert_process(this, assert_metric);
    // Try to remove the entry in case we don't need it.
    pim_mre_sg->entry_try_remove();
    
    return (ret_value);
    
    UNUSED(pim_nbr);
}

//
// XXX: @assert_source_addr is the source address inside the assert message
// XXX: applies only for (S,G) and (*,G)
//
int
PimVif::pim_assert_mre_send(PimMre *pim_mre, const IPvX& assert_source_addr,
			    string& error_msg)
{
    IPvX	assert_group_addr(family());
    uint32_t	metric_preference, metric;
    bool	rpt_bit = true;
    int		ret_value;
    
    if (! (pim_mre->is_sg() || pim_mre->is_wc()))
	return (XORP_ERROR);
    
    // Prepare the Assert data
    assert_group_addr  = pim_mre->group_addr();
    
    if (pim_mre->is_spt()) {
	rpt_bit = false;
	metric_preference = pim_mre->metric_preference_s();
	metric = pim_mre->metric_s();
    } else {
	rpt_bit = true;
	metric_preference = pim_mre->metric_preference_rp();
	metric = pim_mre->metric_rp();
    }
    
    ret_value = pim_assert_send(assert_source_addr, assert_group_addr, rpt_bit,
				metric_preference, metric, error_msg);
    return (ret_value);
}

// XXX: applies only for (S,G) and (*,G)
int
PimVif::pim_assert_cancel_send(PimMre *pim_mre, string& error_msg)
{
    IPvX	assert_source_addr(family());
    IPvX	assert_group_addr(family());
    uint32_t	metric_preference, metric;
    int		ret_value;
    bool	rpt_bit = false;
    
    if (! (pim_mre->is_sg() || pim_mre->is_wc())) {
	error_msg = c_format("Invalid PimMre entry type");
	return (XORP_ERROR);
    }
    
    // Prepare the Assert data
    if (pim_mre->is_sg()) {
	// AssertCancel(S,G)
	assert_source_addr = pim_mre->source_addr();
    } else {
	// AssertCancel(*,G)
	assert_source_addr = IPvX::ZERO(family());
    }
    assert_group_addr  = pim_mre->group_addr();
    rpt_bit = true;
    metric_preference = PIM_ASSERT_MAX_METRIC_PREFERENCE;
    metric = PIM_ASSERT_MAX_METRIC;
    
    ret_value = pim_assert_send(assert_source_addr, assert_group_addr, rpt_bit,
				metric_preference, metric, error_msg);
    return (ret_value);
}

int
PimVif::pim_assert_send(const IPvX& assert_source_addr,
			const IPvX& assert_group_addr,
			bool rpt_bit,
			uint32_t metric_preference,
			uint32_t metric,
			string& error_msg)
{
    buffer_t *buffer = buffer_send_prepare();
    uint8_t group_addr_reserved_flags = 0;
    uint8_t group_mask_len = IPvX::addr_bitlen(family());
    
    if (rpt_bit)
	metric_preference |= PIM_ASSERT_RPT_BIT;
    else
	metric_preference &= ~PIM_ASSERT_RPT_BIT;
    
    // Write all data to the buffer
    PUT_ENCODED_GROUP_ADDR(family(), assert_group_addr, group_mask_len,
			   group_addr_reserved_flags, buffer);
    PUT_ENCODED_UNICAST_ADDR(family(), assert_source_addr, buffer);
    BUFFER_PUT_HOST_32(metric_preference, buffer);
    BUFFER_PUT_HOST_32(metric, buffer);
    
    return (pim_send(primary_addr(), IPvX::PIM_ROUTERS(family()),
		     PIM_ASSERT, buffer, error_msg));
    
 invalid_addr_family_error:
    XLOG_UNREACHABLE();
    error_msg = c_format("TX %s from %s to %s: "
			 "invalid address family error = %d",
			 PIMTYPE2ASCII(PIM_ASSERT),
			 cstring(primary_addr()),
			 cstring(IPvX::PIM_ROUTERS(family())),
			 family());
    XLOG_ERROR("%s", error_msg.c_str());
    return (XORP_ERROR);
    
 buflen_error:
    XLOG_UNREACHABLE();
    error_msg = c_format("TX %s from %s to %s: "
			 "packet cannot fit into sending buffer",
			 PIMTYPE2ASCII(PIM_ASSERT),
			 cstring(primary_addr()),
			 cstring(IPvX::PIM_ROUTERS(family())));
    XLOG_ERROR("%s", error_msg.c_str());
    return (XORP_ERROR);
}

// Return true if I am better metric
bool
AssertMetric::operator>(const AssertMetric& other) const
{
    // The RPT flag: smaller is better
    if ( (! rpt_bit_flag()) && other.rpt_bit_flag())
	return (true);
    if (rpt_bit_flag() && (! other.rpt_bit_flag()))
	return (false);
    
    // The metric preference: smaller is better
    if (metric_preference() < other.metric_preference())
	return (true);
    if (metric_preference() > other.metric_preference())
	return (false);
    
    // The route metric: smaller is better
    if (metric() < other.metric())
	return (true);
    if (metric() > other.metric())
	return (false);
    
    // The IP address: bigger is better
    if (addr() > other.addr())
	return (true);
    
    return (false);
}

// Return true if contains infinite metric sent by AssertCancel message
bool
AssertMetric::is_assert_cancel_metric() const
{
    //
    // XXX: note that we don't check whether the address is zero.
    // We need to ignore the address, because it won't be zero for
    // AssertCancel messages.
    //
    return (_rpt_bit_flag
	    && (_metric_preference == PIM_ASSERT_MAX_METRIC_PREFERENCE)
	    && (_metric == PIM_ASSERT_MAX_METRIC));
}
