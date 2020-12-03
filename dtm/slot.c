/* -*- C -*- */
/*
 * Copyright (c) 2020 Seagate Technology LLC and/or its Affiliates
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * For any questions about this software or licensing,
 * please email opensource@seagate.com or cortx-questions@seagate.com.
 *
 */

/**
 * @addtogroup dtm
 *
 * @{
 */

#include "lib/types.h"         /* m0_uint128 */
#include "lib/assert.h"        /* M0_IMPOSSIBLE */

#include "dtm/dtm_internal.h"  /* nu_dtm */
#include "dtm/catalogue.h" /* remote slot cat */
#include "dtm/slot.h"
#include "dtm/dtm.h" /* default slot */
#include "dtm/remote.h" /* remote id */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_DTM
#include "lib/trace.h"


enum {
	M0_DTM_HTYPE_SLOT     = 50,
	M0_DTM_HTYPE_SLOT_REM = 60,
};


static const struct m0_dtm_history_ops slot_ops;
static const struct m0_dtm_history_ops slot_remote_ops;

static struct m0_dtm_catalogue *slot_cat(struct m0_dtm *dtm)
{
	return &dtm->d_cat[M0_DTM_HTYPE_SLOT_REM];
}

M0_INTERNAL void m0_dtm_slot_init(struct m0_dtm_slot *slot,
				  struct m0_dtm *dtm)
{
	struct m0_dtm_history *history = &slot->sl_ch.ch_history;

	m0_dtm_controlh_init(&slot->sl_ch, dtm);
	history->h_hi.hi_flags |= M0_DHF_OWNED;
	history->h_ops = &slot_ops;
	history->h_rem = NULL;

	m0_dtm_catalogue_add(slot_cat(dtm), history);
}

M0_INTERNAL void m0_dtm_slot_fini(struct m0_dtm_slot *slot)
{
	struct m0_dtm_history *history = &slot->sl_ch.ch_history;

	m0_dtm_catalogue_del(slot_cat(HISTORY_DTM(history)), history);
	m0_dtm_controlh_fini(&slot->sl_ch);
}

M0_INTERNAL void m0_dtm_slot_add(struct m0_dtm_slot *slot,
				 struct m0_dtm_oper *oper)
{
	/* Assume: operation cannot have more than one update for a slot.
	 * Rationale: there is no place for two updates
	 * for owned slots in the simple model (DTM0 with replication).
	 * NOTE: This condition might be adjusted for sophisticated use-cases.
	 */
	M0_PRE(m0_tl_find(oper, update, &oper->oprt_op.op_ups,
			   UPDATE_HISTORY(update)->h_ops->hio_type ==
			   &m0_dtm_slot_htype) == NULL);
	m0_dtm_controlh_add(&slot->sl_ch, oper);
}

static int slot_find(struct m0_dtm *dtm, const struct m0_dtm_history_type *ht,
		     const struct m0_uint128 *id, struct m0_dtm_history **out)
{
	/* See the comment in slot_id */
	M0_PRE(m0_uint128_eq(id, &dtm->d_id));
	return m0_dtm_catalogue_lookup(slot_cat(dtm), id, out);
}

static const struct m0_dtm_history_type_ops slot_htype_ops = {
	.hito_find = &slot_find
};

M0_INTERNAL const struct m0_dtm_history_type m0_dtm_slot_htype = {
	.hit_id     = M0_DTM_HTYPE_SLOT,
	.hit_rem_id = M0_DTM_HTYPE_SLOT_REM,
	.hit_name   = "slot",
	.hit_ops    = &slot_htype_ops
};

static void slot_persistent(struct m0_dtm_history *history)
{
	/* noop */
}

static void slot_fixed(struct m0_dtm_history *history)
{
	/* Slot history cannot be closed therefore it cannot be fixed. */
	M0_IMPOSSIBLE("Slot cannot be fixed!");
}

static const struct m0_uint128 *slot_id(const struct m0_dtm_history *history)
{
	/* FIXME: only one slot per DTM instance so far */
	return &HISTORY_DTM(history)->d_id;
}

