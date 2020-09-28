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
   @addtogroup dtm
   @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_DTM
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/locality.h"
#include "lib/types.h"
#include "motr/magic.h"
#include "rpc/rpclib.h"
#include "pool/pool.h"        /* m0_pool, m0_pool_version, m0_pools_common */
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "conf/confc.h"
#include "conf/obj_ops.h"     /* m0_conf_obj_get, m0_conf_obj_put */
#include "ha/note.h"
#include "dtm/service.h"
#include "dtm/fol.h"

static void dtm_client_remote_init(struct m0_dtm_client_remote *rem,
				   struct m0_dtm_client *dtmc);
static void dtm_client_remote_free(struct m0_dtm_client_remote *rem);
static void dtm_client_remote_subscribe(struct m0_dtm_client_remote *rem);
static void dtm_client_remote_unsubscribe(struct m0_dtm_client_remote *rem);

M0_TL_DESCR_DEFINE(dtmc_remotes, "local dtm remotes list", M0_INTERNAL,
		   struct m0_dtm_client_remote, dcr_link, dcr_magic,
		   M0_DTMS_REMOTE_MAGIC, M0_DTMS_REMOTE_HEAD_MAGIC);
M0_TL_DEFINE(dtmc_remotes, M0_INTERNAL, struct m0_dtm_client_remote);

static struct m0_confc *conn2confc(const struct m0_rpc_conn *conn)
{
	return m0_reqh2confc(conn->c_rpc_machine->rm_reqh);
}

/**
 * Connection life-circle states handler.
 *
 * @todo: Handle ctx pools refresh.
 */
static bool remote_conn_event_handler(struct m0_clink *clink)
{
	struct m0_dtm_client_remote *rem = M0_AMB(rem, clink, dcr_conn_event);
	struct m0_rpc_conn          *conn = rem->dcr_remote.rpr_conn;
	struct m0_dtm_client        *dtmc = rem->dcr_client;
	M0_ENTRY();

	M0_PRE(conn != NULL);
	switch (conn->c_sm.sm_state) {
	case M0_RPC_CONN_INITIALISED:
		/* Just created, nothing to do? Not going to happen here. */
		break;
	case M0_RPC_CONN_CONNECTING:
		/* Connection initated, nothing to do? */
		 M0_LOG(M0_WARN, "DTM remote connection (0x%p)/"FID_F" received "
			"M0_RPC_CONN_CONNECTING event.", conn,
			FID_P(&conn->c_svc_fid));
		break;
	case M0_RPC_CONN_ACTIVE:
		/* Connection to remote is established. This late connection. */
		 M0_LOG(M0_WARN, "DTM remote connection (0x%p)/"FID_F" received "
			"M0_RPC_CONN_ACTIVE event.", conn,
			FID_P(&conn->c_svc_fid));
		break;
	case M0_RPC_CONN_TERMINATING:
		/* Local node connect termination initiated? */
		 M0_LOG(M0_WARN, "DTM remote connection (0x%p)/"FID_F" received "
			"M0_RPC_CONN_TERMINATING event.", conn,
			FID_P(&conn->c_svc_fid));
		break;
	case M0_RPC_CONN_TERMINATED:
		/* Connection terminated. Unsubscribe? */
		 M0_LOG(M0_WARN, "DTM remote connection (0x%p)/"FID_F" received "
			"M0_RPC_CONN_TERMINATED event.", conn,
			FID_P(&conn->c_svc_fid));
		break;
	case M0_RPC_CONN_FAILED:
		/*
		 * Connection attempt failed. Remove this
		 * remote from the list and monitoring or
		 * it still can connect later?
		 */
		 M0_LOG(M0_WARN, "DTM remote connection (0x%p)/"FID_F" received "
			"M0_RPC_CONN_FAILED event.", conn,
			FID_P(&conn->c_svc_fid));
		 break;
	case M0_RPC_CONN_FINALISED:
		/*
		 * Memory occupid by connection is about
		 * to be released. Remove from the list
		 * and monitoring?
		 */
		m0_mutex_lock(&dtmc->dc_rem_list_lock);
		dtmc_remotes_tlink_del_fini(rem);
		dtm_client_remote_unsubscribe(rem);
		dtm_client_remote_free(rem);
		m0_mutex_unlock(&dtmc->dc_rem_list_lock);
		break;
	default:
		;
	}

	M0_LEAVE();
	return true;
}

/**
 * Connection ha states changes handler.
 *
 * @todo: Handle ctx pools refresh.
 */
