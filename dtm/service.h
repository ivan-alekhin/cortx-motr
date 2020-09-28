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

#ifndef __MOTR_DTM_SERVICE_H__
#define __MOTR_DTM_SERVICE_H__

#include "reqh/reqh_service.h"
#include "dtm/dtm.h"
#include "dtm/client.h"

/**
 * @addtogroup dtm
 * @see @ref reqh
 *
 * @{
 *
 */

struct m0_reqh_dtm_svc_params {
	/** XXX: Nothing for now. */
};

/**
 * Structure contains generic service structure and
 * service specific/privite information.
 */
struct m0_reqh_dtm_service {
	/** Generic reqh service object */
	struct m0_reqh_service  rdtms_gen;

	/** Local dtm instance, used on server and client. */
	struct m0_dtm           rdtms_dtm;
	/** Client with the remotes. */
	struct m0_dtm_client    rdtms_cli;

	/** Magic to check dtm service object */
	uint64_t                rdtms_magic;
};

M0_INTERNAL void m0_dtms_unregister(void);
M0_INTERNAL int m0_dtms_register(void);

/** @} end of addtogroup dtm */

#endif /* __MOTR_DTM_SERVICE_H__ */
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