static const struct m0_dtm_history_ops slot_ops = {
	.hio_type       = &m0_dtm_slot_htype,
	.hio_id         = &slot_id,
	.hio_persistent = &slot_persistent,
	.hio_fixed      = &slot_fixed,
	.hio_update     = &m0_dtm_controlh_update
};

static struct m0_dtm_catalogue *rem_slot_cat(struct m0_dtm *dtm)
{
	return &dtm->d_cat[M0_DTM_HTYPE_SLOT_REM];
}

M0_INTERNAL void m0_dtm_slot_remote_init(struct m0_dtm_slot_remote *srem,
					 struct m0_dtm *dtm,
					 struct m0_dtm_remote *remote)
{
	struct m0_dtm_history *history = &srem->rs_ch.ch_history;

	m0_dtm_controlh_init(&srem->rs_ch, dtm);
	history->h_ops = &slot_remote_ops;
	history->h_hi.hi_flags = 0;
	/* FIXME:
	 * h_rem is not set to make remote slot get to the persistent state
	 * together with its FOL.
	 * However, if the remote DTM instance is unkown then it makes
	 * hard to understand the right history type id to be packed. The id
	 * needs to be repaced with the remote id when replying but it should
	 * not be replaced when the update is re-sent to the "sibling" nodes
	 * (servers).
	 * Once DTM operation packing is able properly put slot, FOL and RFOLs
	 * into on-wire descriptor without using h_rem or rs_rem, this
	 * code should be re-visited and rs_rem should be removed.
	 * It will play nice with the "more-than-one-slot-per-DTM-instance"
	 * feature -- slots can be dynamically allocated "on-demand" rather
	 * than explicitelly associated with DTM remotes.
	 * See also: oper_pack, reply_pack, R_PERSISTENT, REDO.
	 */
	history->h_rem = NULL;
	srem->rs_rem = remote;

	m0_dtm_catalogue_add(rem_slot_cat(dtm), history);
}

M0_INTERNAL void m0_dtm_slot_remote_fini(struct m0_dtm_slot_remote *srem)
{
	struct m0_dtm_history *history = &srem->rs_ch.ch_history;

	m0_dtm_catalogue_del(rem_slot_cat(HISTORY_DTM(history)), history);
	m0_dtm_controlh_fini(&srem->rs_ch);
}

static const struct m0_uint128 *
slot_remote_id(const struct m0_dtm_history *history)
{
	const struct m0_dtm_controlh *ch;
	const struct m0_dtm_slot_remote *srem;

	ch = M0_AMB(ch, history, ch_history);
	srem = M0_AMB(srem, ch, rs_ch);

	return &srem->rs_rem->re_id;
}

static int slot_remote_find(struct m0_dtm *dtm,
			    const struct m0_dtm_history_type *ht,
			    const struct m0_uint128 *id,
			    struct m0_dtm_history **out)
{
	return m0_dtm_catalogue_lookup(rem_slot_cat(dtm), id, out);
}

static const struct m0_dtm_history_type_ops slot_remote_htype_ops = {
	.hito_find = &slot_remote_find
};

M0_INTERNAL const struct m0_dtm_history_type m0_dtm_slot_remote_htype = {
	.hit_id     = M0_DTM_HTYPE_SLOT_REM,
	.hit_rem_id = M0_DTM_HTYPE_SLOT,
	.hit_name   = "remote slot",
	.hit_ops    = &slot_remote_htype_ops
};

static void slot_remote_persistent(struct m0_dtm_history *history)
{
	/* noop */
}

static void slot_remote_fixed(struct m0_dtm_history *history)
{
	M0_IMPOSSIBLE("A slot cannot be fixed!");
}

static const struct m0_dtm_history_ops slot_remote_ops = {
	.hio_type       = &m0_dtm_slot_remote_htype,
	.hio_id         = &slot_remote_id,
	.hio_persistent = &slot_remote_persistent,
	.hio_fixed      = &slot_remote_fixed,
	.hio_update     = &m0_dtm_controlh_update
};




/** @} end of dtm group */


#undef M0_TRACE_SUBSYSTEM
