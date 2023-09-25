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

#ident "$XORP: xorp/pim/pim_mre.cc,v 1.41 2007/02/16 22:46:46 pavlin Exp $"

//
// PIM Multicast Routing Entry handling
//


#include "pim_module.h"
#include "libxorp/xorp.h"
#include "libxorp/xlog.h"
#include "libxorp/debug.h"
#include "libxorp/ipvx.hh"

#include "pim_mfc.hh"
#include "pim_mre.hh"
#include "pim_mrt.hh"
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


PimMre::PimMre(PimMrt& pim_mrt, const IPvX& source, const IPvX& group)
    : Mre<PimMre>(source, group),
      _pim_mrt(pim_mrt),
      _pim_rp(NULL),
      _mrib_rp(NULL),
      _mrib_s(NULL),
      _nbr_mrib_next_hop_rp(NULL),
      _nbr_mrib_next_hop_s(NULL),
      _rpfp_nbr_wc(NULL),
      _rpfp_nbr_sg(NULL),
      _rpfp_nbr_sg_rpt(NULL),
      _wc_entry(NULL),
      _rp_entry(NULL),
      _sg_sg_rpt_entry(NULL),
      _pmbr_addr(IPvX::ZERO(family())),
      _flags(0)
{
    for (size_t i = 0; i < MAX_VIFS; i++)
	_assert_winner_metrics[i] = NULL;
}

PimMre::~PimMre()
{
    //
    // Reset the pointers of the corresponding (S,G) and (S,G,rpt) entries
    // to this PimMre entry.
    //
    do {
	if (is_sg()) {
	    if (sg_rpt_entry() != NULL)
		sg_rpt_entry()->set_sg_entry(NULL);
	    break;
	}
	if (is_sg_rpt()) {
	    if (sg_entry() != NULL)
		sg_entry()->set_sg_rpt_entry(NULL);
	    break;
	}
    } while (false);
    
    for (size_t i = 0; i < MAX_VIFS; i++) {
	if (_assert_winner_metrics[i] != NULL) {
	    delete _assert_winner_metrics[i];
	    _assert_winner_metrics[i] = NULL;
	}
    }

    //
    // Remove this entry from various lists
    //
    remove_pim_mre_lists();
    
    //
    // Remove this entry from the PimMrt table
    //
    pim_mrt().remove_pim_mre(this);
}

//
// Add the PimMre entry to various lists
//
void
PimMre::add_pim_mre_lists()
{
    //
    // Add this entry to various lists towards neighbors
    //
    do {
	if (is_rp()) {
	    // (*,*,RP)
	    if (_nbr_mrib_next_hop_rp != NULL) {
		_nbr_mrib_next_hop_rp->add_pim_mre(this);
	    } else {
		pim_node().add_pim_mre_no_pim_nbr(this);
	    }
	    break;
	}
	
	if (is_wc()) {
	    // (*,G)
	    if (_nbr_mrib_next_hop_rp != NULL) {
		_nbr_mrib_next_hop_rp->add_pim_mre(this);
	    } else {
		pim_node().add_pim_mre_no_pim_nbr(this);
	    }
	    if (_rpfp_nbr_wc != _nbr_mrib_next_hop_rp) {
		if (_rpfp_nbr_wc != NULL) {
		    _rpfp_nbr_wc->add_pim_mre(this);
		} else {
		    pim_node().add_pim_mre_no_pim_nbr(this);
		}
	    }
	    break;
	}
	
	if (is_sg()) {
	    // (S,G)
	    if (_nbr_mrib_next_hop_s != NULL) {
		_nbr_mrib_next_hop_s->add_pim_mre(this);
	    } else {
		pim_node().add_pim_mre_no_pim_nbr(this);
	    }
	    if (_rpfp_nbr_sg != _nbr_mrib_next_hop_s) {
		if (_rpfp_nbr_sg != NULL) {
		    _rpfp_nbr_sg->add_pim_mre(this);
		} else {
		    pim_node().add_pim_mre_no_pim_nbr(this);
		}
	    }
	    break;
	}
	
	if (is_sg_rpt()) {
	    // (S,G,rpt)
	    if (_rpfp_nbr_sg_rpt != NULL) {
		_rpfp_nbr_sg_rpt->add_pim_mre(this);
	    } else {
		pim_node().add_pim_mre_no_pim_nbr(this);
	    }
	    break;
	}
	
	XLOG_UNREACHABLE();
	break;
    } while (false);
    
    //
    // Add this entry to the RP table
    //
    pim_node().rp_table().add_pim_mre(this);
}

