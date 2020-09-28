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

#ifndef __MOTR_DTM_CLIENT_H__
#define __MOTR_DTM_CLIENT_H__

#include "reqh/reqh_service.h"
#include "dtm/dtm.h"
#include "dtm/remote.h"

/**
 * @addtogroup dtm
 * @see @ref reqh
 *
 * @{
 *
 */

/**
 * The dtm client entitity to encapsulate the list of dtm
 * remotes and related functionality.
 */
struct m0_dtm_client {
	/** The list of all remotes of the local instance. */
	struct m0_tl            dc_rem_list;
	struct m0_mutex         dc_rem_list_lock;

	/** Local dtm instance for this client. */
	struct m0_dtm          *dc_dtm;

	uint64_t                dc_magic;
};

/**
 * The link client-to-remote in order to not load remote
 * with functionality that does not really belong to it.
 */
struct m0_dtm_client_remote
{
	struct m0_dtm_client     *dcr_client;
	struct m0_dtm_rpc_remote  dcr_remote;

	/* link to list of all remotes of particulat local instance */
	struct m0_tlink           dcr_link;

	/* Monitoring life-circle connection state. */
	struct m0_clink           dcr_conn_event;
	/* Monitoring ha connection state. */
	struct m0_clink           dcr_ha_event;

	uint64_t                  dcr_magic;
};

M0_INTERNAL int m0_dtm_client_init(struct m0_dtm_client *dtmc,
				   struct m0_dtm *local_dtm,
				   struct m0_pools_common *pc);
M0_INTERNAL void m0_dtm_client_fini(struct m0_dtm_client *dtmc);
M0_INTERNAL bool m0_dtm_cient_is_connected(struct m0_dtm_client *dtmc);


/** @} end of addtogroup dtm */

#endif /* __MOTR_DTM_CLIENT_H__ */
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
