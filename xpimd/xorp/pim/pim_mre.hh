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

// $XORP: xorp/pim/pim_mre.hh,v 1.51 2007/02/16 22:46:46 pavlin Exp $


#ifndef __PIM_PIM_MRE_HH__
#define __PIM_PIM_MRE_HH__


//
// PIM Multicast Routing Entry definitions.
//


#include "libxorp/timer.hh"
#include "mrt/mifset.hh"
#include "mrt/mrt.hh"
#include "pim_mrib_table.hh"
#include "pim_proto_assert.hh"


//
// Constants definitions
//


//
// Structures/classes, typedefs and macros
//

class AssertMetric;
class PimMre;
class PimMrt;
class PimNbr;
class PimRp;
class PimVif;


// PimMre _flags
// TODO: move it inside PimMre class??
enum {
    // Multicast Routing Entry type
    PIM_MRE_SG			= 1 << 0,	// (S,G) entry
    PIM_MRE_SG_RPT		= 1 << 1,	// (S,G,rpt) entry
    PIM_MRE_WC			= 1 << 2,	// (*,G) entry
    PIM_MRE_RP			= 1 << 3,	// (*,*,RP) entry
    // 
    PIM_MRE_SPT			= 1 << 4,	// (S,G) entry switched to SPT
    PIM_MFC			= 1 << 5,	// Multicast Forwarding Cache
						// entry: enumerated here for
						// consistency.
    // State machine
    PIM_MRE_JOINED_STATE	= 1 << 8,	// The state is Joined:
						//   (*,*,RP) (*,G) (S,G)
    PIM_MRE_PRUNED_STATE	= 1 << 9,	// The state is Pruned:
						//   (S,G,rpt)
    PIM_MRE_NOT_PRUNED_STATE	= 1 << 10,	// The state is NotPruned:
						//   (S,G,rpt)
    PIM_MRE_REGISTER_JOIN_STATE = 1 << 11,	// The Register tunnel for
						// (S,G) is in Join state
    PIM_MRE_REGISTER_PRUNE_STATE = 1 << 12,	// The Register tunnel for
						// (S,G) is in Prune state
    PIM_MRE_REGISTER_JOIN_PENDING_STATE = 1 << 13, // The Register tunnel for
						   // (S,G) is in Join-Pending
						   // state
    PIM_MRE_COULD_REGISTER_SG	= 1 << 14,   // The macro "CouldRegister(S,G)"
					     // is true
    // Misc.
    PIM_MRE_GRAFTED		= 1 << 16,   // For PIM-DM
    PIM_MRE_DIRECTLY_CONNECTED_S = 1 << 18,  // Directly-connected S
    PIM_MRE_I_AM_RP		 = 1 << 19,  // I am the RP for the group
    PIM_MRE_KEEPALIVE_TIMER_IS_SET = 1 << 20,// The (S,G) Keepalive Timer is
					     // running
    PIM_MRE_TASK_DELETE_PENDING	= 1 << 21,   // Entry is pending deletion
    PIM_MRE_TASK_DELETE_DONE	= 1 << 22,   // Entry is ready to be deleted
    PIM_MRE_SWITCH_TO_SPT_DESIRED = 1 << 23, // SwitchToSptDesired(S,G) is true
    PIM_MRE_IS_KAT_SET_TO_RP_KEEPALIVE_PERIOD = 1 << 24 // At the RP, when
						        // Register-Stop is sent
};


// PIM-specific Multicast Routing Entry
// XXX: the source_addr() for (*,*,RP) entry contains the RP address
class PimMre : public Mre<PimMre> {
public:
    PimMre(PimMrt& pim_mrt, const IPvX& source, const IPvX& group);
    ~PimMre();
    
    void	add_pim_mre_lists();
    void	remove_pim_mre_lists();
    
    // General info: PimNode, PimMrt, family, etc.
    PimNode&	pim_node()	const;
    PimMrt&	pim_mrt()	const	{ return (_pim_mrt);		}
    PimMrt&	_pim_mrt;		// The PIM MRT (yuck!)
    int		family()	const;
    uint32_t	pim_register_vif_index() const;
    
    //
    // Type of PimMre entry and related info
    //
    // The four entry types: (S,G), (S,G,rpt), (*,G), (*,*,RP)
    bool	is_sg()		const { return (_flags & PIM_MRE_SG);	}
    bool	is_sg_rpt()	const { return (_flags & PIM_MRE_SG_RPT); }
    bool	is_wc()		const { return (_flags & PIM_MRE_WC);	}
    bool	is_rp()		const { return (_flags & PIM_MRE_RP);	}
    // Entry characteristics
    // Note: applies only for (S,G)
    bool	is_spt()	const { return (_flags & PIM_MRE_SPT);	}
    void	set_sg(bool v);
    void	set_sg_rpt(bool v);
    void	set_wc(bool v);
    void	set_rp(bool v);
    // Note: applies only for (S,G)
    void	set_spt(bool v);
    
    const IPvX*	rp_addr_ptr() const;	// The RP address
    string	rp_addr_string() const;	// C++ string with the RP address
					// or "RP_ADDR_UNKNOWN"
    