static bool remote_ha_event_handler(struct m0_clink *clink)
{
	struct m0_dtm_client_remote *rem = M0_AMB(rem, clink, dcr_ha_event);
	struct m0_rpc_conn          *conn = rem->dcr_remote.rpr_conn;
	struct m0_conf_obj          *svc_obj;
	M0_ENTRY();

	M0_PRE(conn != NULL);
	if (!m0_fid_is_set(&conn->c_svc_fid))
		return false;
	svc_obj = m0_conf_cache_lookup(&conn2confc(conn)->cc_cache,
				       &conn->c_svc_fid);
	M0_ASSERT(svc_obj != NULL);

	/**
	 * Notify all remotes and local about ha connection state.
	 * Why all? Because dtx can contain updates to more than
	 * one remote. If one of remotes participating in tx is
	 * completely dead, we have to undo the changes on another
	 * remote from this dtx.
	 *
	 * @todo: Unfortunatelly HA is not sending these events
	 * correctly and needs to be fixed first.
	 */
	switch (svc_obj->co_ha_state) {
	case M0_NC_FAILED:
		/*
		 * HA thinks the connection is dead, not just
		 * disconnected. Node is crashed or removed
		 * from the cluster.
		 */
		 M0_LOG(M0_WARN, "DTM remote connection (0x%p)/"FID_F" received "
			"M0_NC_FAILED ha event.", conn, FID_P(&conn->c_svc_fid));
		 break;
	case M0_NC_TRANSIENT:
		/* Transient failure like timeout. */
		 M0_LOG(M0_WARN, "DTM remote connection (0x%p)/"FID_F" received "
			"M0_NC_TRANSIENT ha event.", conn, FID_P(&conn->c_svc_fid));
		 break;
	case M0_NC_ONLINE:
		/*
		 * Became online, including first connection
		 * to late nodes such as clients.
		 */
		 M0_LOG(M0_WARN, "DTM remote connection (0x%p)/"FID_F" received "
			"M0_NC_ONLINE ha event.", conn, FID_P(&conn->c_svc_fid));
		break;
	default:
		;
	}

	M0_LEAVE();
	return true;
}

static void dtm_client_remote_free(struct m0_dtm_client_remote *rem)
{
	M0_PRE(!m0_clink_is_armed(&rem->dcr_conn_event));
	M0_PRE(!m0_clink_is_armed(&rem->dcr_ha_event));

	m0_clink_fini(&rem->dcr_conn_event);
	m0_clink_fini(&rem->dcr_ha_event);
	m0_free(rem);
}

static void dtm_client_remote_init(struct m0_dtm_client_remote *rem,
				   struct m0_dtm_client *dtmc)
{
	m0_clink_init(&rem->dcr_conn_event, remote_conn_event_handler);
	m0_clink_init(&rem->dcr_ha_event, remote_ha_event_handler);
	rem->dcr_client = dtmc;
}

static void dtm_client_connect_log_locked(struct m0_dtm_client *dtmc)
{
	struct m0_dtm_client_remote *rem;

	if (dtmc_remotes_tlist_is_empty(&dtmc->dc_rem_list))
		return;

	M0_LOG(M0_WARN, "List of remotes on DTM client (0x%p)/"U128X_F"...",
		dtmc, U128_P(&dtmc->dc_dtm->d_id));

	m0_tl_for(dtmc_remotes, &dtmc->dc_rem_list, rem) {
		struct m0_rpc_conn *conn = rem->dcr_remote.rpr_conn;
		bool is_conn = m0_dtm_rpc_remote_is_connected(&rem->dcr_remote);
		M0_LOG(M0_WARN, "  remote 0x%p with connection 0x%p/"FID_F
			" status: %s", rem, conn, FID_P(&conn->c_svc_fid),
			is_conn ? "connected" : "not connected");
	}  m0_tl_endfor;
}

/**
 * DTM service is runing on all nodes in the cluster. In order to connect
 * to all DTM services we use service context and connect pool functionality.
 *
 * @todo: Handle pools refresh when all connections get killed and re-populated
 * from the new configuration.
 */
static int dtm_client_remotes_setup(struct m0_dtm_client *dtmc,
				    struct m0_pools_common *pc)
{
	struct m0_dtm_client_remote *rem;
	struct m0_reqh_service_ctx  *ctx;
	struct m0_uint128            rem_id;
	struct m0_uint128            svc_id;

	M0_ASSERT(dtmc->dc_dtm != NULL);

	M0_LOG(M0_WARN, "Setup remotes for DTM client (0x%p)/"U128X_F"...",
		dtmc, U128_P(&dtmc->dc_dtm->d_id));
	/**
	 * The passed id is local dtm instance id and serves as a base id
	 * for allocation of unique ids for all the remotes.
	 */
	rem_id = dtmc->dc_dtm->d_id;