//
// Remove the PimMre entry from various lists
//
void
PimMre::remove_pim_mre_lists()
{
    //
    // Remove this entry from various lists towards neighbors
    //
    do {
	if (is_rp()) {
	    // (*,*,RP)
	    if (_nbr_mrib_next_hop_rp != NULL) {
		_nbr_mrib_next_hop_rp->delete_pim_mre(this);
	    } else {
		pim_node().delete_pim_mre_no_pim_nbr(this);
	    }
	    _nbr_mrib_next_hop_rp = NULL;
	    break;
	}
	
	if (is_wc()) {
	    // (*,G)
	    if (_nbr_mrib_next_hop_rp != NULL) {
		_nbr_mrib_next_hop_rp->delete_pim_mre(this);
	    } else {
		pim_node().delete_pim_mre_no_pim_nbr(this);
	    }
	    if (_rpfp_nbr_wc != _nbr_mrib_next_hop_rp) {
		if (_rpfp_nbr_wc != NULL) {
		    _rpfp_nbr_wc->delete_pim_mre(this);
		} else {
		    pim_node().delete_pim_mre_no_pim_nbr(this);
		}
	    }
	    _nbr_mrib_next_hop_rp = NULL;
	    _rpfp_nbr_wc = NULL;
	    break;
	}
	
	if (is_sg()) {
	    // (S,G)
	    if (_nbr_mrib_next_hop_s != NULL) {
		_nbr_mrib_next_hop_s->delete_pim_mre(this);
	    } else {
		pim_node().delete_pim_mre_no_pim_nbr(this);
	    }
	    if (_rpfp_nbr_sg != _nbr_mrib_next_hop_s) {
		if (_rpfp_nbr_sg != NULL) {
		    _rpfp_nbr_sg->delete_pim_mre(this);
		} else {
		    pim_node().delete_pim_mre_no_pim_nbr(this);
		}
	    }
	    _nbr_mrib_next_hop_s = NULL;
	    _nbr_mrib_next_hop_rp = NULL;
	    break;
	}
	
	if (is_sg_rpt()) {
	    // (S,G,rpt)
	    if (_rpfp_nbr_sg_rpt != NULL) {
		_rpfp_nbr_sg_rpt->delete_pim_mre(this);
	    } else {
		pim_node().delete_pim_mre_no_pim_nbr(this);
	    }
	    _rpfp_nbr_sg_rpt = NULL;
	    break;
	}
	
	XLOG_UNREACHABLE();
	break;
    } while (false);
    
    //
    // Remove this entry from the RP table
    //
    pim_node().rp_table().delete_pim_mre(this);
}

PimNode&
PimMre::pim_node() const
{
    return (_pim_mrt.pim_node());
}

int
PimMre::family() const
{
    return (_pim_mrt.family());
}

uint32_t
PimMre::pim_register_vif_index() const
{
    return (_pim_mrt.pim_register_vif_index());
}

//
// Mifset functions
//
void
PimMre::set_sg(bool v)
{
    if (v) {
	set_sg_rpt(false);
	set_wc(false);
	set_rp(false);
	_flags |= PIM_MRE_SG;
    } else {
	_flags &= ~PIM_MRE_SG;
    }
}

void
PimMre::set_sg_rpt(bool v)
{
    if (v) {
	set_sg(false);
	set_wc(false);
	set_rp(false);
	_flags |= PIM_MRE_SG_RPT;
    } else {
	_flags &= ~PIM_MRE_SG_RPT;
    }
}

void
PimMre::set_wc(bool v)
{
    if (v) {
	set_sg(false);
	set_sg_rpt(false);
	set_rp(false);
	_flags |= PIM_MRE_WC;
    } else {
	_flags &= ~PIM_MRE_WC;
    }
}

void
PimMre::set_rp(bool v)
{
    if (v) {
	set_sg(false);
	set_sg_rpt(false);
	set_wc(false);
	_flags |= PIM_MRE_RP;
    } else {
	_flags &= ~PIM_MRE_RP;
    }
}

// Note: applies only for (S,G)
void
PimMre::set_spt(bool v)
{
    if (! is_sg())
	return;
    
    if (is_spt() == v)
	return;		// Nothing changed
    
    if (v) {
	_flags |= PIM_MRE_SPT;
    } else {
	_flags &= ~PIM_MRE_SPT;
    }
    
    pim_mrt().add_task_sptbit_sg(source_addr(), group_addr());
}

const IPvX *
PimMre::rp_addr_ptr() const
{
    if (is_rp())
	return (&source_addr());	// XXX: (*,*,RP) entry
    
    if (pim_rp() != NULL)
	return (&pim_rp()->rp_addr());
    
    return (NULL);
}

string
PimMre::rp_addr_string() const
{
    const IPvX *addr_ptr = rp_addr_ptr();
    
    if (addr_ptr != NULL)
	return (cstring(*addr_ptr));
    else
	return ("RP_ADDR_UNKNOWN");
}