    //
    // The RP entry
    //
    PimRp	*pim_rp()	const	{ return (_pim_rp);		}
    void	set_pim_rp(PimRp *v);	// Used by (*,G) (S,G) (S,G,rpt)
    void	uncond_set_pim_rp(PimRp *v); // Used by (*,G) (S,G) (S,G,rpt)
    PimRp	*compute_rp() const;	// Used by (*,G) (S,G) (S,G,rpt)
    void	recompute_rp_wc();	// Used by (*,G)
    void	recompute_rp_sg();	// Used by (S,G)
    void	recompute_rp_sg_rpt();	// Used by (S,G,rpt)
    //
    PimRp	*_pim_rp;		// The RP entry
					// Used by (*,G) (S,G) (S,G,rpt)
    
    //
    // MRIB info
    //
    // Note: mrib_s(), rpf_interface_s() and related *_s() methods
    // are used only by (S,G) and (S,G,rpt) entry.
    // Note: mrib_rp(), rpf_interface_rp(), set_mrib_rp() apply for all entries
    Mrib	*mrib_rp()	const	{ return (_mrib_rp);		}
    Mrib	*mrib_s()	const	{ return (_mrib_s);		}
    uint32_t	rpf_interface_rp() const;
    uint32_t	rpf_interface_s() const;
    void	set_mrib_rp(Mrib *v)	{ _mrib_rp = v;			}
    void	set_mrib_s(Mrib *v)	{ _mrib_s = v;			}
    Mrib	*compute_mrib_rp() const;
    Mrib	*compute_mrib_s() const;
    void	recompute_mrib_rp_rp();		// Used by (*,*,RP)
    void	recompute_mrib_rp_wc();		// Used by (*,G)
    void	recompute_mrib_rp_sg();		// Used by (S,G)
    void	recompute_mrib_rp_sg_rpt();	// Used by (S,G,rpt)
    void	recompute_mrib_s_sg();		// Used by (S,G)
    void	recompute_mrib_s_sg_rpt();	// Used by (S,G,rpt)
    //
    Mrib	*_mrib_rp;		// The MRIB info to the RP
					// Used by all entries
    Mrib	*_mrib_s;		// The MRIB info to the source
					// Used by (S,G) (S,G,rpt)
    
    //
    // RPF and RPF' neighbor info
    //
    // Note: applies only for (*,*,RP) and (*,G), but works also for (S,G)
    // and (S,G,rpt)
    PimNbr	*nbr_mrib_next_hop_rp() const;
    // Note: applies only for (S,G)
    PimNbr	*nbr_mrib_next_hop_s() const { return (_nbr_mrib_next_hop_s); }
    // Note: applies only for (*,G) ans (S,G,rpt) but works also for (S,G)
    PimNbr	*rpfp_nbr_wc()	const;
    // Note: applies only for (S,G)
    PimNbr	*rpfp_nbr_sg()	const	{ return (_rpfp_nbr_sg);	}
    // Note: applies only for (S,G,rpt)
    PimNbr	*rpfp_nbr_sg_rpt() const { return (_rpfp_nbr_sg_rpt);	}
    // Note: applies for all entries
    bool	is_pim_nbr_in_use(const PimNbr *pim_nbr) const;
    // Note: applies for all entries
    bool	is_pim_nbr_missing() const;
    // Note: applies only for (*,*,RP) and (*,G)
    void	set_nbr_mrib_next_hop_rp(PimNbr *v);
    // Note: applies only for (S,G)
    void	set_nbr_mrib_next_hop_s(PimNbr *v);
    // Note: applies only for (*,G)
    void	set_rpfp_nbr_wc(PimNbr *v);
    // Note: applies only for (S,G)
    void	set_rpfp_nbr_sg(PimNbr *v);
    // Note: applies only for (S,G,rpt)
    void	set_rpfp_nbr_sg_rpt(PimNbr *v);
    // Note: applies only for (*,*,RP), (*,G), (S,G,rpt), but works also
    // for (S,G).
    PimNbr	*compute_nbr_mrib_next_hop_rp() const;
    // Note: applies only for (S,G)
    PimNbr	*compute_nbr_mrib_next_hop_s() const;
    // Note: applies only for (*,G)
    PimNbr	*compute_rpfp_nbr_wc() const;
    // Note: applies only for (S,G)
    PimNbr	*compute_rpfp_nbr_sg() const;
    // Note: applies only for (S,G,rpt)
    PimNbr	*compute_rpfp_nbr_sg_rpt() const;
    // (*,*,RP)-related upstream changes
    void	recompute_nbr_mrib_next_hop_rp_rp_changed();
    void	recompute_nbr_mrib_next_hop_rp_gen_id_changed();
    // (*,G)-related upstream changes
    void	recompute_nbr_mrib_next_hop_rp_wc_changed();
    void	recompute_rpfp_nbr_wc_assert_changed();
    void	recompute_rpfp_nbr_wc_not_assert_changed();
    void	recompute_rpfp_nbr_wc_gen_id_changed();
    // (S,G)-related upstream changes
    void	recompute_nbr_mrib_next_hop_s_changed();
    void	recompute_rpfp_nbr_sg_assert_changed();
    void	recompute_rpfp_nbr_sg_not_assert_changed();
    void	recompute_rpfp_nbr_sg_gen_id_changed();
    // (S,G,rpt)-related upstream changes
    void	recompute_rpfp_nbr_sg_rpt_changed();
    // (S,G,rpt)-related upstream changes (recomputed via (S,G) to (S,G,rpt))
    void	recompute_rpfp_nbr_sg_rpt_sg_changed();
    // Misc. other RPF-related info
    // Note: applies for (S,G) and (S,G,rpt)
    bool	compute_is_directly_connected_s();
    // Note: applies for (S,G)
    void	recompute_is_directly_connected_sg();
    //
    PimNbr	*_nbr_mrib_next_hop_rp;	// Applies only for (*,*,RP) and (*,G)
    PimNbr	*_nbr_mrib_next_hop_s;	// Applies only for (S,G)
    PimNbr	*_rpfp_nbr_wc;		// Applies only for (*,G)
    PimNbr	*_rpfp_nbr_sg;		// Applies only for (S,G)
    PimNbr	*_rpfp_nbr_sg_rpt;	// Applies only for (S,G,rpt)
    