	m0_mutex_lock(&dtmc->dc_rem_list_lock);

	/**
	 * Loop over all the services and setup remotes dtm instance using
	 * local fid as a base for allocating unique dtm ids.
	 */
	m0_tl_for(pools_common_svc_ctx, &pc->pc_svc_ctxs, ctx) {
		svc_id = M0_UINT128(ctx->sc_fid.f_container, ctx->sc_fid.f_key);

		/**
		 * Skip connection to ourself. Connect to nodes with DTM
		 * service, including clients. What do we do if none found?
		 */
		if (m0_uint128_eq(&svc_id, &dtmc->dc_dtm->d_id) ||
		    ctx->sc_type != M0_CST_DTM)
			continue;

		M0_LOG(M0_WARN, "Setup DTM rpc for remote DTM service "U128X_F"...",
			U128_P(&svc_id));

		M0_ALLOC_PTR(rem);
		if (rem == NULL) {
			m0_mutex_unlock(&dtmc->dc_rem_list_lock);
			return -ENOMEM;
		}
		dtm_client_remote_init(rem, dtmc);

		/**
		 * Some services can already be available for connect. Others - not,
		 * especially clients that connect later, when all server side services
		 * started up. These not yet connected services will connect later
		 * and connection status is handled in reqh_service.c
		 */
		rem_id.u_lo++;
		m0_dtm_rpc_remote_init(&rem->dcr_remote, &rem_id, dtmc->dc_dtm,
				       &ctx->sc_rlink.rlk_conn);
		dtm_client_remote_subscribe(rem);

		/** Adding to the list of remotes. */
		dtmc_remotes_tlink_init_at_tail(rem, &dtmc->dc_rem_list);
	} m0_tl_endfor;

	/* There must be at least one remote? */
	if (dtmc_remotes_tlist_is_empty(&dtmc->dc_rem_list)) {
		M0_LOG(M0_WARN, "Empty list of remotes on DTM client (0x%p)/"U128X_F"...",
			dtmc, U128_P(&dtmc->dc_dtm->d_id));
	}

	dtm_client_connect_log_locked(dtmc);
	m0_mutex_unlock(&dtmc->dc_rem_list_lock);

	return 0;
}

M0_INTERNAL int m0_dtm_client_init(struct m0_dtm_client *dtmc,
				   struct m0_dtm *local_dtm,
				   struct m0_pools_common *pc)
{
	int            rc;

	M0_PRE(dtmc->dc_magic == 0);

	dtmc->dc_dtm = local_dtm;
	m0_mutex_init(&dtmc->dc_rem_list_lock);
	dtmc_remotes_tlist_init(&dtmc->dc_rem_list);

	M0_LOG(M0_WARN, "Setup DTM client (0x%p)/"U128X_F"...",
		dtmc, U128_P(&local_dtm->d_id));

	M0_LOG(M0_WARN, "Register DTM remote history type "
		"m0_dtm_fol_remote_htype");

	/* All remotes need remote fol htype. */
	m0_dtm_history_type_register(local_dtm, &m0_dtm_fol_remote_htype);

	rc = dtm_client_remotes_setup(dtmc, pc);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "Failed to initialize DTM remotes "
			"from connections pool. Error %d", rc);
		/* TODO: Will fail because magic is not set! */
		m0_dtm_client_fini(dtmc);
	} else {
		dtmc->dc_magic = M0_DTMS_CLIENT_MAGIC;
	}
	return rc;
}

M0_INTERNAL void m0_dtm_client_fini(struct m0_dtm_client *dtmc)
{
	struct m0_dtm_client_remote *rem;

	M0_LOG(M0_WARN, "Cleanup DTM client (0x%p)/"U128X_F"...",
		dtmc, U128_P(&dtmc->dc_dtm->d_id));

	M0_PRE(dtmc->dc_magic == M0_DTMS_CLIENT_MAGIC);
	m0_mutex_lock(&dtmc->dc_rem_list_lock);
	m0_tl_teardown(dtmc_remotes, &dtmc->dc_rem_list, rem) {
		dtm_client_remote_unsubscribe(rem);
		m0_dtm_rpc_remote_fini(&rem->dcr_remote);
		dtm_client_remote_free(rem);
	}
	m0_mutex_unlock(&dtmc->dc_rem_list_lock);
	dtmc_remotes_tlist_fini(&dtmc->dc_rem_list);
	m0_mutex_fini(&dtmc->dc_rem_list_lock);

	m0_dtm_history_type_deregister(dtmc->dc_dtm, &m0_dtm_fol_remote_htype);
	dtmc->dc_magic = 0;
}