//
// Set of state interface functions
// (See the "State Summarization Macros" section)
//
// Note: works for all entries
const Mifset&
PimMre::joins_rp() const
{
    static Mifset mifs;
    const PimMre *pim_mre_rp;
    
    if (is_rp()) {
	pim_mre_rp = this;
    } else {
	pim_mre_rp = rp_entry();
	if (pim_mre_rp == NULL) {
	    mifs.reset();
	    return (mifs);
	}
    }
    
    mifs = pim_mre_rp->downstream_join_state();
    mifs |= pim_mre_rp->downstream_prune_pending_state();
    return (mifs);
}

// Note: works for (*,G), (S,G), (S,G,rpt)
const Mifset&
PimMre::joins_wc() const
{
    static Mifset mifs;
    const PimMre *pim_mre_wc;
    
    if (is_wc()) {
	pim_mre_wc = this;
    } else {
	pim_mre_wc = wc_entry();
	if (pim_mre_wc == NULL) {
	    mifs.reset();
	    return (mifs);
	}
    }
    
    mifs = pim_mre_wc->downstream_join_state();
    mifs |= pim_mre_wc->downstream_prune_pending_state();
    return (mifs);
}

// Note: applies only for (S,G)
const Mifset&
PimMre::joins_sg() const
{
    static Mifset mifs;
    
    if (! is_sg()) {
	mifs.reset();
	return (mifs);
    }
    
    mifs = downstream_join_state();
    mifs |= downstream_prune_pending_state();
    return (mifs);
}

// Note: applies only for (S,G,rpt)
const Mifset&
PimMre::prunes_sg_rpt() const
{
    static Mifset mifs;
    
    if (! is_sg_rpt()) {
	mifs.reset();
	return (mifs);
    }
    
    mifs = downstream_prune_state();
    mifs |= downstream_prune_tmp_state();
    return (mifs);    
}

// Note: works for (*,*,RP), (*,G), (S,G), (S,G,rpt)
const Mifset&
PimMre::immediate_olist_rp() const
{
    return (joins_rp());
}

// Note: works for (*,G), (S,G), (S,G,rpt)
const Mifset&
PimMre::immediate_olist_wc() const
{
    static Mifset mifs;
    
    if (! (is_wc() || is_sg() || is_sg_rpt())) {
	mifs.reset();
	return (mifs);
    }
    
    mifs = joins_wc();
    mifs |= pim_include_wc();
    mifs &= ~lost_assert_wc();
    
    return (mifs);
}

// Note: works for (S,G)
const Mifset&
PimMre::immediate_olist_sg() const
{
    static Mifset mifs;
    
    if (! is_sg()) {
	mifs.reset();
	return (mifs);
    }
    
    mifs = joins_sg();
    mifs |= pim_include_sg();
    mifs &= ~lost_assert_sg();
    
    return (mifs);
}

// Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
const Mifset&
PimMre::inherited_olist_sg() const
{
    static Mifset mifs;
    const PimMre *pim_mre_sg = NULL;
    
    mifs = inherited_olist_sg_rpt();

    // Get a pointer to the (S,G) entry
    do {
	if (is_sg()) {
	    pim_mre_sg = this;
	    break;
	}
	if (is_sg_rpt()) {
	    pim_mre_sg = sg_entry();
	    break;
	}
	break;
    } while (false);

    if (pim_mre_sg != NULL) {
	mifs |= pim_mre_sg->joins_sg();
	mifs |= pim_mre_sg->pim_include_sg();
	mifs &= ~(pim_mre_sg->lost_assert_sg());
    }

    return (mifs);
}

// Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
const Mifset&
PimMre::inherited_olist_sg_rpt() const
{
    static Mifset mifs;
    Mifset mifs2;
    
    mifs.reset();
    
    do {
	if (is_sg_rpt()) {
	    //
	    // (S,G,rpt)
	    //
	    PimMre *pim_mre_sg = sg_entry();
	    
	    mifs = joins_rp();
	    mifs |= joins_wc();
	    mifs &= ~prunes_sg_rpt();
	    
	    mifs2 = pim_include_wc();
	    if (pim_mre_sg != NULL)
		mifs2 &= ~(pim_mre_sg->pim_exclude_sg());
	    mifs |= mifs2;
	    
	    mifs2 = lost_assert_wc();
	    mifs2 |= lost_assert_sg_rpt();
	    mifs &= ~mifs2;
	    
	    break;
	}
	if (is_sg()) {
	    //
	    // (S,G)
	    //
	    PimMre *pim_mre_sg_rpt = sg_rpt_entry();

	    mifs = joins_rp();
	    mifs |= joins_wc();
	    if (pim_mre_sg_rpt != NULL)
		mifs &= ~(pim_mre_sg_rpt->prunes_sg_rpt());
	    
	    mifs2 = pim_include_wc();
	    mifs2 &= ~pim_exclude_sg();
	    mifs |= mifs2;
	    
	    mifs2 = lost_assert_wc();
	    mifs2 |= lost_assert_sg_rpt(); // XXX: applies for (S,G) as well
	    mifs &= ~mifs2;
	    
	    break;
	}
	if (is_wc()) {
	    //
	    // (*,G)
	    //
	    mifs = joins_rp();
	    mifs |= joins_wc();
	    
	    mifs2 = pim_include_wc();
	    mifs |= mifs2;
	    
	    mifs2 = lost_assert_wc();
	    mifs &= ~mifs2;
	    
	    break;
	}
	if (is_rp()) {
	    //
	    // (*,*,RP)
	    //
	    mifs = joins_rp();
	    
	    break;
	}
    } while (false);
    
    return (mifs);
}

