/* -*- C -*- */
/*
 * Copyright (c) 2012-2020 Seagate Technology LLC and/or its Affiliates
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


#include "fop/fop.h"
#include "fop/fop_item_type.h"

#include "sns/cm/cm.h"
#include "sns/cm/trigger_fop.h"
#include "cm/repreb/trigger_fop.h"
#include "cm/repreb/trigger_fop_xc.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/trace.h"

/*
 * Implements a simplistic sns repair trigger FOM for corresponding trigger FOP.
 * This is solely for testing purpose and a separate trigger FOP/FOM will be
 * implemented later, which would be similar to this one.
 */

extern struct m0_cm_type sns_rebalance_cmt;
extern const struct m0_fom_type_ops m0_sns_trigger_fom_type_ops;

M0_INTERNAL void m0_sns_cm_rebalance_trigger_fop_fini(void)
{
	m0_cm_trigger_fop_fini(&m0_sns_rebalance_trigger_fopt);
	m0_cm_trigger_fop_fini(&m0_sns_rebalance_trigger_rep_fopt);
	m0_cm_trigger_fop_fini(&m0_sns_rebalance_quiesce_fopt);
	m0_cm_trigger_fop_fini(&m0_sns_rebalance_quiesce_rep_fopt);
	m0_cm_trigger_fop_fini(&m0_sns_rebalance_status_fopt);
	m0_cm_trigger_fop_fini(&m0_sns_rebalance_status_rep_fopt);
	m0_cm_trigger_fop_fini(&m0_sns_rebalance_abort_fopt);
	m0_cm_trigger_fop_fini(&m0_sns_rebalance_abort_rep_fopt);
}

M0_INTERNAL void m0_sns_cm_rebalance_trigger_fop_init(void)
{
	m0_cm_trigger_fop_init(&m0_sns_rebalance_trigger_fopt,
			       M0_SNS_REBALANCE_TRIGGER_OPCODE,
			       "sns rebalance trigger",
			       trigger_fop_xc,
			        M0_RPC_MUTABO_REQ,
			        &sns_rebalance_cmt,
			        &m0_sns_trigger_fom_type_ops);
	m0_cm_trigger_fop_init(&m0_sns_rebalance_trigger_rep_fopt,
			       M0_SNS_REBALANCE_TRIGGER_REP_OPCODE,
			       "sns rebalance trigger reply",
			       trigger_rep_fop_xc,
			       M0_RPC_ITEM_TYPE_REPLY,
			       &sns_rebalance_cmt,
			       &m0_sns_trigger_fom_type_ops);

	m0_cm_trigger_fop_init(&m0_sns_rebalance_quiesce_fopt,
			       M0_SNS_REBALANCE_QUIESCE_OPCODE,
			       "sns rebalance quiesce trigger",
			       trigger_fop_xc,
			       M0_RPC_MUTABO_REQ,
			       &sns_rebalance_cmt,
			       &m0_sns_trigger_fom_type_ops);
	m0_cm_trigger_fop_init(&m0_sns_rebalance_quiesce_rep_fopt,
			       M0_SNS_REBALANCE_QUIESCE_REP_OPCODE,
			       "sns rebalance quiesce trigger reply",
			       trigger_rep_fop_xc,
			       M0_RPC_ITEM_TYPE_REPLY,
			       &sns_rebalance_cmt,
			       &m0_sns_trigger_fom_type_ops);

	m0_cm_trigger_fop_init(&m0_sns_rebalance_status_fopt,
			       M0_SNS_REBALANCE_STATUS_OPCODE,
			       "sns rebalance status",
			       trigger_fop_xc,
			       M0_RPC_MUTABO_REQ,
			       &sns_rebalance_cmt,
			       &m0_sns_trigger_fom_type_ops);
	m0_cm_trigger_fop_init(&m0_sns_rebalance_status_rep_fopt,
			       M0_SNS_REBALANCE_STATUS_REP_OPCODE,
			       "sns rebalance status reply",
			       m0_status_rep_fop_xc,
			       M0_RPC_ITEM_TYPE_REPLY,
			       &sns_rebalance_cmt,
			       &m0_sns_trigger_fom_type_ops);
	m0_cm_trigger_fop_init(&m0_sns_rebalance_abort_fopt,
			       M0_SNS_REBALANCE_ABORT_OPCODE,
			       "sns rebalance abort",
			       m0_status_rep_fop_xc,
			       M0_RPC_ITEM_TYPE_REQUEST,
			       &sns_rebalance_cmt,
			       &m0_sns_trigger_fom_type_ops);
	m0_cm_trigger_fop_init(&m0_sns_rebalance_abort_rep_fopt,
			       M0_SNS_REBALANCE_ABORT_REP_OPCODE,
			       "sns rebalance abort reply",
			       m0_status_rep_fop_xc,
			       M0_RPC_ITEM_TYPE_REPLY,
			       &sns_rebalance_cmt,
			       &m0_sns_trigger_fom_type_ops);
}

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
