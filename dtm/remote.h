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

#ifndef __MOTR_DTM_REMOTE_H__
#define __MOTR_DTM_REMOTE_H__


/**
 * @addtogroup dtm
 *
 * @{
 */

#include "lib/types.h"               /* m0_uint128 */
#include "xcode/xcode_attr.h"
#include "dtm/update.h"              /* m0_dtm_history_id */
#include "dtm/update_xc.h"           /* m0_dtm_history_id_xc */

/* import */
#include "dtm/fol.h"
#include "dtm/slot.h"
struct m0_dtm_oper;
struct m0_dtm_update;
struct m0_rpc_conn;
struct m0_dtm_remote;
struct m0_reqh;

/* export */
struct m0_dtm_remote;
struct m0_dtm_rpc_remote;
struct m0_dtm_remote_ops;

enum m0_dtm_remote_type {
	/* deprecated: remote fol for client-server interactions */
	M0_DRT_FOL = 0,

	/* Owner points to a dependent node (server).
	 * Owner can be created only on the side that owns
	 * the corresponding slot history (client).
	 */
	M0_DRT_SLOT_OWNER,

	/* Each dependent (from the perspective of slot ownership)
	 * node has a connection to the other nodes of such type.
	 * This kind of connectivity is called nexus (a set of
	 * nodes that are bound to a nexum).
	 */
	M0_DRT_SLOT_NEXUS,
};

struct m0_dtm_remote {
	enum m0_dtm_remote_type	        re_type;
	struct m0_uint128               re_id;
	uint64_t                        re_instance;
	const struct m0_dtm_remote_ops *re_ops;
	union {
		/* Owner (client) always point a pair of slot and remote fol. */
		struct {
			struct m0_dtm_slot       slot;
			struct m0_dtm_fol_remote fol;
		} owner;

		/* Nexus points to another slot only. */
		struct {
			struct m0_dtm_slot       slot;
		} nexus;

	} re;
};

#define M0_DTM_REM_FOL(__remote) ({ \
	M0_ASSERT((__remote)->re_type == M0_DRT_FOL || \
		  (__remote)->re_type == M0_DRT_SLOT_OWNER); \
	&(__remote)->re.owner.fol; \
})

#define M0_DTM_REM_DTM(__remote) ({ \
	struct m0_dtm *__result; \
	if ((__remote)->re_type == M0_DRT_FOL || \
		  (__remote)->re_type == M0_DRT_SLOT_OWNER) { \
		__result = HISTORY_DTM(&(__remote)->re.owner.fol.rfo_ch.ch_history); \
	} else { \
		__result = HISTORY_DTM(&(__remote)->re.nexus.slot.sl_ch.ch_history); \
	} \
	M0_ASSERT(__result); \
	__result; \
})


struct m0_dtm_remote_ops {
	void (*reo_persistent)(struct m0_dtm_remote *rem,
			       struct m0_dtm_history *history);
	void (*reo_fixed)(struct m0_dtm_remote *rem,
			  struct m0_dtm_history *history);
	void (*reo_reset)(struct m0_dtm_remote *rem,
			  struct m0_dtm_history *history);
	void (*reo_undo)(struct m0_dtm_remote *rem,
			 struct m0_dtm_history *history, m0_dtm_ver_t upto);
	void (*reo_send)(struct m0_dtm_remote *rem,
			 struct m0_dtm_update *update);
	void (*reo_resend)(struct m0_dtm_remote *rem,
			   struct m0_dtm_update *update);
};

M0_INTERNAL void m0_dtm_remote_init(struct m0_dtm_remote *remote,
				    struct m0_uint128 *id,
				    struct m0_dtm *local);
M0_INTERNAL void m0_dtm_remote_fini(struct m0_dtm_remote *remote);

M0_INTERNAL void m0_dtm_remote_add(struct m0_dtm_remote *rem,
				   struct m0_dtm_oper *oper,
				   struct m0_dtm_history *history,
				   struct m0_dtm_update *update);

struct m0_dtm_rpc_remote {
	struct m0_dtm_remote  rpr_rem;
	struct m0_rpc_conn   *rpr_conn;
	uint64_t              rpr_magic;
};

M0_INTERNAL void m0_dtm_rpc_remote_init(struct m0_dtm_rpc_remote *remote,
					struct m0_uint128 *id,
					struct m0_dtm *local,
					struct m0_rpc_conn *conn);
M0_INTERNAL void m0_dtm_rpc_remote_fini(struct m0_dtm_rpc_remote *remote);
M0_INTERNAL bool m0_dtm_rpc_remote_is_connected(struct m0_dtm_rpc_remote *remote);

struct m0_dtm_notice {
	struct m0_dtm_history_id dno_id;
	uint64_t                 dno_ver;
	uint8_t                  dno_opcode;
} M0_XCA_RECORD M0_XCA_DOMAIN(rpc);

struct m0_dtm_local_remote {
	struct m0_dtm_remote  lre_rem;
	struct m0_dtm        *lre_dtm;
	struct m0_reqh       *lre_reqh;
};

M0_INTERNAL void m0_dtm_local_remote_init(struct m0_dtm_local_remote *lre,
					  struct m0_uint128 *id,
					  struct m0_dtm *local,
					  struct m0_reqh *reqh);
M0_INTERNAL void m0_dtm_local_remote_init_alt(struct m0_dtm_local_remote *lre,
					  struct m0_dtm *remote,
					  struct m0_dtm *local,
					  struct m0_reqh *reqh);
M0_INTERNAL void m0_dtm_local_remote_fini(struct m0_dtm_local_remote *remote);

/** @} end of dtm group */

#endif /* __MOTR_DTM_REMOTE_H__ */


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