// Note: works for (*,G), (S,G), (S,G,rpt)
const Mifset&
PimMre::pim_include_wc() const
{
    static Mifset mifs;
    
    mifs = i_am_dr();
    mifs &= ~lost_assert_wc();
    mifs |= i_am_assert_winner_wc();
    mifs &= local_receiver_include_wc();
    
    return (mifs);
}

// Note: works only for (S,G)
const Mifset&
PimMre::pim_include_sg() const
{
    static Mifset mifs;
    
    mifs = i_am_dr();
    mifs &= ~lost_assert_sg();
    mifs |= i_am_assert_winner_sg();
    mifs &= local_receiver_include_sg();
    
    return (mifs);
}

// Note: works only for (S,G)
const Mifset&
PimMre::pim_exclude_sg() const
{
    static Mifset mifs;
    
    mifs = i_am_dr();
    mifs &= ~lost_assert_wc();
    mifs |= i_am_assert_winner_wc();
    mifs &= local_receiver_exclude_sg();
    
    return (mifs);
}

// Note: works only for (S,G)
const Mifset&
PimMre::local_receiver_include_sg() const
{
    static Mifset mifs;
    
    if (! is_sg()) {
	mifs.reset();
	return (mifs);
    }
    
    return (local_receiver_include());
}

// Note: works for (*,G), (S,G), (S,G,rpt)
const Mifset&
PimMre::local_receiver_include_wc() const
{
    static Mifset mifs;
    const PimMre *pim_mre_wc;
    
    if (is_wc()) {
	pim_mre_wc = this;
    } else {
	pim_mre_wc = wc_entry();
	if (pim_mre_wc == NULL) {
	    mifs.reset();
	    return (mifs);
	}
    }
    
    return (pim_mre_wc->local_receiver_include());
}

const Mifset&
PimMre::local_receiver_exclude_sg() const
{
    static Mifset mifs;
    
    if (! is_sg()) {
	mifs.reset();
	return (mifs);
    }
    
    mifs = local_receiver_include_wc();
    mifs &= local_receiver_exclude();
    
    return (mifs);
}

void
PimMre::set_local_receiver_include(uint32_t vif_index, bool v)
{
    if (vif_index == Vif::VIF_INDEX_INVALID)
	return;
    
    if (_local_receiver_include.test(vif_index) == v)
	return;			// Nothing changed
    
    if (v)
	_local_receiver_include.set(vif_index);
    else
	_local_receiver_include.reset(vif_index);
    
    // Add the task to recompute the effect of this and take actions
    do {
	if (is_wc()) {
	    pim_mrt().add_task_local_receiver_include_wc(vif_index,
							 group_addr());
	    break;
	}
	if (is_sg()) {
	    pim_mrt().add_task_local_receiver_include_sg(vif_index,
							 source_addr(),
							 group_addr());
	    break;
	}
    } while (false);
    
    // Try to remove the entry
    if (! v)
	entry_try_remove();
}

void
PimMre::set_local_receiver_exclude(uint32_t vif_index, bool v)
{
    if (vif_index == Vif::VIF_INDEX_INVALID)
	return;
    
    if (_local_receiver_exclude.test(vif_index) == v)
	return;			// Nothing changed
    
    if (v)
	_local_receiver_exclude.set(vif_index);
    else
	_local_receiver_exclude.reset(vif_index);

    // Add the task to recompute the effect of this and take actions
    do {
	if (is_sg()) {
	    pim_mrt().add_task_local_receiver_exclude_sg(vif_index,
							 source_addr(),
							 group_addr());
	    break;
	}
    } while (false);
    
    // Try to remove the entry
    if (! v)
	entry_try_remove();
}

// Try to remove PimMre entry if not needed anymore.
// The function is generic and can be applied to any type of PimMre entry.
// Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
// Return true if entry is scheduled to be removed, othewise false.
bool
PimMre::entry_try_remove()
{
    bool ret_value;
    
    // TODO: XXX: PAVPAVPAV: make sure it is called
    // from all appropriate places!!
    
    if (is_task_delete_pending())
	return (true);		// The entry is already pending deletion
    
    ret_value = entry_can_remove();
    if (ret_value)
	pim_mrt().add_task_delete_pim_mre(this);
    
    return (ret_value);
}