/** Checks if all remotes are connected. */
M0_INTERNAL bool m0_dtm_cient_is_connected(struct m0_dtm_client *dtmc)
{
	struct m0_dtm_client_remote *rem;

	if (dtmc_remotes_tlist_is_empty(&dtmc->dc_rem_list))
		return false;

	M0_PRE(dtmc->dc_magic == M0_DTMS_CLIENT_MAGIC);
	m0_mutex_lock(&dtmc->dc_rem_list_lock);
	m0_tl_for(dtmc_remotes, &dtmc->dc_rem_list, rem) {
		struct m0_rpc_conn *conn = rem->dcr_remote.rpr_conn;
		if (!m0_dtm_rpc_remote_is_connected(&rem->dcr_remote)) {
			M0_LOG(M0_WARN, "Remote 0x%p with connection (0x%p)/"FID_F
				" is not yet connected.", rem, conn,
				FID_P(&conn->c_svc_fid));
			m0_mutex_unlock(&dtmc->dc_rem_list_lock);
			return false;
		}
	}  m0_tl_endfor;
	m0_mutex_unlock(&dtmc->dc_rem_list_lock);
	return true;
}

/**
 * Subscribe for ha and connection life-circle events. What we need
 * to do here is the following.
 *
 * DTM functionality:
 * - track/handle transient network issues such as timeouts;
 * - react on connect/disconnect in case of remotes crash/reboot.
 *
 * Mero core functionality:
 * - startup/cleanup;
 * - ctx pool refresh.
 */
static void dtm_client_remote_subscribe(struct m0_dtm_client_remote *rem)
{
	struct m0_rpc_conn *conn = rem->dcr_remote.rpr_conn;
	struct m0_conf_obj *svc_obj;

	M0_ASSERT(conn != NULL);
	m0_conf_cache_lock(&conn2confc(conn)->cc_cache);
	m0_rpc_machine_lock(conn->c_rpc_machine);

	/** Connection states monitoring (life-circle). */
	m0_clink_add(&conn->c_sm.sm_chan, &rem->dcr_conn_event);

	if (!m0_fid_is_set(&conn->c_svc_fid)) {
		M0_LOG(M0_WARN, "Skipping HA events subscribe for "
			"conn 0x%p with unset fid.", conn);
		goto out;
	}

	/** Connection ha states monitoring */
	svc_obj = m0_conf_cache_lookup(&conn2confc(conn)->cc_cache,
				       &conn->c_svc_fid);
	M0_ASSERT_INFO(svc_obj != NULL, "unknown service " FID_F,
		       FID_P(&conn->c_svc_fid));
	M0_ASSERT(conn->c_ha_clink.cl_cb != NULL);
	M0_LOG(M0_DEBUG, "svc_fid "FID_F", cs_type=%d", FID_P(&conn->c_svc_fid),
	       M0_CONF_CAST(svc_obj, m0_conf_service)->cs_type);
	m0_conf_obj_get(svc_obj);
	m0_clink_add(&svc_obj->co_ha_chan, &rem->dcr_ha_event);

	M0_LOG(M0_WARN, "Subscribed for events for conn (0x%p)/"FID_F"",
		conn, FID_P(&conn->c_svc_fid));

out:
	m0_rpc_machine_unlock(conn->c_rpc_machine);
	m0_conf_cache_unlock(&conn2confc(conn)->cc_cache);
}

static void dtm_client_remote_unsubscribe(struct m0_dtm_client_remote *rem)
{
	struct m0_rpc_conn *conn = rem->dcr_remote.rpr_conn;
	struct m0_conf_obj *svc_obj;

	m0_conf_cache_lock(&conn2confc(conn)->cc_cache);
	m0_rpc_machine_lock(conn->c_rpc_machine);

	if (rem->dcr_conn_event.cl_chan != NULL) {
		m0_clink_del(&rem->dcr_conn_event);
		rem->dcr_conn_event.cl_chan = NULL;
	}

	if (rem->dcr_ha_event.cl_chan != NULL) {
		svc_obj = m0_rpc_conn2svc(conn);
		if (svc_obj != NULL)
			m0_conf_obj_put(svc_obj);
		m0_clink_del(&rem->dcr_ha_event);
		rem->dcr_ha_event.cl_chan = NULL;
	}

	m0_rpc_machine_unlock(conn->c_rpc_machine);
	m0_conf_cache_unlock(&conn2confc(conn)->cc_cache);
}

#undef M0_TRACE_SUBSYSTEM

/** @} endgroup dtm */

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
