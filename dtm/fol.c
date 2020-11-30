/* -*- C -*- */
/*
 * Copyright (c) 2013-2020 Seagate Technology LLC and/or its Affiliates
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
#include "lib/errno.h"         /* EPROTO */

#include "dtm/dtm_internal.h"  /* nu_dtm */
#include "dtm/catalogue.h"
#include "dtm/remote.h"
#include "dtm/dtm.h"
#include "dtm/fol.h"

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_DTM
#include "lib/trace.h"

static const struct m0_dtm_history_ops fol_ops;
static const struct m0_dtm_history_ops fol_remote_ops;

M0_INTERNAL void m0_dtm_fol_init(struct m0_dtm_fol *fol, struct m0_dtm *dtm)
{
	struct m0_dtm_history *history = &fol->fo_ch.ch_history;

	m0_dtm_controlh_init(&fol->fo_ch, dtm);
	history->h_hi.hi_flags |= M0_DHF_OWNED;
	history->h_ops = &fol_ops;
	history->h_rem = NULL;
}

M0_INTERNAL void m0_dtm_fol_fini(struct m0_dtm_fol *fol)
{
	m0_dtm_controlh_fini(&fol->fo_ch);
}

M0_INTERNAL void m0_dtm_fol_add(struct m0_dtm_fol *fol,
				struct m0_dtm_oper *oper)
{
	m0_dtm_controlh_add(&fol->fo_ch, oper);
}

static const struct m0_uint128 *fol_id(const struct m0_dtm_history *history)
{
	return &HISTORY_DTM(history)->d_id;
}

enum {
	M0_DTM_HTYPE_FOL      = 5,
	M0_DTM_HTYPE_FOL_REM  = 6,
	M0_DTM_HTYPE_SLOT     = 50,
	M0_DTM_HTYPE_SLOT_REM = 60,
};

static struct m0_dtm_catalogue *rem_fol_cat(struct m0_dtm *dtm)
{
	return &dtm->d_cat[M0_DTM_HTYPE_FOL_REM];
}

static void fol_persistent(struct m0_dtm_history *history)
{
	struct m0_dtm_catalogue *cat = rem_fol_cat(HISTORY_DTM(history));
	struct m0_dtm_history   *scan;

	m0_tl_for(cat, &cat->ca_el, scan) {
		scan->h_rem->re_ops->reo_persistent(scan->h_rem, history);
	} m0_tl_endfor;
}

static void fol_fixed(struct m0_dtm_history *history)
{
	M0_IMPOSSIBLE("Fixing fol?");
}

static int fol_find(struct m0_dtm *dtm, const struct m0_dtm_history_type *ht,
		    const struct m0_uint128 *id, struct m0_dtm_history **out)
{
	if (m0_uint128_eq(id, &dtm->d_id)) {
		*out = &dtm->d_fol.fo_ch.ch_history;
		return 0;
	} else
		return M0_ERR(-EPROTO);
}

static const struct m0_dtm_history_type_ops fol_htype_ops = {
	.hito_find = &fol_find
};

M0_INTERNAL const struct m0_dtm_history_type m0_dtm_fol_htype = {
	.hit_id     = M0_DTM_HTYPE_FOL,
	.hit_rem_id = M0_DTM_HTYPE_FOL_REM,
	.hit_name   = "fol",
	.hit_ops    = &fol_htype_ops
};

static const struct m0_dtm_history_ops fol_ops = {
	.hio_type       = &m0_dtm_fol_htype,
	.hio_id         = &fol_id,
	.hio_persistent = &fol_persistent,
	.hio_fixed      = &fol_fixed,
	.hio_update     = &m0_dtm_controlh_update
};

M0_INTERNAL void m0_dtm_fol_remote_init(struct m0_dtm_fol_remote *frem,
					struct m0_dtm *dtm,
					struct m0_dtm_remote *remote)
{
	struct m0_dtm_history *history = &frem->rfo_ch.ch_history;

	m0_dtm_controlh_init(&frem->rfo_ch, dtm);
	history->h_ops = &fol_remote_ops;
	history->h_rem = remote;
	m0_dtm_catalogue_add(rem_fol_cat(dtm), history);
}

M0_INTERNAL void m0_dtm_fol_remote_fini(struct m0_dtm_fol_remote *frem)
{
	struct m0_dtm_history *history = &frem->rfo_ch.ch_history;

	m0_dtm_catalogue_del(rem_fol_cat(HISTORY_DTM(history)), history);
	m0_dtm_controlh_fini(&frem->rfo_ch);
}

M0_INTERNAL void m0_dtm_fol_remote_add(struct m0_dtm_fol_remote *frem,
				       struct m0_dtm_oper *oper)
{
	m0_dtm_controlh_add(&frem->rfo_ch, oper);
}