// Test if it is OK to remove PimMre entry.
// It tests whether all downstream and upstream
// states are NoInfo, as well as whether no relevant timers are running.
// The function is generic and can be applied to any type of PimMre entry.
// Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
// Return true if entry can be removed, othewise false.
bool
PimMre::entry_can_remove() const
{
    // TODO: XXX: PAVPAVPAV: add more checks if needed
    
    if (_local_receiver_include.any())
	return (false);
    if (_local_receiver_exclude.any())
	return (false);
    if (_downstream_join_state.any())
	return (false);
    if (_downstream_prune_state.any())
	return (false);
    if (_downstream_prune_pending_state.any())
	return (false);
    // XXX: we don't really need to test _downstream_tmp_state, because
    // it is used only in combination with other state, but anyway...
    if (_downstream_tmp_state.any())
	return (false);
    if (is_rp() || is_wc() || is_sg()) {
	if (is_joined_state())
	    return (false);
    }
    if (is_rp()) {
	if (immediate_olist_rp().any())
	    return (false);
	if ((rp_addr_ptr() != NULL)
	    && pim_node().rp_table().has_rp_addr(*rp_addr_ptr())) {
	    return (false);
	}
    }
    if (is_wc()) {
	if (immediate_olist_wc().any())
	    return (false);
	if (pim_include_wc().any())
	    return (false);
    }
    if (is_sg()) {
	if (immediate_olist_sg().any())
	    return (false);
	if (pim_include_sg().any())
	    return (false);
	if (pim_exclude_sg().any())
	    return (false);
    }
    // XXX: (S,G,rpt) entry cannot be removed if upstream state is Pruned,
    // or the Override Timer is running (when in Not Pruned state).
    if (is_sg_rpt()) {
	if (is_pruned_state())
	    return (false);
	if (is_not_pruned_state() && const_override_timer().scheduled())
	    return (false);
    }
#if 0		// TODO: XXX: PAVPAVPAV: not needed?
    if (inherited_olist_sg().any())
	return (false);
    if (inherited_olist_sg_rpt().any())
	return (false);
#endif // 0
#if 1		// TODO: XXX: PAVPAVPAV: not needed?
    if (is_sg()) {
	if (! is_register_noinfo_state())
	    return (false);
    }
#endif // 1
    
#if 1		// TODO: XXX: PAVPAVPAV: not needed?
    if (is_wc() || is_sg()) {
	if (i_am_assert_winner_state().any()
	    || i_am_assert_loser_state().any())
	    return (false);
    }
#endif // 1
    
    if (is_sg()) {
	// TODO: OK NOT to remove if the KeepaliveTimer(S,G) is running?
	if (is_keepalive_timer_running())
	    return (false);
    }
    
    return (true);
}

// The KeepaliveTimer(S,G)
// Note: applies only for (S,G)
void
PimMre::start_keepalive_timer()
{
    if (! is_sg())
	return;
    
    if (is_keepalive_timer_running())
	return;		// Nothing changed
    
    _flags |= PIM_MRE_KEEPALIVE_TIMER_IS_SET;
    
    pim_mrt().add_task_keepalive_timer_sg(source_addr(), group_addr());
}

// The KeepaliveTimer(S,G)
// Note: applies only for (S,G)
void
PimMre::cancel_keepalive_timer()
{
    if (! is_sg())
	return;
    
    if (! is_keepalive_timer_running())
	return;		// Nothing changed

    //
    // If an RP, the PMBR value must be cleared when the Keepalive Timer
    // expires.
    //
    // XXX: We always clear the PMBR value and we don't use "is_rp()"
    // to check whether we are the RP, because the PMBR value is not used
    // on non-RPs, so it doesn't hurt to unconditionally reset it.
    // Otherwise, we will have to add state dependency whenever the
    // RP changes. This introduces complexity without any benefit,
    // hence we don't do it.
    //
    clear_pmbr_addr();

    _flags &= ~PIM_MRE_KEEPALIVE_TIMER_IS_SET;
    
    pim_mrt().add_task_keepalive_timer_sg(source_addr(), group_addr());
}

// The KeepaliveTimer(S,G)
// XXX: applies only for (S,G)
bool
PimMre::is_keepalive_timer_running() const
{
    if (! is_sg())
	return (false);
    
    return (_flags & PIM_MRE_KEEPALIVE_TIMER_IS_SET);
}

// The KeepaliveTimer(S,G)
// Note: applies only for (S,G)
void
PimMre::keepalive_timer_timeout()
{
    if (! is_sg())
	return;
    
    if (! is_keepalive_timer_running())
	return;
    //
    // If an RP, the PMBR value must be cleared when the Keepalive Timer
    // expires.
    // XXX: this will happen inside cancel_keepalive_timer()
    //

    cancel_keepalive_timer();
    entry_try_remove();
}