    //
    // Related entries: (*,G), (*,*,RP) (may be NULL).
    //
    PimMre	*wc_entry()	const	{ return (_wc_entry);		}
    PimMre	*rp_entry()	const	{
	if (_rp_entry != NULL)
	    return (_rp_entry);
	if (wc_entry() != NULL)
	    return (wc_entry()->rp_entry());	// XXX: get it through (*,G)
	return (NULL);
    }
    PimMre	*sg_entry() const {
	if (is_sg_rpt())
	    return (_sg_sg_rpt_entry);
	return (NULL);
    }
    PimMre	*sg_rpt_entry() const {
	if (is_sg())
	    return (_sg_sg_rpt_entry);
	return (NULL);
    }
    void	set_wc_entry(PimMre *v)	{ _wc_entry = v;		}
    void	set_rp_entry(PimMre *v)	{ _rp_entry = v;		}
    void	set_sg_entry(PimMre *v) { _sg_sg_rpt_entry = v;		}
    void	set_sg_rpt_entry(PimMre *v) { _sg_sg_rpt_entry = v;	}
    //
    PimMre	*_wc_entry;		// The (*,G) entry
    PimMre	*_rp_entry;		// The (*,*,RP) entry
    PimMre	*_sg_sg_rpt_entry;	// The (S,G) or (S,G,rpt) entry
    
    //
    // ASSERT-related route metric and metric preference
    //
    // Note: applies only for (S,G) and (S,G,rpt)
    uint32_t	metric_preference_s() const;
    // Note: applies for all entries
    uint32_t	metric_preference_rp() const;
    // Note: applies only for (S,G) and (S,G,rpt)
    uint32_t	metric_s() const;
    // Note: applies for all entries
    uint32_t	metric_rp() const;
    
    //
    // Local receivers info
    //
    // Note: applies only for (*,G), (S,G) and (S,G,rpt)
    const Mifset& local_receiver_include_wc() const;
    // Note: applies only for (S,G)
    const Mifset& local_receiver_include_sg() const;
    // Note: applies only for (S,G)
    const Mifset& local_receiver_exclude_sg() const;
    // Note: applies only for (*,G) and (S,G); has only internal PimMre meaning
    const Mifset& local_receiver_include() const {
	return (_local_receiver_include);
    }
    const Mifset& local_receiver_exclude() const {
	return (_local_receiver_exclude);
    }
    void	set_local_receiver_include(uint32_t vif_index, bool v);
    void	set_local_receiver_exclude(uint32_t vif_index, bool t);
    Mifset	_local_receiver_include; // The interfaces with IGMP/MLD6 Join
    Mifset	_local_receiver_exclude; // The interfaces with IGMP/MLD6 Leave

