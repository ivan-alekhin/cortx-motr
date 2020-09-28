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
#include "motr/magic.h"
#include "motr/setup.h"
#include "rpc/rpclib.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "conf/confc.h"
#include "dtm/service.h"

static int dtms_allocate(struct m0_reqh_service **service,
			 const struct m0_reqh_service_type *stype);
static void dtms_fini(struct m0_reqh_service *service);

static int dtms_start(struct m0_reqh_service *service);
static void dtms_prepare_to_stop(struct m0_reqh_service *service);
static void dtms_stop(struct m0_reqh_service *service);

/**
 * DTM Service type operations.
 */
static const struct m0_reqh_service_type_ops dtms_type_ops = {
	.rsto_service_allocate = dtms_allocate
};

/**
 * DTM Service operations.
 */
static const struct m0_reqh_service_ops dtms_ops = {
	.rso_start           = dtms_start,
	.rso_start_async     = m0_reqh_service_async_start_simple,
	.rso_stop            = dtms_stop,
	.rso_prepare_to_stop = dtms_prepare_to_stop,
	.rso_fini            = dtms_fini
};

#ifndef __KERNEL__
M0_INTERNAL struct m0_reqh_service_type m0_dtm_service_type = {
	.rst_name       = "M0_CST_DTM",
	.rst_ops        = &dtms_type_ops,
	.rst_level      = M0_DTM_SVC_LEVEL,
	.rst_typecode   = M0_CST_DTM,
	.rst_keep_alive = true
};
#endif

M0_INTERNAL int m0_dtms_register(void)
{
	M0_ENTRY();

#ifndef __KERNEL__
	m0_reqh_service_type_register(&m0_dtm_service_type);
#endif

	/* m0_dtm_global_init() is done as part of the general motr init. */
	return M0_RC(0);
}

M0_INTERNAL void m0_dtms_unregister(void)
{
	M0_ENTRY();
	m0_reqh_service_type_unregister(&m0_dtm_service_type);
	/* m0_dtm_global_fini() is done as part of general cleanup */
	M0_LEAVE();
}

static int dtms_allocate(struct m0_reqh_service **service,
                         const struct m0_reqh_service_type *stype)
{
	struct m0_reqh_dtm_service *dtms;

	M0_ENTRY();

	M0_PRE(service != NULL && stype != NULL);

	M0_ALLOC_PTR(dtms);
	if (dtms == NULL)
		return M0_RC(-ENOMEM);

	dtms->rdtms_magic = M0_DTMS_REQH_SVC_MAGIC;

	*service = &dtms->rdtms_gen;
	(*service)->rs_ops = &dtms_ops;
	return M0_RC(0);
}

/**
 * Finalise DTM Service instance.
 * This operation finalises service instance and de-allocate it.
 *
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static void dtms_fini(struct m0_reqh_service *service)
{
	struct m0_reqh_dtm_service *dtms;

	M0_ENTRY();

	M0_PRE(service != NULL);

	dtms = container_of(service, struct m0_reqh_dtm_service, rdtms_gen);
	m0_free(dtms);

	M0_LEAVE();
}

/**
 * Start DTM Service.
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static int dtms_start(struct m0_reqh_service *service)
{
	struct m0_reqh_dtm_service    *dtms;
	int                            rc;
	struct m0_uint128 local_id = {
		.u_hi = service->rs_service_fid.f_container,
		.u_lo = service->rs_service_fid.f_key
	};
	M0_LOG(M0_WARN, "Starting DTM service "FID_F"...",
		FID_P(&service->rs_service_fid));

	M0_ENTRY();

	M0_PRE(service != NULL);

	dtms = container_of(service, struct m0_reqh_dtm_service, rdtms_gen);

	/** Each dtm needs globaly unique id. Let's use service id for that. */
	m0_dtm_init(&dtms->rdtms_dtm, &local_id);

	rc = m0_dtm_client_init(&dtms->rdtms_cli, &dtms->rdtms_dtm,
				service->rs_reqh->rh_pools);
	if (rc != 0) {
		m0_dtm_fini(&dtms->rdtms_dtm);
		M0_LOG(M0_ERROR, "Failed to initialize DTM client. Error %d", rc);
	}
	return M0_RC(rc);
}

/**
 * Preparing DTM Service to stop
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static void dtms_prepare_to_stop(struct m0_reqh_service *service)
{
	struct m0_reqh_dtm_service *dtms;

	M0_PRE(service != NULL);

	dtms = container_of(service, struct m0_reqh_dtm_service, rdtms_gen);

	M0_LOG(M0_WARN, "Stopping DTM service "FID_F"...",
		FID_P(&service->rs_service_fid));

	/** Fini all remotes first, local dtm refers them. */
	m0_dtm_client_fini(&dtms->rdtms_cli);

	/** Fini local dtm instance */
	m0_dtm_fini(&dtms->rdtms_dtm);
}

/**
 * Stops DTM Service.
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static void dtms_stop(struct m0_reqh_service *service)
{;}

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