// The KeepaliveTimer(S,G)
// Note: applies only for (S,G)
void
PimMre::recompute_set_keepalive_timer_sg()
{
    bool should_set_keepalive_timer_sg = false;
    PimMfc *pim_mfc;

    if (! is_sg())
	return;

    if (is_keepalive_timer_running())
	return;

    //
    // Test if there is PimMfc entry. If there is no PimMfc entry,
    // then there is no (S,G) traffic, and therefore the KeepaliveTimer(S,G)
    // does not need to be started.
    //
    pim_mfc = pim_mrt().pim_mfc_find(source_addr(), group_addr(), false);
    if (pim_mfc == NULL)
	return;

    do {
	//
	// Test the following scenario:
	//
	// On receipt of data from S to G on interface iif:
	//     if( DirectlyConnected(S) == TRUE AND iif == RPF_interface(S) ) {
	//         set KeepaliveTimer(S,G) to Keepalive_Period
	//         ...
	//     }
	//
	//     if( iif == RPF_interface(S) AND UpstreamJPState(S,G) == Joined AND
	//        inherited_olist(S,G) != NULL ) {
	//            set KeepaliveTimer(S,G) to Keepalive_Period
	//     }
	//
	if (is_directly_connected_s()
	    && (pim_mfc->iif_vif_index() == rpf_interface_s())) {
	    should_set_keepalive_timer_sg = true;
	    break;
	}
	if ((pim_mfc->iif_vif_index() == rpf_interface_s())
	    && is_joined_state()
	    && inherited_olist_sg().any()) {
	    should_set_keepalive_timer_sg = true;
	    break;
	}

	//
	// Test the following scenario:
	//
	// CheckSwitchToSpt(S,G) {
	//     if ( ( pim_include(*,G) (-) pim_exclude(S,G)
	//            (+) pim_include(S,G) != NULL )
	//          AND SwitchToSptDesired(S,G) ) {
	//            # Note: Restarting the KAT will result in the SPT switch
	//            restart KeepaliveTimer(S,G);
	//
 	Mifset mifs;
	mifs = pim_include_wc();
	mifs &= ~pim_exclude_sg();
	mifs |= pim_include_sg();
	if (mifs.any() && was_switch_to_spt_desired_sg()) {
	    should_set_keepalive_timer_sg = true;
	    break;
	}

	//
	// Test the following scenario:
	//
	// packet_arrives_on_rp_tunnel( pkt ) {
	//     ....
	//     if( I_am_RP(G) AND outer.dst == RP(G) ) {
	//           ...
	//           if ( SPTbit(S,G) OR SwitchToSptDesired(S,G) ) {
	//                if ( sentRegisterStop == TRUE ) {
	//                     restart KeepaliveTimer(S,G) to RP_Keepalive_Period;
	//                } else {
	//                     restart KeepaliveTimer(S,G) to Keepalive_Period;
	//                }
	//           }
	//           ...
	//
	if (i_am_rp() && (is_spt() || was_switch_to_spt_desired_sg())) {
	    should_set_keepalive_timer_sg = true;
	    break;
	}

	break;
    } while (false);

    if (should_set_keepalive_timer_sg) {
	//
	// Start the (S,G) Keepalive Timer, and add a dataflow monitor
	// (if it wasn't added yet).
	//
	start_keepalive_timer();

	if (! pim_mfc->has_idle_dataflow_monitor()) {
	    //
	    // Add a dataflow monitor to expire idle (S,G) PimMre state
	    // and/or idle PimMfc+MFC state
	    //
	    // First, compute the dataflow monitor value, which may be
	    // different for (S,G) entry in the RP that is used for
	    // Register decapsulation.
	    //
	    uint32_t expected_dataflow_monitor_sec = PIM_KEEPALIVE_PERIOD_DEFAULT;
	    if (is_kat_set_to_rp_keepalive_period()) {
		if (expected_dataflow_monitor_sec
		    < PIM_RP_KEEPALIVE_PERIOD_DEFAULT) {
		    expected_dataflow_monitor_sec
			= PIM_RP_KEEPALIVE_PERIOD_DEFAULT;
		}
	    }

	    pim_mfc->add_dataflow_monitor(expected_dataflow_monitor_sec, 0,
					  0,	// threshold_packets
					  0,	// threshold_bytes
					  true,	// is_threshold_in_packets
					  false,// is_threshold_in_bytes
					  false,// is_geq_upcall ">="
					  true);// is_leq_upcall "<="
	}
    }
}

//
// MISC. info
//
// Note: applies for all entries
const Mifset&
PimMre::i_am_dr() const
{
    return pim_mrt().i_am_dr();
}