    //
    // JOIN/PRUNE info
    //
    // Note: applies only for (*,*,RP), (*,G), (S,G)
    XorpTimer&	join_timer() { return (_join_or_override_timer); }
    // Note: applies only for (*,*,RP), (*,G), (S,G)
    const XorpTimer& const_join_timer() const {
	return (_join_or_override_timer);
    }
    void	join_timer_timeout();
    // Note: applies only for (S,G,rpt)
    XorpTimer&	override_timer() { return (_join_or_override_timer); }
    // Note: applies only for (S,G,rpt)
    const XorpTimer& const_override_timer() const {
	return (_join_or_override_timer);
    }
    void	override_timer_timeout();
    XorpTimer	_join_or_override_timer; // The Join Timer for
					 // (*,*,RP) (*,G) (S,G);
					 // Also Override Timer for (S,G,rpt)
    // Note: applies only for (*,*,RP)
    void	receive_join_rp(uint32_t vif_index, uint16_t holdtime);
    // Note: applies only for (*,*,RP)
    void	receive_prune_rp(uint32_t vif_index, uint16_t holdtime);
    // Note: applies only for (*,G)
    void	receive_join_wc(uint32_t vif_index, uint16_t holdtime);
    // Note: applies only for (*,G)
    void	receive_prune_wc(uint32_t vif_index, uint16_t holdtime);
    // Note: applies only for (S,G)
    void	receive_join_sg(uint32_t vif_index, uint16_t holdtime);
    // Note: applies only for (S,G)
    void	receive_prune_sg(uint32_t vif_index, uint16_t holdtime);
    // Note: applies only for (S,G,rpt)
    void	receive_join_wc_by_sg_rpt(uint32_t vif_index);
    // Note: applies only for (S,G,rpt)
    void	receive_join_sg_rpt(uint32_t vif_index, uint16_t holdtime);
    // Note: applies only for (S,G,rpt)
    void	receive_prune_sg_rpt(uint32_t vif_index, uint16_t holdtime,
				     bool is_join_wc_received);
    // Note: applies only for (S,G,rpt)
    void	receive_end_of_message_sg_rpt(uint32_t vif_index);
    // Note: applies only for (*,*,RP)
    void	rp_see_join_rp(uint32_t vif_index, uint16_t holdtime,
			       const IPvX& target_nbr_addr);
    // Note: applies only for (*,*,RP)
    void	rp_see_prune_rp(uint32_t vif_index, uint16_t holdtime,
				const IPvX& target_nbr_addr);
    // Note: applies only for (*,G)
    void	wc_see_join_wc(uint32_t vif_index, uint16_t holdtime,
			       const IPvX& target_nbr_addr);
    // Note: applies only for (*,G)
    void	wc_see_prune_wc(uint32_t vif_index, uint16_t holdtime,
				const IPvX& target_nbr_addr);
    // Note: applies only for (S,G)
    void	sg_see_join_sg(uint32_t vif_index, uint16_t holdtime,
			       const IPvX& target_nbr_addr);
    // Note: applies only for (S,G)
    void	sg_see_prune_sg(uint32_t vif_index, uint16_t holdtime,
				const IPvX& target_nbr_addr);
    // Note: applies only for (S,G)
    void	sg_see_prune_wc(uint32_t vif_index,
				const IPvX& target_nbr_addr);
    // Note: applies only for (S,G)
    void	sg_see_prune_sg_rpt(uint32_t vif_index, uint16_t holdtime,
				    const IPvX& target_nbr_addr);
    // Note: applies only for (S,G,rpt)
    void	sg_rpt_see_join_sg_rpt(uint32_t vif_index, uint16_t holdtime,
				       const IPvX& target_nbr_addr);
    // Note: applies only for (S,G,rpt)
    void	sg_rpt_see_prune_sg_rpt(uint32_t vif_index, uint16_t holdtime,
					const IPvX& target_nbr_addr);
    // Note: applies only for (S,G,rpt)
    void	sg_rpt_see_prune_sg(uint32_t vif_index, uint16_t holdtime,
				    const IPvX& target_nbr_addr);
    // Note: applies only for (*,*,RP), (*,G), (S,G), (S,G,rpt)
    bool	is_join_desired_rp() const;
    // Note: applies only for (*,G), (S,G), (S,G,rpt)
    bool	is_join_desired_wc() const;
    // Note: applies only for (S,G)
    bool	is_join_desired_sg() const;
    // Note: applies only for (*,G), (S,G), (S,G,rpt)
    bool	is_rpt_join_desired_g() const;
    // Note: applies only for (S,G,rpt)
    bool	is_prune_desired_sg_rpt() const;
    // Note: applies only for (*,*,RP)
    bool	recompute_is_join_desired_rp();
    // Note: applies only for (*,G)
    bool	recompute_is_join_desired_wc();
    // Note: applies only for (S,G)
    bool	recompute_is_join_desired_sg();
    // Note: applies only for (S,G,rpt)
    bool	recompute_is_prune_desired_sg_rpt();
    // Note: applies only for (S,G) (recomputed via (S,G) to (S,G,rpt))
    bool	recompute_is_prune_desired_sg_rpt_sg();
    // Note: applies only for (S,G,rpt)
    bool	recompute_is_rpt_join_desired_g();

    // Note: applies only for (*,*,RP), (*,G), (S,G), (S,G,rpt)
    const Mifset& joins_rp() const;
    // Note: works for (*,G), (S,G), (S,G,rpt)
    const Mifset& joins_wc() const;
    // Note: applies only for (S,G)
    const Mifset& joins_sg() const;
    // Note: applies only for (S,G,rpt)
    const Mifset& prunes_sg_rpt() const;
    
    //
    // J/P (downstream) state (per interface)
    //
    // Note: each method below applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
    // (except for the *_tmp_* and *_processed_wc_by_sg_rpt*
    // methods which apply only for (S,G,rpt))
    void	set_downstream_noinfo_state(uint32_t vif_index);
    void	set_downstream_join_state(uint32_t vif_index);
    void	set_downstream_prune_state(uint32_t vif_index);
    void	set_downstream_prune_pending_state(uint32_t vif_index);
    void	set_downstream_prune_tmp_state(uint32_t vif_index);
    void	set_downstream_prune_pending_tmp_state(uint32_t vif_index);
    void	set_downstream_processed_wc_by_sg_rpt(uint32_t vif_index,
						      bool v);
    bool	is_downstream_noinfo_state(uint32_t vif_index) const;
    bool	is_downstream_join_state(uint32_t vif_index) const;
    bool	is_downstream_prune_state(uint32_t vif_index) const;
    bool	is_downstream_prune_pending_state(uint32_t vif_index) const;
    bool	is_downstream_prune_tmp_state(uint32_t vif_index) const;
    bool	is_downstream_prune_pending_tmp_state(uint32_t vif_index) const;
    bool	is_downstream_processed_wc_by_sg_rpt(uint32_t vif_index) const;
    const Mifset& downstream_join_state() const;
    const Mifset& downstream_prune_state() const;
    const Mifset& downstream_prune_pending_state() const;
    const Mifset& downstream_prune_tmp_state() const;
    const Mifset& downstream_prune_pending_tmp_state() const;
    Mifset	_downstream_join_state;			// Join state
    Mifset	_downstream_prune_pending_state;	// Prune-Pending state
    Mifset	_downstream_prune_state;		// Prune state
    Mifset	_downstream_tmp_state;			// P' and PP' state
    Mifset	_downstream_processed_wc_by_sg_rpt; // (S,G,rpt)J/P processed
    
