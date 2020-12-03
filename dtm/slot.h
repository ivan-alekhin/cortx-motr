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



#pragma once

#ifndef __MOTR_DTM_SLOT_H__
#define __MOTR_DTM_SLOT_H__

/**
 * @addtogroup dtm
 *
 * @{
 */

/* Intent slot overview
 * --------------------
 *
 * Intent slot is a history used to order DTM operations in a certain way
 * defined by the owner of an intent slot.
 *
 * Intent slots are intended to be used in conjunction with FOLs.
 * Imagine the following DTM operation:
 *
 * @verbatim
 *     OP1 = [ RFOL1:INC/0, RFOL2:INC/0, RFOL3:INC/0, ... ],
 *     OP2 = [ RFOL1:INC/0, RFOL2:INC/0, RFOL3:INC/0, ... ], where
 *
 *     RFOLn - the remote FOL histories of the nodes 1-3;
 *     INC/0 - INC or UNK rule without version set.
 * @endverbatim
 *
 * Such operations do not provide the participants (1-3) with the information
 * about the order that defines the sequence of their execution.
 * For example, Node1 is free to execture OP1 and then OP2, and vice-versa.
 * The other nodes have the same kind of freedom.
 *
 * However, DTM users sometimes require DTM to execute operations in the
 * "right" order (from their perspective). Intent slot history type helps
 * them to achive that goal:
 * @verbatim
 *     OP3 = [ Slot:INC/1, RFOL1:INC/0, RFOL2:INC/0, RFOL3:INC/0, ... ],
 *     OP4 = [ Slot:INC/2, RFOL1:INC/0, RFOL2:INC/0, RFOL3:INC/0, ... ].
 * @endverbatim
 *
 * In this example OP3 cannot be executed after OP4. And this "restriction"
 * holds for all the participants. However, as per the DTM DLD, some of the
 * updates of OP4 may enter PERSISTENT state even if OP3 is not persisetnt yet.
 * In other words, slot imposes only execution ordering.
 *
 *
 * Slot, Remote slot and DTM remote
 * --------------------------------
 *
 * Intent slots are sometimes called client FOLs because they define
 * ordering of DTM operations on the client side (as opposed to a FOL
 * which defines ordering of DTM operations on the server side).
 *
 * However, slots are different from FOLs if we look at their interaction
 * with their remote counterparts and DTM remotes. Since an update for
 * a slot describes an intent that may be delivered to more than one
 * participant, a slot cannot refer to only one DTM remote. It either have to
 * point to a set of DTM remotes or point to nothing. The later is easier
 * and helps to avoid the need to keep track of the participants.
 * To conclude, the slot history type is a simple control history with
 * no remote history and no specific actions; the only one distinct
 * feature here is that slot history update are always included into
 * on-wire representations, so that any recipient of such a representation
 * could properly order the operation.
 *
 * Remote slot is different of slot in the following ways:
 *     - Remote slot does own its history. Only slot history type is
 *     allowed to produce INC updates with a non-zero version.
 *     - Remote slot is attached to a DTM remote that points to the
 *     corresponding originator/client.
 *     - Remote slot update is resent as it is between the participants
 *     of a DTM operation during stabilization or recovery.
 *
 *
 * DTM slot and NFS4.1 slot
 * ------------------------
 *
 * DTM slot are almost the same as NFS slots. NFS slot allows to identify
 * an RPC compound, and DTM slot allows to identify a DTM operation.
 * DTM slot in conjunction with DTM FOL provides Exactly-Once Semantic in the
 * same way as NFS slot in conjunction with DRC (duplicate request cache)
 * provides it. BE-backed log is an equivalent of the persistent session
 * feature (Exactly-Once Semantic is supported even if volatile state is lost).
 * An update for a slot history is an equivalent to sequence id (sequence
 * number, sa/sr_sequenceid) used in the NFS SEQUENCE operation.
 *
 * @verbatim
 * Contents of a DRC (single slot):
 * | sequence number | request    | reply  |
 * +-----------------+------------+--------+
 * | 1               | LOOKUP /   | OK     |
 * | 2               | LOOKUP /a  | ENOENT |
 * | 3               | MKDIR /a   | OK     |
 * | 4               | MKDIR /a/b | OK     |
 *
 * Contents of a persistent FOL (single slot, single FOL):
 * | slot version | req FOP       | repl FOP | LSN |
 * +--------------------+---------+----------+-----+
 * | 1            | GET /         | OK       | 1   |
 * | 2            | GET /a        | ENOENT   | 2   |
 * | 3            | PUT /a,stat   | OK       | 3   |
 * | 4            | PUT /a/b,stat | OK       | 4   |
 *
 *    NOTE1: read-only requests do not have to be tracked in a FOL,
 *    they are shown here just to compare NFS and DTM.
 *    NOTE2: PUT /a,stat describes two PUT operations (one for "/"+"a" link and
 *    the other one for attributes).
 *    NOTE3: NFS compounds are simplified down to one single operation.
 * @endverbatim
 *
 * The only one difference is the amount of participants: NFS4.1 compounds
 * are sent to only one server while DTM operations may be send to more
 * than one server.
 *
 * In the same way as an NFS session has multiple slots, a DTM instance
 * may have more than one slot to execute concurrently multiple independent
 * DTM operations.
 *
 *
 * Multiple slots
 * --------------
 *
 * Please note that DTM slot describes a single logical clock on the
 * slot-owner side. Each "tick" of this clock is an update in the slot
 * history.
 *
 * One single DTM instance may have more than one slot. It helps to achieve
 * better concurrency in the system: if DTM user wants to execute two
 * independent operations then DTM instance may use different slot histories,
 * so that the second one may be executed even before the first one
 * is executed.
 *
 */

/* import */
#include "dtm/history.h" /* struct m0_dtm_controlh */
struct m0_dtm;
struct m0_dtm_remote;

/* export */
struct m0_dtm_slot;
struct m0_dtm_slot_remote;

/** Intent slot.
 * Intent slot is a history owned (in DTM terms) by the side that initiates
 * a DTM operation that contains information about an "intent".
 */
struct m0_dtm_slot {
	struct m0_dtm_controlh sl_ch;
};

/** Remote intent slot. */
struct m0_dtm_slot_remote {
	struct m0_dtm_controlh rs_ch;
	struct m0_dtm_remote  *rs_rem;
};

/** Initialise an intent slot history within the given DTM instance. */
M0_INTERNAL void m0_dtm_slot_init(struct m0_dtm_slot *slot,
				  struct m0_dtm *dtm);

M0_INTERNAL void m0_dtm_slot_fini(struct m0_dtm_slot *slot);

/** Attach an intent slot to a DTM operation. */
M0_INTERNAL void m0_dtm_slot_add(struct m0_dtm_slot *slot,
				 struct m0_dtm_oper *oper);

/** Initialise a remote slot that points to a slot on the given remote
 * DTM instance.
 * @param dtm Local DTM instance.
 * @param remote DTM remote that is associated with a remote DTM instance.
 */
M0_INTERNAL void m0_dtm_slot_remote_init(struct m0_dtm_slot_remote *srem,
					 struct m0_dtm *dtm,
					 struct m0_dtm_remote *remote);


M0_INTERNAL void m0_dtm_slot_remote_fini(struct m0_dtm_slot_remote *srem);


M0_EXTERN const struct m0_dtm_history_type m0_dtm_slot_remote_htype;
M0_EXTERN const struct m0_dtm_history_type m0_dtm_slot_htype;


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
