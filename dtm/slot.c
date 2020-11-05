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

#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/errno.h"              /* ENOMEM, ENOSYS */

#include "dtm/catalogue.h"
#include "dtm/dtm_internal.h"
#include "dtm/history.h"
#include "dtm/dtm.h"
#include "dtm/slot.h"
#include "dtm/remote.h" /* remote->re_id */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_DTM
#include "lib/trace.h"

/* Naming convention:
 *	DTX is being executed over a group of slots.
 *	A group of slots has items of the following kinds:
 *		DOM - A slot owned by the client. One the client side
 *			all updates belong to this kind of slots.
 *		SLF - a kind of slots that exists only on the "server" side and
 *		       any request (or redo notification) should have at least
 *		       one SLF (self) slot.
 *		       It cannot be created on the "client" side because
 *		       each slot belongs either to only one "node" (self) or
 *		       to all ("others").
 *		CTR - a kind of slots that does not hold any information
 *		      about ownership. It just describes the rest of
 *		      of the updates (et cetera).
 *
 * Htype casting:
 *	Normal mode:
 *	([dom]) -> request(rem) -> (self, [ctr])
 *	(self, [ctr]) -> reply(rem) -> ([dom])
 *	Recovering mode:
 *	(self, [ctr]) -> redo(rem) -> (self', [ctr'])
 *
 * Transmutation:
 *	request(rem): [[dom].to_self(rem), [dom - self].to_ctr(rem)]
 *	reply(rem): [self.to_dom(), [ctr].to_dom()]
 *	redo(rem): [[ctr].get(rem).to_self(), [self.to_other() +
 *	                                       ctr.without(rem)]]
 */
enum {
	M0_DTM_HTYPE_SLOT_DOM  = 15,
	M0_DTM_HTYPE_SLOT_SLF = 16,
	M0_DTM_HTYPE_SLOT_CTR  = 17,
};

/*******************************************************************************/
/* SLOT DOM */

static struct m0_dtm_catalogue *slot_dom_cat(struct m0_dtm *dtm)
{
	return &dtm->d_cat[M0_DTM_HTYPE_SLOT_DOM];
}

static const struct m0_uint128 *
slot_dom_id(const struct m0_dtm_history *history)
{
	return &history->h_rem->re_id;
}

static void slot_dom_persistent(struct m0_dtm_history *history)
{
	struct m0_dtm_update *update = history->h_persistent;
	struct m0_dtm_slot_update *su;
	struct m0_dtm_slot_dtx0 *d0;

	su = M0_AMB(su, update, su_update);

	d0 = (struct m0_dtm_slot_dtx0 *) (su - su->su_index);

	if (d0->d0_nr_fixed == 0 && d0->d0_cb_on_persistent) {
		d0->d0_cb_on_persistent(d0->d0_cb_datum, d0);
	}
	d0->d0_nr_fixed++;
	if (d0->d0_nr_fixed == d0->d0_nr_slots) {
		if (d0->d0_cb_on_stable) {
			d0->d0_cb_on_stable(d0->d0_cb_datum, d0);
		}
	}

}

static int slot_dom_find(struct m0_dtm *dtm,
			 const struct m0_dtm_history_type *ht,
			 const struct m0_uint128 *id,
			 struct m0_dtm_history **out)
{
	return m0_dtm_catalogue_lookup(slot_dom_cat(dtm), id, out);
}

static int slot_dom_to_rem(struct m0_dtm_update *update,
			   struct m0_dtm_remote *remote,
			   uint8_t *out_rem_id)
{
	if (remote == UPDATE_HISTORY(update)->h_rem) {
		/* request: Dom -> Slf */
		*out_rem_id = M0_DTM_HTYPE_SLOT_SLF;
	} else {
		/* request: Dom -> Ctr */
		*out_rem_id = M0_DTM_HTYPE_SLOT_CTR;
	}

	return 0;
}

M0_INTERNAL const struct m0_dtm_history_type_ops slot_dom_htype_ops = {
	.hito_find = &slot_dom_find,
	.hito_to_rem = &slot_dom_to_rem,
};

M0_INTERNAL const struct m0_dtm_history_type m0_dtm_slot_dom_htype = {
	.hit_id     = M0_DTM_HTYPE_SLOT_DOM,
	.hit_rem_id = 0,
	.hit_name   = "slot domain",
	.hit_ops    = &slot_dom_htype_ops
};