    // Note: applies only for (*,*,RP)
    void	downstream_expiry_timer_timeout_rp(uint32_t vif_index);
    // Note: applies only for (*,G)
    void	downstream_expiry_timer_timeout_wc(uint32_t vif_index);
    // Note: applies only for (S,G)
    void	downstream_expiry_timer_timeout_sg(uint32_t vif_index);
    // Note: applies only for (S,G,rpt)
    void	downstream_expiry_timer_timeout_sg_rpt(uint32_t vif_index);
    // Note: applies only for (*,*,RP)
    void	downstream_prune_pending_timer_timeout_rp(uint32_t vif_index);
    // Note: applies only for (*,G)
    void	downstream_prune_pending_timer_timeout_wc(uint32_t vif_index);
    // Note: applies only for (S,G)
    void	downstream_prune_pending_timer_timeout_sg(uint32_t vif_index);
    // Note: applies only for (S,G,rpt)
    void	downstream_prune_pending_timer_timeout_sg_rpt(uint32_t vif_index);
    XorpTimer	_downstream_expiry_timers[MAX_VIFS];	// Expiry timers
    XorpTimer	_downstream_prune_pending_timers[MAX_VIFS]; // Prune-Pending timers
    
    
    //
    // J/P upstream state for (*,*,RP), (*,G), (S,G)
    //
    bool	is_joined_state() const {
	return (_flags & PIM_MRE_JOINED_STATE);
    }
    bool	is_not_joined_state() const { return (!is_joined_state()); }
    void	set_joined_state();
    void	set_not_joined_state();
    //
    // J/P upstream state for (S,G,rpt)
    //
    bool	is_rpt_not_joined_state() const;
    bool	is_pruned_state() const;
    bool	is_not_pruned_state() const;
    void	set_rpt_not_joined_state();
    void	set_pruned_state();
    void	set_not_pruned_state();
    //
    // J/P state recomputation
    //
    //
    // Note: works for all entries
    const Mifset& immediate_olist_rp() const;
    // Note: applies for (*,G), (S,G), (S,G,rpt)
    const Mifset& immediate_olist_wc() const;
    // Note: applies for (S,G)
    const Mifset& immediate_olist_sg() const;
    // Note: applies for (*,G), (S,G), (S,G,rpt)
    const Mifset& pim_include_wc() const;
    // Note: applies for (S,G)
    const Mifset& pim_include_sg() const;
    // Note: applies for (S,G)
    const Mifset& pim_exclude_sg() const;
    // Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
    const Mifset& inherited_olist_sg() const;
    // Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
    const Mifset& inherited_olist_sg_rpt() const;
    // Note: applies for (S,G,rpt)
    bool	recompute_inherited_olist_sg_rpt();
    
    
    //
    // REGISTER info
    //
    // Note: applies for (S,G)
    void	receive_register_stop();
    // Perform the "RP changed" action at the (S,G) register state machine
    // Note that the RP has already changed and assigned by the method that
    // calls this one, hence we unconditionally take the "RP changed" actions.
    // Note: applies for (S,G)
    void	rp_register_sg_changed();
    // Note: applies for (S,G)
    void	set_register_noinfo_state();
    // Note: applies for (S,G)
    void	set_register_join_state();
    // Note: applies for (S,G)
    void	set_register_prune_state();
    // Note: applies for (S,G)
    void	set_register_join_pending_state();
    // Note: applies for (S,G)
    bool	is_register_noinfo_state() const;
    // Note: applies for (S,G)
    bool	is_register_join_state() const;
    // Note: applies for (S,G)
    bool	is_register_prune_state() const;
    // Note: applies for (S,G)
    bool	is_register_join_pending_state() const;
    // Note: applies for (S,G)
    bool	compute_is_could_register_sg() const;
    // Note: applies for (S,G)
    bool	recompute_is_could_register_sg();
    // Note: applies for (S,G)
    void	add_register_tunnel();
    // Note: applies for (S,G)
    void	remove_register_tunnel();
    // Note: applies for (S,G)
    void	update_register_tunnel();
    // Note: the remaining Register-related methods below should apply
    // only for (S,G), but for simplicity we we don't check the entry type.
    bool	is_could_register_sg() const {
	return (_flags & PIM_MRE_COULD_REGISTER_SG);
    }
    bool	is_not_could_register_sg() const {
	return (! is_could_register_sg());
    }
    void	set_could_register_sg() {
	_flags |= PIM_MRE_COULD_REGISTER_SG;
    }
    void	set_not_could_register_sg() {
	_flags &= ~PIM_MRE_COULD_REGISTER_SG;
    }
    XorpTimer&	register_stop_timer() { return (_register_stop_timer); }
    void	register_stop_timer_timeout();
    XorpTimer	_register_stop_timer;
    
    
    //
    // ASSERT info
    //
    //  Note: applies only for (*,G) and (S,G)
    bool	is_assert_noinfo_state(uint32_t vif_index) const;
    //  Note: applies only for (*,G) and (S,G)
    bool	is_i_am_assert_winner_state(uint32_t vif_index) const;
    //  Note: applies only for (*,G) and (S,G)
    bool	is_i_am_assert_loser_state(uint32_t vif_index) const;
    //  Note: applies only for (*,G) and (S,G)
    void	set_assert_noinfo_state(uint32_t vif_index);
    //  Note: applies only for (*,G) and (S,G)
    void	set_i_am_assert_winner_state(uint32_t vif_index);
    //  Note: applies only for (*,G) and (S,G)
    void	set_i_am_assert_loser_state(uint32_t vif_index);
    // Note: applies only for (*,G) and (S,G)
    const Mifset& i_am_assert_winner_state() const {
	return (_i_am_assert_winner_state);
    }
    // Note: applies only for (*,G) and (S,G)
    const Mifset& i_am_assert_loser_state() const {
	return (_i_am_assert_loser_state);
    }
    Mifset	_i_am_assert_winner_state; // The interfaces I am Assert winner
    Mifset	_i_am_assert_loser_state;  // The interfaces I am Assert loser
    
