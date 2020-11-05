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



#pragma once

#ifndef __MOTR_DTM_SLOT_H__
#define __MOTR_DTM_SLOT_H__


/**
 * @addtogroup dtm
 *
 * @{
 */

/* Slot-based history type and dtx0.
 *
 * The module introduces one extra kind of history types - nexus history.
 * This kind of history helps to introduce a new type of DTX - slot-based dtx0.
 * This dtx0 is supposed to be "redo"-able from any participant of a dtx
 * (clients or servers).
 *
 * The other histories (for example, FOL) use only two kinds - local and remote.
 * An update of kind "local" turns into "remote" inside the corresponding
 * pack() function when the history on the machine is "local" and vice versa
 * when the history on the machine is "remote":
 *	FOL owner (server): local -> remote (request)
 *	Non-onwer (client): remote -> local (reply)
 *
 * This simple system does not work with slot-based history types. Slots are
 * supposed to be "resendable" from the server side. It requires one extra
 * kind of history that connects the servers together. This kind is called
 * nexus -- an item from a set of connections between the servers:
 *	Slot owner (client): owner -> remote (request)
 *	Non-owner (server): remote -> owner (reply)
 *	Non-owner (server): remote -> nexus (redo)
 *
 * TODO:
 *	Cleanup naming:
 *	pick one from the list: dom, owner
 *	pick one from the list: etc, ctr, rest, other, remote
 *	pick one from the list: slf, self, local
 *
 */

/* import */
#include "lib/types.h"                /* m0_uint128, uint32_t, uint64_t */
struct m0_dtm_remote;

/* export */

/** Control history-based slot history. */
struct m0_dtm_slot {
	struct m0_dtm_controlh sl_ch;
};

struct m0_dtm_slot_dtx0;

typedef void (*m0_dtm_dtx0_cb_t)(void *, struct m0_dtm_slot_dtx0 *);

/** A custom update that is linked with a slot-based dtx0 (see
 * ::m0_dtm_slot_dtx0::d0_su).
 */
struct m0_dtm_slot_update {
	struct m0_dtm_update su_update;
	struct m0_dtm_slot *su_slot;
	uint64_t su_index;
};

enum {
	M0_DTX0_MAX_SLOT_NR = 16,
};

/** Slot-based DTX for DMT0 */
struct m0_dtm_slot_dtx0 {
	struct m0_dtm_slot_update d0_su[M0_DTX0_MAX_SLOT_NR];
	uint64_t d0_nr_slots;
	m0_dtm_dtx0_cb_t d0_cb_on_persistent;
	m0_dtm_dtx0_cb_t d0_cb_on_stable;
	void *d0_cb_datum;

	/* TODO: probably, it is enough to have the callbacks? */
	uint64_t d0_nr_fixed;
	bool d0_is_stable;
};

/* Initialize Server-to-Server history (ctr). */
M0_INTERNAL void m0_dtm_slot_remote_init(struct m0_dtm_slot *slot,
					 struct m0_dtm *dtm,
					 struct m0_dtm_remote *remote);
/* Initialize Server-side history (slf). */
M0_INTERNAL void m0_dtm_slot_local_init(struct m0_dtm_slot *slot,
					struct m0_dtm *dtm);
/* Initialize Client-side history (dom). */
M0_INTERNAL void m0_dtm_slot_owner_init(struct m0_dtm_slot *slot,
					struct m0_dtm *dtm,
					struct m0_dtm_remote *remote);
/* Finalize any slot-based history. */
M0_INTERNAL void m0_dtm_slot_fini(struct m0_dtm_slot *slot);


M0_INTERNAL int m0_dtm_slot_dtx0_init(struct m0_dtm_slot_dtx0 *d0,
				      m0_dtm_dtx0_cb_t cb_persistent,
				      m0_dtm_dtx0_cb_t cb_stable,
				      void *cb_datum);
M0_INTERNAL void m0_dtm_slot_dtx0_fini(struct m0_dtm_slot_dtx0 *d0);

/* Add a DTM operation into a DTX.
 * Note: only one oper per dtx is supported (you should call
 * close() right after this call).
 */
M0_INTERNAL int m0_dtm_slot_dtx0_add(struct m0_dtm_slot_dtx0 *d0,
				     struct m0_dtm_oper *oper);

M0_INTERNAL int m0_dtm_slot_dtx0_close(struct m0_dtm_slot_dtx0 *d0);

M0_INTERNAL void m0_dtm_slots_init(struct m0_dtm *dtm);
M0_INTERNAL void m0_dtm_slots_fini(struct m0_dtm *dtm);


M0_INTERNAL bool m0_dtm_is_slot_owner_update(struct m0_dtm_update *update);

/** @} end of dtm group */

#endif /* __MOTR_DTM_SLOT_H__ */

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