M0_INTERNAL const struct m0_dtm_history_ops slot_dom_ops = {
	.hio_type       = &m0_dtm_slot_dom_htype,
	.hio_id         = &slot_dom_id,
	.hio_persistent = &slot_dom_persistent,
	.hio_update     = &m0_dtm_controlh_update
};

M0_INTERNAL bool m0_dtm_is_slot_owner_update(struct m0_dtm_update *update)
{
	uint8_t htid = UPDATE_HISTORY(update)->h_ops->hio_type->hit_id;
	return htid == M0_DTM_HTYPE_SLOT_DOM;
}

/*******************************************************************************/
/* SLOT SLF */

static bool is_slot_ctr_update(struct m0_dtm_update *update)
{
	uint8_t htid = UPDATE_HISTORY(update)->h_ops->hio_type->hit_id;
	return htid == M0_DTM_HTYPE_SLOT_CTR;
}

static void slot_slf_persistent(struct m0_dtm_history *history)
{
	struct m0_dtm_oper *oper;
	struct m0_dtm_history *other;

	M0_PRE(history->h_persistent);
	M0_PRE(UPDATE_UP(history->h_persistent)->up_op);

	oper = M0_AMB(oper, UPDATE_UP(history->h_persistent)->up_op, oprt_op);

	oper_for(oper, update) {
		if (is_slot_ctr_update(update)) {
			other = UPDATE_HISTORY(update);
			/* Remove the cookie to make it work on a single
			 * machine. Othwerwise, it tries to get the wrong
			 * DTM instance.
			 */
			history->h_remcookie = (struct m0_cookie) { 0, 0};
			other->h_rem->re_ops->reo_persistent(other->h_rem, history);
		}
	}
	oper_endfor;
}

static const struct m0_uint128 *
slot_slf_id(const struct m0_dtm_history *history)
{
	return &HISTORY_DTM(history)->d_id;
}

static int slot_slf_find(struct m0_dtm *dtm,
			 const struct m0_dtm_history_type *ht,
			 const struct m0_uint128 *id,
			 struct m0_dtm_history **out)
{
	if (!m0_uint128_eq(id, &dtm->d_id)) {
		return M0_ERR(-EPROTO);
	}

	*out = &dtm->d_slot.sl_ch.ch_history;
	return 0;
}

static int slot_slf_to_rem(struct m0_dtm_update *update,
			   struct m0_dtm_remote *remote,
			   uint8_t *out_rem_id)
{
	/* DTM Notice (fixed/persistent) */
	if (update == NULL) {
		*out_rem_id = M0_DTM_HTYPE_SLOT_CTR;
		return 0;
	}

	M0_PRE(UPDATE_HISTORY(update)->h_rem == NULL);

	if (remote == NULL) {
		/* reply: Slf -> Dom */
		*out_rem_id = M0_DTM_HTYPE_SLOT_DOM;
	} else {
		/* redo: Slf -> Ctr */
		*out_rem_id = M0_DTM_HTYPE_SLOT_CTR;
	}

	return 0;
}

M0_INTERNAL const struct m0_dtm_history_type_ops slot_slf_htype_ops = {
	.hito_find = &slot_slf_find,
	.hito_to_rem = &slot_slf_to_rem,
};

M0_INTERNAL const struct m0_dtm_history_type m0_dtm_slot_slf_htype = {
	.hit_id     = M0_DTM_HTYPE_SLOT_SLF,
	.hit_rem_id = 0,
	.hit_name   = "slot self",
	.hit_ops    = &slot_slf_htype_ops
};

M0_INTERNAL const struct m0_dtm_history_ops slot_slf_ops = {
	.hio_type       = &m0_dtm_slot_slf_htype,
	.hio_id         = &slot_slf_id,
	.hio_persistent = &slot_slf_persistent,
	.hio_update     = &m0_dtm_controlh_update
};


/*******************************************************************************/
/* SLOT CTR */

static struct m0_dtm_catalogue *slot_ctr_cat(struct m0_dtm *dtm)
{
	return &dtm->d_cat[M0_DTM_HTYPE_SLOT_CTR];
}

static const struct m0_uint128 *
slot_ctr_id(const struct m0_dtm_history *history)
{
	return &history->h_rem->re_id;
}

static void slot_ctr_persistent(struct m0_dtm_history *history)
{}