//
// PimVif-related methods
//
// Note: applies for (*,*,RP)
void
PimMre::recompute_start_vif_rp(uint32_t vif_index)
{
    // XXX: nothing to do
    
    UNUSED(vif_index);
}

// Note: applies for (*,G)
void
PimMre::recompute_start_vif_wc(uint32_t vif_index)
{
    // XXX: nothing to do
    
    UNUSED(vif_index);
}

// Note: applies for (S,G)
void
PimMre::recompute_start_vif_sg(uint32_t vif_index)
{
    // XXX: nothing to do
    
    UNUSED(vif_index);
}

// Note: applies for (G,G,rpt)
void
PimMre::recompute_start_vif_sg_rpt(uint32_t vif_index)
{
    // XXX: nothing to do
    
    UNUSED(vif_index);
}

// Note: applies for (*,*,RP)
void
PimMre::recompute_stop_vif_rp(uint32_t vif_index)
{
    //
    // Reset all associated state with 'vif_index'
    //
    
    downstream_prune_pending_timer_timeout_rp(vif_index);
    _downstream_prune_pending_timers[vif_index].unschedule();
    downstream_expiry_timer_timeout_rp(vif_index);
    _downstream_expiry_timers[vif_index].unschedule();
    
    // XXX: assert-related state doesn't apply for (*,*,RP) entry
    
    set_local_receiver_include(vif_index, false);
    set_local_receiver_exclude(vif_index, false);
    set_downstream_noinfo_state(vif_index);
}

// Note: applies for (*,G)
void
PimMre::recompute_stop_vif_wc(uint32_t vif_index)
{
    //
    // Reset all associated state with 'vif_index'
    //
    
    downstream_prune_pending_timer_timeout_wc(vif_index);
    _downstream_prune_pending_timers[vif_index].unschedule();
    downstream_expiry_timer_timeout_wc(vif_index);
    _downstream_expiry_timers[vif_index].unschedule();

    //
    // XXX: don't call assert_timer_timeout_wc(vif_index)
    // because it may transmit a regular Assert message.
    //
    process_could_assert_wc(vif_index, false);
    delete_assert_winner_metric_wc(vif_index);
    
    _assert_timers[vif_index].unschedule();
    set_assert_tracking_desired_state(vif_index, false);
    set_could_assert_state(vif_index, false);
    delete_assert_winner_metric_wc(vif_index);
    set_assert_noinfo_state(vif_index);
    _asserts_rate_limit.reset(vif_index);
    
    set_local_receiver_include(vif_index, false);
    set_local_receiver_exclude(vif_index, false);
    set_downstream_noinfo_state(vif_index);
}

// Note: applies for (S,G)
void
PimMre::recompute_stop_vif_sg(uint32_t vif_index)
{
    //
    // Reset all associated state with 'vif_index'
    //
    
    downstream_prune_pending_timer_timeout_sg(vif_index);
    _downstream_prune_pending_timers[vif_index].unschedule();
    downstream_expiry_timer_timeout_sg(vif_index);
    _downstream_expiry_timers[vif_index].unschedule();

    //
    // XXX: don't call assert_timer_timeout_sg(vif_index)
    // because it may transmit a regular Assert message.
    //
    process_could_assert_sg(vif_index, false);
    delete_assert_winner_metric_sg(vif_index);
    set_assert_winner_metric_is_better_than_spt_assert_metric_sg(vif_index,
								 false);
    
    _assert_timers[vif_index].unschedule();
    set_assert_tracking_desired_state(vif_index, false);
    set_could_assert_state(vif_index, false);
    delete_assert_winner_metric_sg(vif_index);
    set_assert_noinfo_state(vif_index);
    _asserts_rate_limit.reset(vif_index);
    
    set_local_receiver_include(vif_index, false);
    set_local_receiver_exclude(vif_index, false);
    set_downstream_noinfo_state(vif_index);
}

// Note: applies for (S,G,rpt)
void
PimMre::recompute_stop_vif_sg_rpt(uint32_t vif_index)
{
    //
    // Reset all associated state with 'vif_index'
    //
    
    downstream_prune_pending_timer_timeout_sg_rpt(vif_index);
    _downstream_prune_pending_timers[vif_index].unschedule();
    downstream_expiry_timer_timeout_sg_rpt(vif_index);
    _downstream_expiry_timers[vif_index].unschedule();
    
    // XXX: assert-related state doesn't apply for (S,G,rpt) entry
    
    set_local_receiver_include(vif_index, false);
    set_local_receiver_exclude(vif_index, false);
    set_downstream_noinfo_state(vif_index);
}

// Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
void
PimMre::add_pim_mre_rp_entry()
{
    if (is_wc() || is_sg() || is_sg_rpt()) {
	// XXX: the RP-related state is set by a special task
	// uncond_set_pim_rp(compute_rp());
    }
}