static const struct m0_uint128 *
fol_remote_id(const struct m0_dtm_history *history)
{
	return &history->h_rem->re_id;
}

#if 0
static bool is_update_any_fol(const struct m0_dtm_update *update)
{
	const struct m0_dtm_history_type *htype;

	htype = HISTORY_HTYPE(UPDATE_HISTORY(update));

	return M0_IN(htype, (&m0_dtm_fol_htype,
			     &m0_dtm_fol_remote_htype));
}
#endif

static void fol_remote_persistent(struct m0_dtm_history *history)
{
	struct m0_dtm_oper   *oper;
	struct m0_dtm_update *slup = NULL;

	oper = op_oper(UPDATE_UP(history->h_persistent)->up_op);

	M0_ASSERT(oper);

	oper_for(oper, update) {
		if (HISTORY_HTYPE(UPDATE_HISTORY(update)) ==
		    &m0_dtm_slot_htype) {
			M0_ASSERT(slup == NULL);
			slup = update;
		}
	} oper_endfor;

	if (!slup) {
		return;
	}

	/* Slot silently becomes persistent on the slot-owner
	 * (client) side when at least one RFOL becomes persistent.
	 */
	if (slup->upd_up.up_state < M0_DOS_PERSISTENT) {
		slup->upd_up.up_state = M0_DOS_PERSISTENT;
	}
}

static void fol_remote_stable(struct m0_dtm_history *history)
{}

#if 0
static bool is_update_any_fol(const struct m0_dtm_update *update)
{
	const struct m0_dtm_history_type *htype;

	htype = HISTORY_HTYPE(UPDATE_HISTORY(update));

	return M0_IN(htype, (&m0_dtm_fol_htype,
			     &m0_dtm_fol_remote_htype));
}
#endif

#if 0
static bool fol_remote_is_stable(struct m0_dtm_history *history,
				 struct m0_dtm_update *update)
{
	struct m0_dtm_oper   *oper;
	enum m0_dtm_state     min_state = M0_DOS_STABLE;
	struct m0_dtm_up     *up;

	oper = op_oper(UPDATE_UP(update)->up_op);

	oper_for(oper, other) {
		if (is_update_any_fol(other)) {
			up = UPDATE_UP(other);
			min_state = up->up_state < min_state ?
				up->up_state : min_state;
		}
	} oper_endfor;

	return min_state >= M0_DOS_PERSISTENT;
}
#endif

static bool is_unk0_descr(const struct m0_dtm_update_data *upd)
{
	return upd->da_ver == 0 && upd->da_orig_ver == 0 &&
		upd->da_rule == M0_DUR_INC;
}

static bool is_unk0_update(struct m0_dtm_up *up)
{
	return up->up_ver == 0 && up->up_orig_ver == 0 &&
		up->up_rule == M0_DUR_INC;
}

static void fol_remote_onp(struct m0_dtm_history *history,
			   struct m0_dtm_oper_descr *od)
{
	int                         i;
	int                         rc;
	struct m0_dtm_history      *other = NULL;
	struct m0_dtm_update_descr *ud;
	struct m0_dtm_up           *up;
	struct m0_dtm_history      *h_slot = NULL;
	struct m0_dtm_update       *u_slot = NULL;
	struct m0_dtm_update_descr *ud_slot = NULL;


	/* No persistency for RFOL => nothing to do */
	if (!(history->h_hi.hi_flags & M0_DHF_EAGER)) {
		return;
	}

	for (i = 0; i < od->od_updates.ou_nr; ++i) {
		ud = &od->od_updates.ou_update[i];
		other = NULL;

		/* ignore meaningless updates */
		if (is_unk0_descr(&ud->udd_data)) {
			continue;
		}

		rc = m0_dtm_history_unpack(HISTORY_DTM(history),
					   &ud->udd_id,
					   &other);
		M0_ASSERT(rc == 0);
		M0_ASSERT(ergo(rc == 0, other != NULL));

		if (HISTORY_HTYPE(other) == &m0_dtm_slot_remote_htype) {
			M0_ASSERT(h_slot == NULL);
			h_slot = other;
			ud_slot = ud;
		}
	}

	M0_ASSERT(h_slot);

	history_lock(h_slot);
	up = hi_find(&h_slot->h_hi, ud_slot->udd_data.da_ver);
	M0_ASSERT(up != NULL);
	M0_ASSERT(!is_unk0_update(up));
	u_slot = up_update(up);

	if (u_slot->upd_up.up_state == M0_DOS_PERSISTENT) {
		/* TODO: Operation ended up a persistent storage.
		 * We should open a local tx, update the data and close
		 * it, and it has to be done synchronously.
		 * So far we have no integration with the fol module,
		 * so let's just do nothing
		 */
	}