static void slot_ctr_fixed(struct m0_dtm_history *history)
{
	/* TODO: Mark as fixed and keep this info in the
	 * persistent log.
	 */
	M0_LOG(M0_FATAL, "Fixed: %p", history);
}

static int slot_ctr_find(struct m0_dtm *dtm,
			   const struct m0_dtm_history_type *ht,
			   const struct m0_uint128 *id,
			   struct m0_dtm_history **out)
{
	return m0_dtm_catalogue_lookup(slot_ctr_cat(dtm), id, out);
}

static int slot_ctr_to_rem(struct m0_dtm_update *update,
			   struct m0_dtm_remote *remote,
			   uint8_t *out_rem_id)
{

	/* CTR slots always have a remote */
	M0_PRE(UPDATE_HISTORY(update)->h_rem != NULL);

	if (remote == NULL) {
		/* reply: Ctr -> Dom */
		*out_rem_id = M0_DTM_HTYPE_SLOT_DOM;
		return 0;
	}

	if (remote == UPDATE_HISTORY(update)->h_rem) {
		/* redo: Ctr -> Slf' */
		*out_rem_id = M0_DTM_HTYPE_SLOT_SLF;
		return 0;
	} else {
		/* redo: Ctr -> Ctr */
		*out_rem_id = M0_DTM_HTYPE_SLOT_CTR;
		return 0;
	}

	M0_IMPOSSIBLE("Invalid Ctr slot update runtime state.");
	return M0_ERR(-EINVAL);
}

M0_INTERNAL const struct m0_dtm_history_type_ops slot_ctr_htype_ops = {
	.hito_find = &slot_ctr_find,
	.hito_to_rem = &slot_ctr_to_rem,
};

M0_INTERNAL const struct m0_dtm_history_type m0_dtm_slot_ctr_htype = {
	.hit_id     = M0_DTM_HTYPE_SLOT_CTR,
	.hit_rem_id = 0,
	.hit_name   = "slot cetera",
	.hit_ops    = &slot_ctr_htype_ops
};

M0_INTERNAL const struct m0_dtm_history_ops slot_ctr_ops = {
	.hio_type       = &m0_dtm_slot_ctr_htype,
	.hio_id         = &slot_ctr_id,
	.hio_persistent = &slot_ctr_persistent,
	.hio_fixed      = &slot_ctr_fixed,
	.hio_update     = &m0_dtm_controlh_update
};

/*******************************************************************************/
/* Public init/fini */

void m0_dtm_slot_remote_init(struct m0_dtm_slot *slot,
			     struct m0_dtm *dtm,
			     struct m0_dtm_remote *remote)
{
	struct m0_dtm_history *history = &slot->sl_ch.ch_history;

	m0_dtm_controlh_init(&slot->sl_ch, dtm);
	history->h_ops = &slot_ctr_ops;
	history->h_rem = remote;
	m0_dtm_catalogue_add(slot_ctr_cat(dtm), history);
}

void m0_dtm_slot_local_init(struct m0_dtm_slot *slot,
			    struct m0_dtm *dtm)
{
	struct m0_dtm_history *history = &slot->sl_ch.ch_history;

	m0_dtm_controlh_init(&slot->sl_ch, dtm);
	history->h_ops = &slot_slf_ops;
	history->h_rem = NULL;
}

void m0_dtm_slot_owner_init(struct m0_dtm_slot *slot,
			    struct m0_dtm *dtm,
			    struct m0_dtm_remote *remote)
{
	struct m0_dtm_history *history = &slot->sl_ch.ch_history;

	m0_dtm_controlh_init(&slot->sl_ch, dtm);
	history->h_hi.hi_flags |= M0_DHF_OWNED;
	history->h_ops = &slot_dom_ops;
	history->h_rem = remote;
	m0_dtm_catalogue_add(slot_dom_cat(dtm), history);
}

void m0_dtm_slot_fini(struct m0_dtm_slot *slot)
{
	struct m0_dtm_history *history = &slot->sl_ch.ch_history;

	if (history->h_ops == &slot_dom_ops) {
		m0_dtm_catalogue_del(slot_dom_cat(HISTORY_DTM(history)), history);
	}
	if (history->h_ops == &slot_ctr_ops) {
		m0_dtm_catalogue_del(slot_ctr_cat(HISTORY_DTM(history)), history);
	}

	m0_dtm_controlh_fini(&slot->sl_ch);
	M0_SET0(slot);
}