// Note: applies for (*,G), (S,G), (S,G,rpt)
void
PimMre::add_pim_mre_wc_entry()
{
    if (is_sg() || is_sg_rpt()) {
	PimMre *pim_mre_wc = pim_mrt().pim_mre_find(source_addr(),
						    group_addr(),
						    PIM_MRE_WC,
						    0);
	if (pim_mre_wc == wc_entry())
	    return;		// Nothing changed
	// Remove from the PimNbr and PimRp lists.
	//
	// Remove this entry from the RP table lists.
	// XXX: We don't delete this entry from the PimNbr lists,
	// because an (S,G) or (S,G,rpt) is on those lists regardless
	// whether it has a matching (*,G) entry.
	XLOG_ASSERT(pim_mre_wc != NULL);
	pim_node().rp_table().delete_pim_mre(this);
	set_wc_entry(pim_mre_wc);
    }
}

// Note: applies for (S,G), (S,G,rpt)
void
PimMre::add_pim_mre_sg_entry()
{
    // XXX: the cross-pointers between (S,G) and and (S,G,rpt) are set
    // when the PimMre entry was created
}

// Note: applies for (S,G), (S,G,rpt)
void
PimMre::add_pim_mre_sg_rpt_entry()
{
    // XXX: the cross-pointers between (S,G) and and (S,G,rpt) are set
    // when the PimMre entry was created
}

// Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
void
PimMre::remove_pim_mre_rp_entry()
{
    if (is_rp()) {
	if (is_task_delete_pending() && entry_can_remove()) {
	    //
	    // Remove the entry from the PimMrt, and mark it as deletion done
	    //
	    pim_mrt().remove_pim_mre(this);
	    set_is_task_delete_done(true);
	} else {
	    set_is_task_delete_pending(false);
	    set_is_task_delete_done(false);
	    return;
	}
    }
    
    if (is_wc() || is_sg() || is_sg_rpt()) {
	// XXX: the RP-related state is set by a special task
	// uncond_set_pim_rp(compute_rp());
    }
}

// Note: applies for (*,G), (S,G), (S,G,rpt)
void
PimMre::remove_pim_mre_wc_entry()
{
    if (is_wc()) {
	if (is_task_delete_pending() && entry_can_remove()) {
	    //
	    // Remove the entry from the PimMrt, and mark it as deletion done
	    //
	    pim_mrt().remove_pim_mre(this);
	    set_is_task_delete_done(true);
	} else {
	    set_is_task_delete_pending(false);
	    set_is_task_delete_done(false);
	    return;
	}
    }
    
    if (is_sg() || is_sg_rpt()) {
	PimMre *pim_mre_wc = pim_mrt().pim_mre_find(source_addr(),
						    group_addr(),
						    PIM_MRE_WC,
						    0);
	if (pim_mre_wc == wc_entry())
	    return;		// Nothing changed
	set_wc_entry(pim_mre_wc);
	// Add to the PimNbr and PimRp lists.
	add_pim_mre_lists();
    }
}

// Note: applies for (S,G), (S,G,rpt)
void
PimMre::remove_pim_mre_sg_entry()
{
    if (is_sg()) {
	if (is_task_delete_pending() && entry_can_remove()) {
	    //
	    // Remove the entry from the PimMrt, and mark it as deletion done
	    //
	    pim_mrt().remove_pim_mre(this);
	    set_is_task_delete_done(true);
	} else {
	    set_is_task_delete_pending(false);
	    set_is_task_delete_done(false);
	    return;
	}
    }
    
    if (is_sg_rpt()) {
	PimMre *pim_mre_sg = pim_mrt().pim_mre_find(source_addr(),
						    group_addr(),
						    PIM_MRE_SG,
						    0);
	if (pim_mre_sg == sg_entry())
	    return;		// Nothing changed
	set_sg(pim_mre_sg);
    }
}

// Note: applies for (S,G), (S,G,rpt)
void
PimMre::remove_pim_mre_sg_rpt_entry()
{
    if (is_sg_rpt()) {
	if (is_task_delete_pending() && entry_can_remove()) {
	    //
	    // Remove the entry from the PimMrt, and mark it as deletion done
	    //
	    pim_mrt().remove_pim_mre(this);
	    set_is_task_delete_done(true);
	} else {
	    set_is_task_delete_pending(false);
	    set_is_task_delete_done(false);
	    return;
	}
    }
    
    if (is_sg()) {
	PimMre *pim_mre_sg_rpt = pim_mrt().pim_mre_find(source_addr(),
							group_addr(),
							PIM_MRE_SG_RPT,
							0);
	if (pim_mre_sg_rpt == sg_rpt_entry())
	    return;		// Nothing changed
	set_sg_rpt(pim_mre_sg_rpt);
    }
}