    // Note: works for (*,G), (S,G), (S,G,rpt)
    const Mifset& i_am_assert_winner_wc() const;
    // Note: works only for (S,G)
    const Mifset& i_am_assert_winner_sg() const;
    // Note: applies for (*,G), (S,G), (S,G,rpt)
    const Mifset& i_am_assert_loser_wc() const;
    // Note: applies only for (S,G)
    const Mifset& i_am_assert_loser_sg() const;
    // Note: applies for (*,G), (S,G), (S,G,rpt)
    const Mifset& lost_assert_wc() const;
    // Note: applies only for (S,G)
    const Mifset& lost_assert_sg() const;
    // Note: applies only for (S,G) and (S,G,rpt)
    const Mifset& lost_assert_sg_rpt() const;
    
    XorpTimer	_assert_timers[MAX_VIFS];  // The Assert (winner/loser) timers
    // Note: applies only for (*,G)
    void	assert_timer_timeout_wc(uint32_t vif_index);
    // Note: applies only for (S,G)
    void	assert_timer_timeout_sg(uint32_t vif_index);
    // Note: works for (*,G), (S,G)
    AssertMetric *assert_winner_metric_wc(uint32_t vif_index) const;
    // Note: works for (S,G)
    AssertMetric *assert_winner_metric_sg(uint32_t vif_index) const;
    // Note: applies only for (*,G) and (S,G)
    AssertMetric *assert_winner_metric(uint32_t vif_index) const {
	return (_assert_winner_metrics[vif_index]);
    }
    // Note: works for (*,G), (S,G)
    void	set_assert_winner_metric_wc(uint32_t vif_index, AssertMetric *v);
    // Note: works for (S,G)
    void	set_assert_winner_metric_sg(uint32_t vif_index, AssertMetric *v);
    // Note: applies only for (*,G) and (S,G)
    void	set_assert_winner_metric(uint32_t vif_index, AssertMetric *v);
    // Note: works for (*,G), (S,G)
    void	delete_assert_winner_metric_wc(uint32_t vif_index);
    // Note: works for (S,G)
    void	delete_assert_winner_metric_sg(uint32_t vif_index);
    // Note: applies only for (*,G) and (S,G)
    void	delete_assert_winner_metric(uint32_t vif_index);
    // Note: applies only for (S,G)
    const Mifset& assert_winner_metric_is_better_than_spt_assert_metric_sg() const {
	return (_assert_winner_metric_is_better_than_spt_assert_metric_sg);
    }
    // Note: applies only for (S,G)
    void	set_assert_winner_metric_is_better_than_spt_assert_metric_sg(uint32_t vif_index, bool v);
    Mifset	_assert_winner_metric_is_better_than_spt_assert_metric_sg;
    
    // Note: applies for (*,G)
    const Mifset& assert_tracking_desired_wc() const;
    // Note: applies for (S,G)
    const Mifset& assert_tracking_desired_sg() const;
    // Note: applies only for (*,G) and (S,G)
    const Mifset& assert_tracking_desired_state() const {
	return (_assert_tracking_desired_state);
    }
    // Note: applies only for (*,G) and (S,G)
    void	set_assert_tracking_desired_state(uint32_t vif_index, bool v);
    // Note: applies only for (*,G) and (S,G)
    bool	is_assert_tracking_desired_state(uint32_t vif_index) const;
    Mifset	_assert_tracking_desired_state;	// To store the
						// AssertTrackingDesired state
    
    // Note: applies only for (*,G) and (S,G)
    const Mifset& could_assert_state() const { return (_could_assert_state); }
    // Note: applies only for (*,G) and (S,G)
    bool	is_could_assert_state(uint32_t vif_index) const;
    // Note: applies only for (*,G) and (S,G)
    void	set_could_assert_state(uint32_t vif_index, bool v);
    Mifset	_could_assert_state;	// To store the CouldAssert state
    