	for (i = 0; i < od->od_updates.ou_nr; ++i) {
		ud = &od->od_updates.ou_update[i];
		other = NULL;

		if (is_unk0_descr(&ud->udd_data)) {
			continue;
		}

		rc = m0_dtm_history_unpack(HISTORY_DTM(history),
					   &ud->udd_id,
					   &other);
		M0_ASSERT(rc == 0);
		M0_ASSERT(ergo(rc == 0, other != NULL));

		if (HISTORY_HTYPE(other) == &m0_dtm_fol_remote_htype) {
			// history_lock(other);
			up = hi_find(&other->h_hi, ud->udd_data.da_ver);
			M0_ASSERT(up != NULL);
			if (is_unk0_update(up)) {
				up->up_ver = ud->udd_data.da_ver;
				up->up_orig_ver = ud->udd_data.da_orig_ver;
			}
			// history_unlock(other);
		}
	}

#if 1
	M0_LOG(M0_FATAL, "\n\tONP: inst: " U128X_F " slot: %i",
	       U128_P(&HISTORY_DTM(history)->d_id),
	       (int) u_slot->upd_up.up_ver);
#endif

	history_unlock(h_slot);
}

static int fol_remote_find(struct m0_dtm *dtm,
			   const struct m0_dtm_history_type *ht,
			   const struct m0_uint128 *id,
			   struct m0_dtm_history **out)
{
	return m0_dtm_catalogue_lookup(rem_fol_cat(dtm), id, out);
}

static const struct m0_dtm_history_type_ops fol_remote_htype_ops = {
	.hito_find = &fol_remote_find
};

M0_INTERNAL const struct m0_dtm_history_type m0_dtm_fol_remote_htype = {
	.hit_id     = M0_DTM_HTYPE_FOL_REM,
	.hit_rem_id = M0_DTM_HTYPE_FOL,
	.hit_name   = "remote fol",
	.hit_ops    = &fol_remote_htype_ops
};

static const struct m0_dtm_history_ops fol_remote_ops = {
	.hio_type       = &m0_dtm_fol_remote_htype,
	.hio_id         = &fol_remote_id,
	.hio_persistent = &fol_remote_persistent,
	.hio_update     = &m0_dtm_controlh_update,
	.hio_onp        = &fol_remote_onp,
	.hio_is_stable  = NULL /* &fol_remote_is_stable */,
	.hio_stable     = &fol_remote_stable,
};



/* TODO: move slots into their own modules? */

/* Slot section */
static const struct m0_dtm_history_ops slot_ops;
static const struct m0_dtm_history_ops slot_remote_ops;

M0_INTERNAL void m0_dtm_slot_init(struct m0_dtm_slot *slot, struct m0_dtm *dtm)
{
	struct m0_dtm_history *history = &slot->sl_ch.ch_history;

	m0_dtm_controlh_init(&slot->sl_ch, dtm);
	history->h_hi.hi_flags |= M0_DHF_OWNED;
	history->h_ops = &slot_ops;
	history->h_rem = NULL;
}

M0_INTERNAL void m0_dtm_slot_fini(struct m0_dtm_slot *slot)
{
	m0_dtm_controlh_fini(&slot->sl_ch);
}

M0_INTERNAL struct m0_dtm_update *m0_dtm_slot_add(struct m0_dtm_slot *slot,
						  struct m0_dtm_oper *oper)
{
	return m0_dtm_controlh_add(&slot->sl_ch, oper);
}

static int slot_find(struct m0_dtm *dtm, const struct m0_dtm_history_type *ht,
		     const struct m0_uint128 *id, struct m0_dtm_history **out)
{
	M0_PRE(m0_uint128_eq(id, &dtm->d_id));
	*out = &dtm->d_slot.sl_ch.ch_history;
	return 0;
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
	/* As opposite to FOLs, slots do nothing
	 * in terms of inter-participant communication.
	 */

	/* noop */
}

static void slot_fixed(struct m0_dtm_history *history)
{
	/* Slot history cannot be closed therefore it cannot be fixed. */

	M0_IMPOSSIBLE("A slot cannot be fixed!");
}

static const struct m0_uint128 *slot_id(const struct m0_dtm_history *history)
{
	/* XXX: only one slot per DTM instance so far */
	return &HISTORY_DTM(history)->d_id;
}


static const struct m0_dtm_history_ops slot_ops = {
	.hio_type       = &m0_dtm_slot_htype,
	.hio_id         = &slot_id,
	.hio_persistent = &slot_persistent,
	.hio_fixed      = &slot_fixed,
	.hio_update     = &m0_dtm_controlh_update
};

/* Remote slot section */

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
	srem->rs_rem = remote;

	/* XXX: Remote slot does not send anything
	 * to slot, but remote slot update may be
	 * a part of PERSISTENT notice, therefore it
	 * has to be stored somewhere.
	 * TODO: Check if remote slot could be a part
	 * of dtm_remote in the same way as
	 * fol_remote is a part of it.
	 */
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

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