/*******************************************************************************/
/* Global Init/Fini */
M0_INTERNAL void m0_dtm_slots_init(struct m0_dtm *dtm)
{

	m0_dtm_history_type_register(dtm, &m0_dtm_slot_dom_htype);
	m0_dtm_history_type_register(dtm, &m0_dtm_slot_slf_htype);
	m0_dtm_history_type_register(dtm, &m0_dtm_slot_ctr_htype);
}

M0_INTERNAL void m0_dtm_slots_fini(struct m0_dtm *dtm)
{
	m0_dtm_history_type_deregister(dtm, &m0_dtm_slot_dom_htype);
	m0_dtm_history_type_deregister(dtm, &m0_dtm_slot_slf_htype);
	m0_dtm_history_type_deregister(dtm, &m0_dtm_slot_ctr_htype);
}

/*******************************************************************************/
/* DTX0 */

M0_INTERNAL int m0_dtm_slot_dtx0_init(struct m0_dtm_slot_dtx0 *d0,
				      m0_dtm_dtx0_cb_t cb_persistent,
				      m0_dtm_dtx0_cb_t cb_stable,
				      void *cb_datum)
{
	*d0 = (struct m0_dtm_slot_dtx0) {
		.d0_cb_on_persistent = cb_persistent,
		.d0_cb_on_stable = cb_stable,
		.d0_cb_datum = cb_datum,
	};

	return 0;
}

void m0_dtm_slot_dtx0_fini(struct m0_dtm_slot_dtx0 *d0)
{
	/* TODO: clean it up */
	// M0_ASSERT(0);
}

static bool m0_dtm_slot_dtx0_has_slot(struct m0_dtm_slot_dtx0 *d0,
				      struct m0_dtm_slot *slot)
{
	int i;

	for (i = 0; i < d0->d0_nr_slots; ++i) {
		if (d0->d0_su[i].su_slot == slot) {
			return true;
		}
	}

	return false;
}

static struct m0_dtm_slot *history_slot(struct m0_dtm_history *history)
{
	struct m0_dtm_controlh *ch;
	struct m0_dtm_slot *slot;

	ch = M0_AMB(ch, history, ch_history);
	slot = M0_AMB(slot, ch, sl_ch);

	return slot;
}

static int m0_dtm_slot_dtx_get(struct m0_dtm_slot_dtx0 *d0,
			       struct m0_dtm *dtm,
			       struct m0_dtm_remote *remote,
			       struct m0_dtm_slot **out_sl,
			       struct m0_dtm_update **out_u)
{
	struct m0_dtm_history *pa_history;
	struct m0_dtm_slot *pa;
	struct m0_dtm_slot_update su;
	int rc;

	rc = m0_dtm_catalogue_lookup(slot_dom_cat(dtm),
				     &remote->re_id,
				     &pa_history);
	if (rc != 0) {
		return M0_ERR(rc);
	}

	M0_ASSERT(pa_history);
	pa = history_slot(pa_history);

	M0_ASSERT(!m0_dtm_slot_dtx0_has_slot(d0, pa));

	su = (struct m0_dtm_slot_update) {
		.su_slot = pa,
		.su_index = d0->d0_nr_slots,
	};

	d0->d0_su[d0->d0_nr_slots++] = su;

	*out_sl = d0->d0_su[d0->d0_nr_slots - 1].su_slot;
	*out_u = &d0->d0_su[d0->d0_nr_slots - 1].su_update;

	return 0;
}

int m0_dtm_slot_dtx0_add(struct m0_dtm_slot_dtx0 *d0,
			 struct m0_dtm_oper *oper)
{
	struct m0_dtm_slot *pa;
	struct m0_dtm_update *pa_u;
	int rc;

	oper_for(oper, update) {
		if (!UPDATE_REM(update)) {
			continue;
		}

		rc = m0_dtm_slot_dtx_get(d0,
					 HISTORY_DTM(UPDATE_HISTORY(update)),
					 UPDATE_REM(update), &pa, &pa_u);
		M0_ASSERT(rc == 0);

		m0_dtm_controlh_add_static(&pa->sl_ch, oper, pa_u);
	}
	oper_endfor;

	return 0;
}

int m0_dtm_slot_dtx0_close(struct m0_dtm_slot_dtx0 *d0)
{
	/* nothing to do at the moment */
	return 0;
}

/*******************************************************************************/
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