    // Note: applies only for (S,G)
    AssertMetric *my_assert_metric_sg(uint32_t vif_index) const;
    // Note: applies only for (S,G)
    AssertMetric *my_assert_metric_wc(uint32_t vif_index) const;
    // Note: applies only for (S,G)
    AssertMetric *spt_assert_metric(uint32_t vif_index) const;
    // Note: applies only for (*,G) and (S,G)
    AssertMetric *rpt_assert_metric(uint32_t vif_index) const;
    // Note: applies only for (*,G) and (S,G) (but is used only for (S,G))
    AssertMetric *infinite_assert_metric() const;
    AssertMetric *_assert_winner_metrics[MAX_VIFS]; // The Assert winner
						    // metrics array.
    // Note: applies only for (*,G) and (S,G)
    int		assert_process(PimVif *pim_vif, AssertMetric *assert_metric);
    // Note: applies only for (S,G)
    int		assert_process_sg(PimVif *pim_vif,
				  AssertMetric *assert_metric,
				  assert_state_t assert_state,
				  bool i_am_assert_winner);
    // Note: applies only for (*,G)
    int		assert_process_wc(PimVif *pim_vif,
				  AssertMetric *assert_metric,
				  assert_state_t state,
				  bool i_am_assert_winner);
    // Note: applies for all entries
    int		data_arrived_could_assert(PimVif *pim_vif,
					  const IPvX& src,
					  const IPvX& dst,
					  bool& is_assert_sent);
    // Note: applies only for (S,G)
    int		data_arrived_could_assert_sg(PimVif *pim_vif,
					     const IPvX& assert_source_addr,
					     bool& is_assert_sent);
    // Note: applies only for (*,G)
    int		data_arrived_could_assert_wc(PimVif *pim_vif,
					     const IPvX& assert_source_addr,
					     bool& is_assert_sent);
    // Note: applies only for (S,G)
    int		wrong_iif_data_arrived_sg(PimVif *pim_vif,
					  const IPvX& assert_source_addr,
					  bool& is_assert_sent);
    // Note: applies only for (*,G)
    int		wrong_iif_data_arrived_wc(PimVif *pim_vif,
					  const IPvX& assert_source_addr,
					  bool& is_assert_sent);
    // Note: applies only for (S,G)
    bool	recompute_assert_tracking_desired_sg();
    // Note: applies only for (S,G)
    bool	process_assert_tracking_desired_sg(uint32_t vif_index,
						   bool new_value);
    // Note: applies only for (*,G)
    bool	recompute_assert_tracking_desired_wc();
    // Note: applies only for (*,G)
    bool	process_assert_tracking_desired_wc(uint32_t vif_index,
						   bool new_value);
    // Note: applies only for (S,G) and (S,G,rpt)
    const Mifset& could_assert_sg() const;
    // Note: applies only for (S,G)
    bool	recompute_could_assert_sg();
    // Note: applies only for (S,G)
    bool	process_could_assert_sg(uint32_t vif_index, bool new_value);
    // Note: applies for all entries
    const Mifset& could_assert_wc() const;
    // Note: applies only for (*,G)
    bool	recompute_could_assert_wc();
    // Note: applies only for (*,G)
    bool	process_could_assert_wc(uint32_t vif_index, bool new_value);
    // Note: applies only for (S,G)
    bool	recompute_my_assert_metric_sg(uint32_t vif_index);
    // Note: applies only for (*,G)
    bool	recompute_my_assert_metric_wc(uint32_t vif_index);
    // Note: applies only for (S,G)
    bool	recompute_assert_rpf_interface_sg(uint32_t vif_index);
    // Note: applies only for (*,G)
    bool	recompute_assert_rpf_interface_wc(uint32_t vif_index);
    // Note: applies only for (S,G)
    bool	recompute_assert_receive_join_sg(uint32_t vif_index);
    // Note: applies only for (*,G)
    bool	recompute_assert_receive_join_wc(uint32_t vif_index);
    // Note: applies only for (S,G)
    bool	recompute_assert_winner_nbr_sg_gen_id_changed(
	uint32_t vif_index,
	const IPvX& nbr_addr);
    // Note: applies only for (*,G)
    bool	recompute_assert_winner_nbr_wc_gen_id_changed(
	uint32_t vif_index,
	const IPvX& nbr_addr);
    // Note: applies only for (S,G)
    bool	recompute_assert_winner_nbr_sg_nlt_expired(
	uint32_t vif_index,
	const IPvX& nbr_addr);
    // Note: applies only for (*,G)
    bool	recompute_assert_winner_nbr_wc_nlt_expired(
	uint32_t vif_index,
	const IPvX& nbr_addr);
    
    // Assert rate-limiting stuff
    void	asserts_rate_limit_timer_timeout();
    Mifset	_asserts_rate_limit;	// Bit-flags for Asserts rate limit
    XorpTimer	_asserts_rate_limit_timer;	// Timer for Asserts rate limit
						// support

    //
    // PMBR info
    //
    // PMBR: the first PMBR to send a Register for this source
    // with the Border bit set.
    // Note: applies only for (S,G)
    const IPvX&	pmbr_addr() const { return _pmbr_addr; }
    void	set_pmbr_addr(const IPvX& v) { _pmbr_addr = v; }
    void	clear_pmbr_addr() { _pmbr_addr = IPvX::ZERO(family()); }
    bool	is_pmbr_addr_set() const { return (_pmbr_addr != IPvX::ZERO(family())); }
    IPvX	_pmbr_addr;		// The address of the PMBR

    //
    // MISC. info
    //
    // Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
    const Mifset& i_am_dr() const;
    
    
    //
    // Data
    //
    // Note: applies only for (S,G)
    void	update_sptbit_sg(uint32_t iif_vif_index);
    // Note: applies for (*,G), (S,G), (S,G,rpt)
    bool	is_monitoring_switch_to_spt_desired_sg(const PimMre *pim_mre_sg) const;
    // Note: applies for all entries
    bool	is_switch_to_spt_desired_sg(uint32_t measured_interval_sec,
					    uint32_t measured_bytes) const;
    // Note: in theory applies for all entries, but in practice it could
    // be true only for (*,G), (S,G), (S,G,rpt)
    bool	check_switch_to_spt_sg(const IPvX& src, const IPvX& dst,
				       PimMre*& pim_mre_sg,
				       uint32_t measured_interval_sec,
				       uint32_t measured_bytes);
    // Note: applies only for (S,G)
    void	set_switch_to_spt_desired_sg(bool v);
    // Note: applies only for (S,G)
    bool	was_switch_to_spt_desired_sg() const;
    
    
    //
    // MISC. timers
    //
    // The KeepaliveTimer(S,G)
    // Note: applies only for (S,G)
    void	start_keepalive_timer();
    // Note: applies only for (S,G)
    void	cancel_keepalive_timer();
    // Note: applies only for (S,G)
    bool	is_keepalive_timer_running() const;
    // Note: applies only for (S,G)
    void	keepalive_timer_timeout();
    // Note: applies only for (S,G)
    void	recompute_set_keepalive_timer_sg();
    
    //
    // MISC. other stuff
    //
    
    // Note: applies for (*,*,RP)
    void	recompute_start_vif_rp(uint32_t vif_index);
    // Note: applies for (*,G)
    void	recompute_start_vif_wc(uint32_t vif_index);
    // Note: applies for (S,G)
    void	recompute_start_vif_sg(uint32_t vif_index);
    // Note: applies for (S,G,rpt)
    void	recompute_start_vif_sg_rpt(uint32_t vif_index);
    // Note: applies for (*,*,RP)
    void	recompute_stop_vif_rp(uint32_t vif_index);
    // Note: applies for (*,G)
    void	recompute_stop_vif_wc(uint32_t vif_index);
    // Note: applies for (S,G)
    void	recompute_stop_vif_sg(uint32_t vif_index);
    // Note: applies for (S,G,rpt)
    void	recompute_stop_vif_sg_rpt(uint32_t vif_index);
    
    // Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
    bool	entry_try_remove();    // Try to remove the entry if not needed
    // Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
    bool	entry_can_remove() const; // Test if OK to remove the entry

    // Actions to take when a related PimMre entry is added or removed
    // Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
    void	add_pim_mre_rp_entry();
    // Note: applies for (*,G), (S,G), (S,G,rpt)
    void	add_pim_mre_wc_entry();
    // Note: applies for (S,G), (S,G,rpt)
    void	add_pim_mre_sg_entry();
    // Note: applies for (S,G), (S,G,rpt)
    void	add_pim_mre_sg_rpt_entry();
    // Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
    void	remove_pim_mre_rp_entry();
    // Note: applies for (*,G), (S,G), (S,G,rpt)
    void	remove_pim_mre_wc_entry();
    // Note: applies for (S,G), (S,G,rpt)
    void	remove_pim_mre_sg_entry();
    // Note: applies for (S,G), (S,G,rpt)
    void	remove_pim_mre_sg_rpt_entry();
    
    // Note: applies for (S,G)
    bool	is_directly_connected_s() const {
	return (_flags & PIM_MRE_DIRECTLY_CONNECTED_S);
    }
    // Note: applies for (S,G)
    void	set_directly_connected_s(bool v) {
	if (v)
	    _flags |= PIM_MRE_DIRECTLY_CONNECTED_S;
	else
	    _flags &= ~PIM_MRE_DIRECTLY_CONNECTED_S;    
    }
    
    // Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
    bool	i_am_rp() const	{ return (_flags & PIM_MRE_I_AM_RP); }
    // Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
    void	set_i_am_rp(bool v) {
	if (v) {
	    _flags |= PIM_MRE_I_AM_RP;
	} else {
	    //
	    // XXX: Reset the PIM_MRE_IS_KAT_SET_TO_RP_KEEPALIVE_PERIOD flag
	    // as well, because it applies only at the RP.
	    //
	    _flags &= ~(PIM_MRE_I_AM_RP | PIM_MRE_IS_KAT_SET_TO_RP_KEEPALIVE_PERIOD);
	}
    }

    // Note: applies for (S,G)
    bool	is_kat_set_to_rp_keepalive_period() const { return (_flags & PIM_MRE_IS_KAT_SET_TO_RP_KEEPALIVE_PERIOD); }
    void	set_is_kat_set_to_rp_keepalive_period(bool v) {
	if (v)
	    _flags |= PIM_MRE_IS_KAT_SET_TO_RP_KEEPALIVE_PERIOD;
	else
	    _flags &= ~PIM_MRE_IS_KAT_SET_TO_RP_KEEPALIVE_PERIOD;
    }
    
    // Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
    bool	is_task_delete_pending() const { return (_flags & PIM_MRE_TASK_DELETE_PENDING); }
    // Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
    void	set_is_task_delete_pending(bool v) {
	if (v)
	    _flags |= PIM_MRE_TASK_DELETE_PENDING;
	else
	    _flags &= ~PIM_MRE_TASK_DELETE_PENDING;
    }
    // Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
    bool	is_task_delete_done() const { return (_flags & PIM_MRE_TASK_DELETE_DONE); }
    // Note: applies for (*,*,RP), (*,G), (S,G), (S,G,rpt)
    void	set_is_task_delete_done(bool v) {
	if (v)
	    _flags |= PIM_MRE_TASK_DELETE_DONE;
	else
	    _flags &= ~PIM_MRE_TASK_DELETE_DONE;
    }
    
private:
    uint32_t	_flags;			// Various flags (see PIM_MRE_* above)
};

//
// Global variables
//

//
// Global functions prototypes
//

#endif // __PIM_PIM_MRE_HH__
