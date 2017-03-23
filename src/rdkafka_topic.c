/*
 * librdkafka - Apache Kafka C library
 *
 * Copyright (c) 2012,2013 Magnus Edenhill
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer. 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "rd.h"
#include "rdkafka_int.h"
#include "rdkafka_msg.h"
#include "rdkafka_topic.h"
#include "rdkafka_partition.h"
#include "rdkafka_broker.h"
#include "rdkafka_cgrp.h"
#include "rdlog.h"
#include "rdsysqueue.h"
#include "rdtime.h"



const char *rd_kafka_topic_state_names[] = {
        "unknown",
        "exists",
        "notexists"
};


/**
 * @brief Increases the app's topic reference count and returns the app pointer.
 *
 * The app refcounts are implemented separately from the librdkafka refcounts
 * and to play nicely with shptr we keep one single shptr for the application
 * and increase/decrease a separate rkt_app_refcnt to keep track of its use.
 *
 * This only covers topic_new() & topic_destroy().
 * The topic_t exposed in rd_kafka_message_t is NOT covered and is handled
 * like a standard shptr -> app pointer conversion (keep_a()).
 *
 * @returns a (new) rkt app reference.
 *
 * @remark \p rkt and \p s_rkt are mutually exclusive.
 */
static rd_kafka_topic_t *rd_kafka_topic_keep_app (rd_kafka_itopic_t *rkt) {
    rd_kafka_topic_t *app_rkt;

        mtx_lock(&rkt->rkt_app_lock);
    rkt->rkt_app_refcnt++;
        if (!(app_rkt = rkt->rkt_app_rkt))
                app_rkt = rkt->rkt_app_rkt = rd_kafka_topic_keep_a(rkt);
        mtx_unlock(&rkt->rkt_app_lock);

    return app_rkt;
}

/**
 * @brief drop rkt app reference
 */
static void rd_kafka_topic_destroy_app (rd_kafka_topic_t *app_rkt) {
    if(!app_rkt)
      return;
    rd_kafka_itopic_t *rkt = rd_kafka_topic_a2i(app_rkt);
    shptr_rd_kafka_itopic_t *s_rkt = NULL;

    mtx_lock(&rkt->rkt_app_lock);
    rd_kafka_assert(NULL, rkt->rkt_app_refcnt > 0);
    rkt->rkt_app_refcnt--;
    if (unlikely(rkt->rkt_app_refcnt == 0)) {
        rd_kafka_assert(NULL, rkt->rkt_app_rkt);
        s_rkt = rd_kafka_topic_a2s(app_rkt);
        rkt->rkt_app_rkt = NULL;
    }
    mtx_unlock(&rkt->rkt_app_lock);

    if (s_rkt) /* final app reference lost, destroy the shared ptr. */
        rd_kafka_topic_destroy0(s_rkt);
}


/**
 * Final destructor for topic. Refcnt must be 0.
 */
void rd_kafka_topic_destroy_final (rd_kafka_itopic_t *rkt) {

    rd_kafka_assert(rkt->rkt_rk, rd_refcnt_get(&rkt->rkt_refcnt) == 0);

    if (rkt->rkt_topic)
        rd_kafkap_str_destroy(rkt->rkt_topic);

    rd_kafka_assert(rkt->rkt_rk, rd_list_empty(&rkt->rkt_desp));
    rd_list_destroy(&rkt->rkt_desp, NULL);

    rd_kafka_wrlock(rkt->rkt_rk);
    TAILQ_REMOVE(&rkt->rkt_rk->rk_topics, rkt, rkt_link);
    rkt->rkt_rk->rk_topic_cnt--;
    rd_kafka_wrunlock(rkt->rkt_rk);

    rd_kafka_anyconf_destroy(_RK_TOPIC, &rkt->rkt_conf);

    mtx_destroy(&rkt->rkt_app_lock);
    rwlock_destroy(&rkt->rkt_lock);
    rd_refcnt_destroy(&rkt->rkt_refcnt);

    if(is_streams_topic(rkt)) {
        // Destroy streams specific data-structures.
        streams_destroy_topic(rkt);
    }

    rd_free(rkt);
}

/**
 * Application destroy
 */
void rd_kafka_topic_destroy (rd_kafka_topic_t *app_rkt) {
    rd_kafka_topic_destroy_app(app_rkt);
}


/**
 * Finds and returns a topic based on its name, or NULL if not found.
 * The 'rkt' refcount is increased by one and the caller must call
 * rd_kafka_topic_destroy() when it is done with the topic to decrease
 * the refcount.
 *
 * Locality: any thread
 */
shptr_rd_kafka_itopic_t *rd_kafka_topic_find_fl (const char *func, int line,
                                                rd_kafka_t *rk,
                                                const char *topic, int do_lock){
	rd_kafka_itopic_t *rkt;
        shptr_rd_kafka_itopic_t *s_rkt = NULL;

        if (do_lock)
                rd_kafka_rdlock(rk);
	TAILQ_FOREACH(rkt, &rk->rk_topics, rkt_link) {
		if (!rd_kafkap_str_cmp_str(rkt->rkt_topic, topic)) {
                        s_rkt = rd_kafka_topic_keep(rkt);
			break;
		}
	}
        if (do_lock)
                rd_kafka_rdunlock(rk);

	return s_rkt;
}

/**
 * Same semantics as ..find() but takes a Kafka protocol string instead.
 */
shptr_rd_kafka_itopic_t *rd_kafka_topic_find0_fl (const char *func, int line,
                                                 rd_kafka_t *rk,
                                                 const rd_kafkap_str_t *topic) {
	rd_kafka_itopic_t *rkt;
        shptr_rd_kafka_itopic_t *s_rkt = NULL;

	rd_kafka_rdlock(rk);
	TAILQ_FOREACH(rkt, &rk->rk_topics, rkt_link) {
		if (!rd_kafkap_str_cmp(rkt->rkt_topic, topic)) {
                        s_rkt = rd_kafka_topic_keep(rkt);
			break;
		}
	}
	rd_kafka_rdunlock(rk);

	return s_rkt;
}



/**
 * Create new topic handle. 
 *
 * Locality: any
 */
shptr_rd_kafka_itopic_t *rd_kafka_topic_new0 (rd_kafka_t *rk,
                                              const char *topic,
                                              rd_kafka_topic_conf_t *conf,
                                              int *existing,
                                              int do_lock) {
    rd_kafka_itopic_t *rkt;
    shptr_rd_kafka_itopic_t *s_rkt;

    /* Verify configuration */
    if (!topic) {
        if (conf)
            rd_kafka_topic_conf_destroy(conf);
        rd_kafka_set_last_error(RD_KAFKA_RESP_ERR__INVALID_ARG,
                EINVAL);
        return NULL;
    }

    if (do_lock)
        rd_kafka_wrlock(rk);
    if ((s_rkt = rd_kafka_topic_find(rk, topic, 0/*no lock*/))) {
        if (do_lock)
            rd_kafka_wrunlock(rk);
        if (conf)
            rd_kafka_topic_conf_destroy(conf);
        if (existing)
            *existing = 1;
        return s_rkt;
    }

    if (existing)
        *existing = 0;

    rkt = rd_calloc(1, sizeof(*rkt));

    rkt->rkt_rk        = rk;

    // Streams specific.
    bool add_str_name_to_topic = false;
    if (rk->rk_conf.streams_producer_default_stream_name) {
        if (topic[0] != '/') {
            size_t new_topic_len = strlen(topic) +
                    1 + // ':'
                    strlen (rk->rk_conf.streams_producer_default_stream_name) +
                    1; //snprintf '\0'
            char new_topic_str[new_topic_len];
            snprintf(new_topic_str, new_topic_len, "%s:%s",
                    rk->rk_conf.streams_producer_default_stream_name, topic);
            rkt->rkt_topic = rd_kafkap_str_new(new_topic_str, -1);
            add_str_name_to_topic = true;
        }
    }

    if (!add_str_name_to_topic)
        rkt->rkt_topic     = rd_kafkap_str_new(topic, -1);

    int err = streams_check_and_init_topic(rkt);
    // Invalid topic<->producer combination is used.
    if (err != 0 ) {
        rd_kafkap_str_destroy(rkt->rkt_topic);
        rd_free(rkt);
        if (conf)
            rd_kafka_topic_conf_destroy(conf);
        rd_kafka_set_last_error(RD_KAFKA_RESP_ERR__INVALID_ARG,
                                EINVAL);
        return NULL;
    }

    if (!conf) {
        if (rk->rk_conf.topic_conf)
            conf = rd_kafka_topic_conf_dup(rk->rk_conf.topic_conf);
        else
            conf = rd_kafka_topic_conf_new();
    }
    rkt->rkt_conf = *conf;
    rd_free(conf); /* explicitly not rd_kafka_topic_destroy()
     * since we dont want to rd_free internal members,
     * just the placeholder. The internal members
     * were copied on the line above. */

    /* Default partitioner: consistent_random */
    if (!rkt->rkt_conf.partitioner)
        rkt->rkt_conf.partitioner = rd_kafka_msg_partitioner_consistent_random;

    if (rkt->rkt_conf.compression_codec == RD_KAFKA_COMPRESSION_INHERIT)
        rkt->rkt_conf.compression_codec = rk->rk_conf.compression_codec;

    rd_kafka_dbg(rk, TOPIC, "TOPIC", "New local topic: %.*s",
            RD_KAFKAP_STR_PR(rkt->rkt_topic));

    rd_list_init(&rkt->rkt_desp, 16);
    rd_refcnt_init(&rkt->rkt_refcnt, 0);

    s_rkt = rd_kafka_topic_keep(rkt);

    rwlock_init(&rkt->rkt_lock);
    mtx_init(&rkt->rkt_app_lock, mtx_plain);

    /* Create unassigned partition */
    rkt->rkt_ua = rd_kafka_toppar_new(rkt, RD_KAFKA_PARTITION_UA);

    TAILQ_INSERT_TAIL(&rk->rk_topics, rkt, rkt_link);
    rk->rk_topic_cnt++;

    if (do_lock)
        rd_kafka_wrunlock(rk);

    return s_rkt;
}

/**
 * Create new app topic handle.
 *
 * Locality: application thread
 */
rd_kafka_topic_t *rd_kafka_topic_new (rd_kafka_t *rk, const char *topic,
                                      rd_kafka_topic_conf_t *conf) {
        if (!rk || !topic)
          return NULL;
        shptr_rd_kafka_itopic_t *s_rkt;
        rd_kafka_itopic_t *rkt;
        rd_kafka_topic_t *app_rkt;
        int existing;

        s_rkt = rd_kafka_topic_new0(rk, topic, conf, &existing, 1/*lock*/);
        if (!s_rkt)
                return NULL;

        rkt = rd_kafka_topic_s2i(s_rkt);

        /* Save a shared pointer to be used in callbacks. */
        app_rkt = rd_kafka_topic_keep_app(rkt);

        /* Query for the topic leader (async) */
        if (!existing)
                rd_kafka_topic_leader_query(rk, rkt);

        /* Drop our reference since there is already/now a rkt_app_rkt */
        rd_kafka_topic_destroy0(s_rkt);

        return app_rkt;
}



/**
 * Sets the state for topic.
 * NOTE: rd_kafka_topic_wrlock(rkt) MUST be held
 */
static void rd_kafka_topic_set_state (rd_kafka_itopic_t *rkt, int state) {

        if ((int)rkt->rkt_state == state)
                return;

        rd_kafka_dbg(rkt->rkt_rk, TOPIC, "STATE",
                     "Topic %s changed state %s -> %s",
                     rkt->rkt_topic->str,
                     rd_kafka_topic_state_names[rkt->rkt_state],
                     rd_kafka_topic_state_names[state]);
        rkt->rkt_state = state;
}

/**
 * Returns the name of a topic.
 * NOTE:
 *   The topic Kafka String representation is crafted with an extra byte
 *   at the end for the Nul that is not included in the length, this way
 *   we can use the topic's String directly.
 *   This is not true for Kafka Strings read from the network.
 */
const char *rd_kafka_topic_name (const rd_kafka_topic_t *app_rkt) {
	if(!app_rkt)
	  return NULL;
        const rd_kafka_itopic_t *rkt = rd_kafka_topic_a2i(app_rkt);
	return rkt->rkt_topic->str;
}





/**
 * Update the leader for a topic+partition.
 * Returns 1 if the leader was changed, else 0, or -1 if leader is unknown.
 * NOTE: rd_kafka_topic_wrlock(rkt) MUST be held.
 */
static int rd_kafka_topic_leader_update (rd_kafka_itopic_t *rkt,
                                         int32_t partition, int32_t leader_id,
					 rd_kafka_broker_t *rkb) {
	rd_kafka_t *rk = rkt->rkt_rk;
	rd_kafka_toppar_t *rktp;
        shptr_rd_kafka_toppar_t *s_rktp;

	s_rktp = rd_kafka_toppar_get(rkt, partition, 0);
        if (unlikely(!s_rktp)) {
                /* Have only seen this in issue #132.
                 * Probably caused by corrupt broker state. */
                rd_kafka_log(rk, LOG_WARNING, "LEADER",
                             "Topic %s: partition [%"PRId32"] is unknown "
                             "(partition_cnt %i)",
                             rkt->rkt_topic->str, partition,
                             rkt->rkt_partition_cnt);
                return -1;
        }

        rktp = rd_kafka_toppar_s2i(s_rktp);

        rd_kafka_toppar_lock(rktp);

	if (!rkb) {
		int had_leader = rktp->rktp_leader ? 1 : 0;

		rd_kafka_toppar_broker_delegate(rktp, NULL);

                rd_kafka_toppar_unlock(rktp);
		rd_kafka_toppar_destroy(s_rktp); /* from get() */

		return had_leader ? -1 : 0;
	}


	if (rktp->rktp_leader) {
		if (rktp->rktp_leader == rkb) {
			/* No change in broker */
                        rd_kafka_toppar_unlock(rktp);
			rd_kafka_toppar_destroy(s_rktp); /* from get() */
			return 0;
		}

		rd_kafka_dbg(rk, TOPIC, "TOPICUPD",
			     "Topic %s [%"PRId32"] migrated from "
			     "broker %"PRId32" to %"PRId32,
			     rkt->rkt_topic->str, partition,
			     rktp->rktp_leader->rkb_nodeid, rkb->rkb_nodeid);
	}

	rd_kafka_toppar_broker_delegate(rktp, rkb);

        rd_kafka_toppar_unlock(rktp);
	rd_kafka_toppar_destroy(s_rktp); /* from get() */

	return 1;
}


/**
 * Update the number of partitions for a topic and takes according actions.
 * Returns 1 if the partition count changed, else 0.
 * NOTE: rd_kafka_topic_wrlock(rkt) MUST be held.
 */
static int rd_kafka_topic_partition_cnt_update (rd_kafka_itopic_t *rkt,
						int32_t partition_cnt) {
	rd_kafka_t *rk = rkt->rkt_rk;
	shptr_rd_kafka_toppar_t **rktps;
	shptr_rd_kafka_toppar_t *rktp_ua;
        shptr_rd_kafka_toppar_t *s_rktp;
	rd_kafka_toppar_t *rktp;
	int32_t i;

	if (likely(rkt->rkt_partition_cnt == partition_cnt))
		return 0; /* No change in partition count */

        if (unlikely(rkt->rkt_partition_cnt != 0 &&
                     !rd_kafka_terminating(rkt->rkt_rk)))
                rd_kafka_log(rk, LOG_NOTICE, "PARTCNT",
                             "Topic %s partition count changed "
                             "from %"PRId32" to %"PRId32,
                             rkt->rkt_topic->str,
                             rkt->rkt_partition_cnt, partition_cnt);
        else
                rd_kafka_dbg(rk, TOPIC, "PARTCNT",
                             "Topic %s partition count changed "
                             "from %"PRId32" to %"PRId32,
                             rkt->rkt_topic->str,
                             rkt->rkt_partition_cnt, partition_cnt);


	/* Create and assign new partition list */
	if (partition_cnt > 0)
		rktps = rd_calloc(partition_cnt, sizeof(*rktps));
	else
		rktps = NULL;

	for (i = 0 ; i < partition_cnt ; i++) {
		if (i >= rkt->rkt_partition_cnt) {
			/* New partition. Check if its in the list of
			 * desired partitions first. */

                        s_rktp = rd_kafka_toppar_desired_get(rkt, i);

                        rktp = s_rktp ? rd_kafka_toppar_s2i(s_rktp) : NULL;
                        if (rktp) {
				rd_kafka_toppar_lock(rktp);
                                if (rktp->rktp_flags &
                                    RD_KAFKA_TOPPAR_F_UNKNOWN) {
                                        /* Remove from desp list since the 
                                         * partition is now known. */
                                        rktp->rktp_flags &=
                                                ~RD_KAFKA_TOPPAR_F_UNKNOWN;
                                        rd_kafka_toppar_desired_unlink(rktp);
                                }
                                rd_kafka_toppar_unlock(rktp);
			} else
				s_rktp = rd_kafka_toppar_new(rkt, i);
			rktps[i] = s_rktp;
		} else {
			/* Move existing partition */
			rktps[i] = rkt->rkt_p[i];
		}
	}

	rktp_ua = rd_kafka_toppar_get(rkt, RD_KAFKA_PARTITION_UA, 0);

        /* Propagate notexist errors for desired partitions */
        RD_LIST_FOREACH(s_rktp, &rkt->rkt_desp, i)
                rd_kafka_toppar_enq_error(rd_kafka_toppar_s2i(s_rktp),
                                          RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION);

	/* Remove excessive partitions if partition count decreased. */
	for (; i < rkt->rkt_partition_cnt ; i++) {
		s_rktp = rkt->rkt_p[i];
                rktp = rd_kafka_toppar_s2i(s_rktp);

		rd_kafka_toppar_lock(rktp);

                rd_kafka_toppar_broker_delegate(rktp, NULL);

		/* Partition has gone away, move messages to UA or error-out */
		if (likely(rktp_ua != NULL))
			rd_kafka_toppar_move_msgs(rd_kafka_toppar_s2i(rktp_ua),
                                                  rktp);
		else
                        rd_kafka_dr_msgq(rkt, &rktp->rktp_msgq,
                                         RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION);


                rd_kafka_toppar_purge_queues(rktp);

		if (rktp->rktp_flags & RD_KAFKA_TOPPAR_F_DESIRED) {
                        rd_kafka_dbg(rkt->rkt_rk, TOPIC, "DESIRED",
                                     "Topic %s [%"PRId32"] is desired "
                                     "but no longer known: "
                                     "moving back on desired list",
                                     rkt->rkt_topic->str, rktp->rktp_partition);

                        /* If this is a desired partition move it back on to
                         * the desired list since partition is no longer known*/
			rd_kafka_assert(rkt->rkt_rk,
                                        !(rktp->rktp_flags &
                                          RD_KAFKA_TOPPAR_F_UNKNOWN));
			rktp->rktp_flags |= RD_KAFKA_TOPPAR_F_UNKNOWN;
                        rd_kafka_toppar_desired_link(rktp);

                        if (!rd_kafka_terminating(rkt->rkt_rk))
                                rd_kafka_toppar_enq_error(
                                        rktp,
                                        RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION);
		}
		rd_kafka_toppar_unlock(rktp);

		rd_kafka_toppar_destroy(s_rktp);
	}

	if (likely(rktp_ua != NULL))
		rd_kafka_toppar_destroy(rktp_ua); /* .._get() above */

	if (rkt->rkt_p)
		rd_free(rkt->rkt_p);

	rkt->rkt_p = rktps;

	rkt->rkt_partition_cnt = partition_cnt;

	return 1;
}



/**
 * Topic 'rkt' does not exist: propagate to interested parties.
 * The topic's state must have been set to NOTEXISTS and
 * rd_kafka_topic_partition_cnt_update() must have been called prior to
 * calling this function.
 *
 * Locks: rd_kafka_topic_*lock() must be held.
 */
static void rd_kafka_topic_propagate_notexists (rd_kafka_itopic_t *rkt) {
        shptr_rd_kafka_toppar_t *s_rktp;
        int i;

        if (rkt->rkt_rk->rk_type != RD_KAFKA_CONSUMER)
                return;


        /* Notify consumers that the topic doesn't exist. */
        RD_LIST_FOREACH(s_rktp, &rkt->rkt_desp, i)
                rd_kafka_toppar_enq_error(rd_kafka_toppar_s2i(s_rktp),
                                          RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC);
}


/**
 * Assign messages on the UA partition to available partitions.
 * Locks: rd_kafka_topic_*lock() must be held.
 */
static void rd_kafka_topic_assign_uas (rd_kafka_itopic_t *rkt) {
	rd_kafka_t *rk = rkt->rkt_rk;
	shptr_rd_kafka_toppar_t *s_rktp_ua;
        rd_kafka_toppar_t *rktp_ua;
	rd_kafka_msg_t *rkm, *tmp;
	rd_kafka_msgq_t uas = RD_KAFKA_MSGQ_INITIALIZER(uas);
	rd_kafka_msgq_t failed = RD_KAFKA_MSGQ_INITIALIZER(failed);
	int cnt;

	if (rkt->rkt_rk->rk_type != RD_KAFKA_PRODUCER)
		return;

	s_rktp_ua = rd_kafka_toppar_get(rkt, RD_KAFKA_PARTITION_UA, 0);
	if (unlikely(!s_rktp_ua)) {
		rd_kafka_dbg(rk, TOPIC, "ASSIGNUA",
			     "No UnAssigned partition available for %s",
			     rkt->rkt_topic->str);
		return;
	}

        rktp_ua = rd_kafka_toppar_s2i(s_rktp_ua);

	/* Assign all unassigned messages to new topics. */
	rd_kafka_dbg(rk, TOPIC, "PARTCNT",
		     "Partitioning %i unassigned messages in topic %.*s to "
		     "%"PRId32" partitions",
		     rd_atomic32_get(&rktp_ua->rktp_msgq.rkmq_msg_cnt),
		     RD_KAFKAP_STR_PR(rkt->rkt_topic),
		     rkt->rkt_partition_cnt);

	rd_kafka_toppar_lock(rktp_ua);
	rd_kafka_msgq_move(&uas, &rktp_ua->rktp_msgq);
	cnt = rd_atomic32_get(&uas.rkmq_msg_cnt);
	rd_kafka_toppar_unlock(rktp_ua);

	TAILQ_FOREACH_SAFE(rkm, &uas.rkmq_msgs, rkm_link, tmp) {
		/* Fast-path for failing messages with forced partition */
		if (rkm->rkm_partition != RD_KAFKA_PARTITION_UA &&
		    rkm->rkm_partition >= rkt->rkt_partition_cnt &&
		    rkt->rkt_state != RD_KAFKA_TOPIC_S_UNKNOWN) {
			rd_kafka_msgq_enq(&failed, rkm);
			continue;
		}

		if (unlikely(rd_kafka_msg_partitioner(rkt, rkm, 0) != 0)) {
			/* Desired partition not available */
			rd_kafka_msgq_enq(&failed, rkm);
		}
	}

	rd_kafka_dbg(rk, TOPIC, "UAS",
		     "%i/%i messages were partitioned in topic %s",
		     cnt - rd_atomic32_get(&failed.rkmq_msg_cnt),
		     cnt, rkt->rkt_topic->str);

	if (rd_atomic32_get(&failed.rkmq_msg_cnt) > 0) {
		/* Fail the messages */
		rd_kafka_dbg(rk, TOPIC, "UAS",
			     "%"PRId32"/%i messages failed partitioning "
			     "in topic %s",
			     rd_atomic32_get(&uas.rkmq_msg_cnt), cnt,
			     rkt->rkt_topic->str);
		rd_kafka_dr_msgq(rkt, &failed,
				 rkt->rkt_state == RD_KAFKA_TOPIC_S_NOTEXISTS ?
				 RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC :
				 RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION);
	}

	rd_kafka_toppar_destroy(s_rktp_ua); /* from get() */
}


/**
 * Received metadata request contained no information about topic 'rkt'
 * and thus indicates the topic is not available in the cluster.
 */
void rd_kafka_topic_metadata_none (rd_kafka_itopic_t *rkt) {
	rd_kafka_topic_wrlock(rkt);

	if (unlikely(rd_atomic32_get(&rkt->rkt_rk->rk_terminate))) {
		/* Dont update metadata while terminating, do this
		 * after acquiring lock for proper synchronisation */
		rd_kafka_topic_wrunlock(rkt);
		return;
	}

	rkt->rkt_ts_metadata = rd_clock();

        rd_kafka_topic_set_state(rkt, RD_KAFKA_TOPIC_S_NOTEXISTS);

	/* Update number of partitions */
	rd_kafka_topic_partition_cnt_update(rkt, 0);

	/* Purge messages with forced partition */
	rd_kafka_topic_assign_uas(rkt);

        /* Propagate nonexistent topic info */
        rd_kafka_topic_propagate_notexists(rkt);

	rd_kafka_topic_wrunlock(rkt);
}


/**
 * Update a topic from metadata.
 * Returns 1 if the number of partitions changed, 0 if not, and -1 if the
 * topic is unknown.
 */
int rd_kafka_topic_metadata_update (rd_kafka_broker_t *rkb,
                                    const struct rd_kafka_metadata_topic *mdt) {
	rd_kafka_itopic_t *rkt;
        shptr_rd_kafka_itopic_t *s_rkt;
	int upd = 0;
	int j;
        rd_kafka_broker_t **partbrokers;
        int query_leader = 0;
        int old_state;

	/* Ignore topics in blacklist */
        if (rkb->rkb_rk->rk_conf.topic_blacklist &&
	    rd_kafka_pattern_match(rkb->rkb_rk->rk_conf.topic_blacklist,
                                   mdt->topic)) {
                rd_rkb_dbg(rkb, TOPIC, "BLACKLIST",
                           "Ignoring blacklisted topic \"%s\" in metadata",
                           mdt->topic);
                return -1;
        }

	/* Ignore metadata completely for temporary errors. (issue #513)
	 *   LEADER_NOT_AVAILABLE: Broker is rebalancing
	 */
	if (mdt->err == RD_KAFKA_RESP_ERR_LEADER_NOT_AVAILABLE &&
	    mdt->partition_cnt == 0) {
		rd_rkb_dbg(rkb, TOPIC, "METADATA",
			   "Temporary error in metadata reply for "
			   "topic %s (PartCnt %i): %s: ignoring",
			   mdt->topic, mdt->partition_cnt,
			   rd_kafka_err2str(mdt->err));
		return -1;
	}


	if (!(s_rkt = rd_kafka_topic_find(rkb->rkb_rk, mdt->topic, 1/*lock*/)))
		return -1; /* Ignore topics that we dont have locally. */

        rkt = rd_kafka_topic_s2i(s_rkt);

	if (mdt->err != RD_KAFKA_RESP_ERR_NO_ERROR)
		rd_rkb_dbg(rkb, TOPIC, "METADATA",
			   "Error in metadata reply for "
			   "topic %s (PartCnt %i): %s",
			   rkt->rkt_topic->str, mdt->partition_cnt,
			   rd_kafka_err2str(mdt->err));

	/* Look up brokers before acquiring rkt lock to preserve lock order */
	rd_kafka_rdlock(rkb->rkb_rk);

	if (unlikely(rd_atomic32_get(&rkb->rkb_rk->rk_terminate))) {
		/* Dont update metadata while terminating, do this
		 * after acquiring lock for proper synchronisation */
		rd_kafka_rdunlock(rkb->rkb_rk);
		rd_kafka_topic_destroy0(s_rkt); /* from find() */
		return -1;
	}

        partbrokers = rd_alloca(mdt->partition_cnt * sizeof(*partbrokers));

	for (j = 0 ; j < mdt->partition_cnt ; j++) {
		if (mdt->partitions[j].leader == -1) {
                        partbrokers[j] = NULL;
			continue;
		}

                partbrokers[j] =
                        rd_kafka_broker_find_by_nodeid(rkb->rkb_rk,
                                                       mdt->partitions[j].
                                                       leader);
	}
	rd_kafka_rdunlock(rkb->rkb_rk);


	rd_kafka_topic_wrlock(rkt);

        old_state = rkt->rkt_state;
	rkt->rkt_ts_metadata = rd_clock();

	/* Set topic state */
	if (mdt->err == RD_KAFKA_RESP_ERR_UNKNOWN_TOPIC_OR_PART ||
	    mdt->err == RD_KAFKA_RESP_ERR_UNKNOWN/*auto.create.topics fails*/)
                rd_kafka_topic_set_state(rkt, RD_KAFKA_TOPIC_S_NOTEXISTS);
        else if (mdt->partition_cnt > 0)
                rd_kafka_topic_set_state(rkt, RD_KAFKA_TOPIC_S_EXISTS);

	/* Update number of partitions, but not if there are
	 * (possibly intermittent) errors (e.g., "Leader not available"). */
	if (mdt->err == RD_KAFKA_RESP_ERR_NO_ERROR)
		upd += rd_kafka_topic_partition_cnt_update(rkt,
							   mdt->partition_cnt);

	/* Update leader for each partition */
	for (j = 0 ; j < mdt->partition_cnt ; j++) {
                int r;
		rd_kafka_broker_t *leader;

		rd_rkb_dbg(rkb, METADATA, "METADATA",
			   "  Topic %s partition %i Leader %"PRId32,
			   rkt->rkt_topic->str,
			   mdt->partitions[j].id,
			   mdt->partitions[j].leader);

		leader = partbrokers[j];
		partbrokers[j] = NULL;

		/* Update leader for partition */
		r = rd_kafka_topic_leader_update(rkt,
                                                 mdt->partitions[j].id,
                                                 mdt->partitions[j].leader,
						 leader);

                if (r == -1)
                        query_leader = 1;

                upd += (r != 0 ? 1 : 0);

                /* Drop reference to broker (from find()) */
		if (leader)
			rd_kafka_broker_destroy(leader);

	}

	if (mdt->err != RD_KAFKA_RESP_ERR_NO_ERROR && rkt->rkt_partition_cnt) {
		/* (Possibly intermediate) topic-wide error:
		 * remove leaders for partitions */

		for (j = 0 ; j < rkt->rkt_partition_cnt ; j++) {
                        rd_kafka_toppar_t *rktp;
			if (!rkt->rkt_p[j])
                                continue;

                        rktp = rd_kafka_toppar_s2i(rkt->rkt_p[j]);
                        rd_kafka_toppar_lock(rktp);
                        rd_kafka_toppar_broker_delegate(rktp, NULL);
                        rd_kafka_toppar_unlock(rktp);
                }
        }

	/* Try to assign unassigned messages to new partitions, or fail them */
	if (upd > 0 || rkt->rkt_state == RD_KAFKA_TOPIC_S_NOTEXISTS)
		rd_kafka_topic_assign_uas(rkt);

        /* Trigger notexists propagation */
        if (old_state != (int)rkt->rkt_state &&
            rkt->rkt_state == RD_KAFKA_TOPIC_S_NOTEXISTS)
                rd_kafka_topic_propagate_notexists(rkt);

	rd_kafka_topic_wrunlock(rkt);

        /* Query for the topic leader (async) */
        if (query_leader)
                rd_kafka_topic_leader_query(rkt->rkt_rk, rkt);

	rd_kafka_topic_destroy0(s_rkt); /* from find() */

	/* Loose broker references */
	for (j = 0 ; j < mdt->partition_cnt ; j++)
		if (partbrokers[j])
			rd_kafka_broker_destroy(partbrokers[j]);


	return upd;
}



/**
 * Remove all partitions from a topic, including the ua.
 * WARNING: Any messages in partition queues will be LOST.
 */
void rd_kafka_topic_partitions_remove (rd_kafka_itopic_t *rkt) {
	rd_kafka_toppar_t *rktp;
        shptr_rd_kafka_toppar_t *s_rktp;
        shptr_rd_kafka_itopic_t *s_rkt;
	int i;

	s_rkt = rd_kafka_topic_keep(rkt);
	rd_kafka_topic_wrlock(rkt);

        rd_kafka_topic_partition_cnt_update(rkt, 0);

        /* Remove desired partitions.
         * Use reverse traversal to avoid excessive memory shuffling
         * in rd_list_remove() */
        RD_LIST_FOREACH_REVERSE(s_rktp, &rkt->rkt_desp, i) {
                shptr_rd_kafka_toppar_t *s_rktp2;
                rktp = rd_kafka_toppar_s2i(s_rktp);
                s_rktp2 = rd_kafka_toppar_keep(rktp);
                rd_kafka_toppar_lock(rktp);
                rd_kafka_toppar_desired_del(rktp);
                rd_kafka_toppar_unlock(rktp);
                rd_kafka_toppar_destroy(s_rktp2);
        }

        rd_kafka_assert(rkt->rkt_rk, rkt->rkt_partition_cnt == 0);

	if (rkt->rkt_p)
		rd_free(rkt->rkt_p);

	rkt->rkt_p = NULL;
	rkt->rkt_partition_cnt = 0;

        if ((s_rktp = rkt->rkt_ua)) {
                rkt->rkt_ua = NULL;
                rd_kafka_toppar_destroy(s_rktp);
	}

	rd_kafka_topic_wrunlock(rkt);
	rd_kafka_topic_destroy0(s_rkt);
}



/**
 * Scan all topics and partitions for:
 *  - timed out messages.
 *  - topics that needs to be created on the broker.
 *  - topics who's metadata is too old.
 */
int rd_kafka_topic_scan_all (rd_kafka_t *rk, rd_ts_t now) {
	rd_kafka_itopic_t *rkt;
	rd_kafka_toppar_t *rktp;
        shptr_rd_kafka_toppar_t *s_rktp;
	int totcnt = 0;
	int wrlocked = 0;


	rd_kafka_rdlock(rk);
	TAILQ_FOREACH(rkt, &rk->rk_topics, rkt_link) {
		int p;
                int cnt = 0, tpcnt = 0;
                rd_kafka_msgq_t timedout;

                rd_kafka_msgq_init(&timedout);

		rd_kafka_topic_wrlock(rkt);
		wrlocked = 1;

                /* Check if metadata information has timed out:
                 * older than 3 times the metadata.refresh.interval.ms */
                if (rkt->rkt_state != RD_KAFKA_TOPIC_S_UNKNOWN &&
		    rkt->rkt_rk->rk_conf.metadata_refresh_interval_ms >= 0 &&
                    rd_clock() > rkt->rkt_ts_metadata +
                    (rkt->rkt_rk->rk_conf.metadata_refresh_interval_ms *
                     1000 * 3)) {
                        rd_kafka_dbg(rk, TOPIC, "NOINFO",
                                     "Topic %s metadata information timed out "
                                     "(%"PRId64"ms old)",
                                     rkt->rkt_topic->str,
                                     (rd_clock() - rkt->rkt_ts_metadata)/1000);
                        rd_kafka_topic_set_state(rkt, RD_KAFKA_TOPIC_S_UNKNOWN);
                }

                /* Just need a read-lock from here on. */
                rd_kafka_topic_wrunlock(rkt);
                rd_kafka_topic_rdlock(rkt);
				wrlocked = 0;

		if (rkt->rkt_partition_cnt == 0) {
			/* If this partition is unknown by brokers try
			 * to create it by sending a topic-specific
			 * metadata request.
			 * This requires "auto.create.topics.enable=true"
			 * on the brokers. */

			/* Need to unlock topic lock first.. */
			rd_kafka_topic_rdunlock(rkt);
			rd_kafka_topic_leader_query0(rk, rkt, 0/*no_rk_lock*/);
			rd_kafka_topic_rdlock(rkt);
		}

		for (p = RD_KAFKA_PARTITION_UA ;
		     p < rkt->rkt_partition_cnt ; p++) {
			if (!(s_rktp = rd_kafka_toppar_get(rkt, p, 0)))
				continue;

                        rktp = rd_kafka_toppar_s2i(s_rktp);
			rd_kafka_toppar_lock(rktp);

			/* Scan toppar's message queue for timeouts */
			if (rd_kafka_msgq_age_scan(&rktp->rktp_msgq,
						   &timedout, now) > 0)
				tpcnt++;

			rd_kafka_toppar_unlock(rktp);
			rd_kafka_toppar_destroy(s_rktp);
		}

		if (wrlocked)
			rd_kafka_topic_wrunlock(rkt);
		else
			rd_kafka_topic_rdunlock(rkt);

                if ((cnt = rd_atomic32_get(&timedout.rkmq_msg_cnt)) > 0) {
                        totcnt += cnt;
                        rd_kafka_dbg(rk, MSG, "TIMEOUT",
                                     "%s: %"PRId32" message(s) "
                                     "from %i toppar(s) timed out",
                                     rkt->rkt_topic->str, cnt, tpcnt);
                        rd_kafka_dr_msgq(rkt, &timedout,
                                         RD_KAFKA_RESP_ERR__MSG_TIMED_OUT);
                }
	}
	rd_kafka_rdunlock(rk);

	return totcnt;
}


int streams_topic_partition_available (rd_kafka_itopic_t *rkt, int32_t partition) {
        if (partition < 0)
                return 0;

        int num_part = 0;
        streams_producer_get_num_partitions (
                        (const streams_producer_t)
                        rkt->rkt_rk->streams_producer,
                        rkt->rkt_topic->str, &num_part);

        if (partition < num_part)
                return 1;

        return 0;
}

/**
 * Locks: rd_kafka_topic_*lock() must be held.
 */
int rd_kafka_topic_partition_available (const rd_kafka_topic_t *app_rkt,
					int32_t partition) {
        if(!app_rkt)
                return 0;
	int avail;
	shptr_rd_kafka_toppar_t *s_rktp;
        rd_kafka_toppar_t *rktp;
        rd_kafka_broker_t *rkb;

        rd_kafka_itopic_t *itopic =
                  rd_kafka_topic_a2i(app_rkt);
        if(is_streams_producer (itopic->rkt_rk)) {
          return streams_topic_partition_available(itopic,
                                            partition);
	}
	s_rktp = rd_kafka_toppar_get(itopic,
                                     partition, 0/*no ua-on-miss*/);
	if (unlikely(!s_rktp))
		return 0;

        rktp = rd_kafka_toppar_s2i(s_rktp);
        rkb = rd_kafka_toppar_leader(rktp, 1/*proper broker*/);
        avail = rkb ? 1 : 0;
        if (rkb)
                rd_kafka_broker_destroy(rkb);
	rd_kafka_toppar_destroy(s_rktp);
	return avail;
}


void *rd_kafka_topic_opaque (const rd_kafka_topic_t *app_rkt) {
        return rd_kafka_topic_a2i(app_rkt)->rkt_conf.opaque;
}

bool is_streams_topic (rd_kafka_itopic_t *irkt) {
  if (!irkt) {
    return false;
  }
  return irkt->is_streams_topic;
}


bool streams_is_valid_topic_name (const char * topic_name, bool *isRegex) {
  if(!topic_name)
    return false;
  if(streams_check_full_path_is_valid (topic_name, strlen(topic_name),
                                        isRegex) == 0)
    return true;
  return false;
}

void streams_topic_free (char **streams_topics,
			 int num_topics) {
	int i;
	for(i=0; i< num_topics; i++) {
    rd_free((void *)streams_topics[i]);
  }

	rd_free(streams_topics);
}

int streams_get_topic_names (const rd_kafka_topic_partition_list_t *topics,
			 char ** streams_topics,
			 int *tcount) {
	int streams_topic_count = 0;
	int kafka_topic_count = 0;
	int i;
  for (i=0; i < topics->cnt; i++) {
		const char *topic_name =  (topics->elems[i]).topic;
		if (streams_is_valid_topic_name(topic_name, NULL)) {
			streams_topics[streams_topic_count] = rd_strndup(topic_name,
									 strlen(topic_name));
			streams_topic_count++;
		} else {
			kafka_topic_count++;
		}

		if (streams_topic_count!=0 && kafka_topic_count!=0) {
			//Both streams and kafka topic names provided
			*tcount = streams_topic_count;
			return MIX_TOPICS;
		}
	}

	if (streams_topic_count > 0 ) {
		return STREAMS_TOPICS;
	} else if(kafka_topic_count > 0 ) {
		*tcount = streams_topic_count;
		return KAFKA_TOPICS;
	} else {
		*tcount = streams_topic_count;
		return MIX_TOPICS;
	}
}

int streams_get_regex_topic_names (rd_kafka_t *rk,
                                   const rd_kafka_topic_partition_list_t *topics,
                                   char ***streams_topics, char ***regex_topics,
                                   int *streams_count, int*regex_count) {
  int streams_topic_count = 0;
  int kafka_topic_count = 0;
  int regex_topic_count = 0;
  int i;
  char **temp_topics = *streams_topics;
  char **temp_reg_topics = *regex_topics;
  for (i=0; i < topics->cnt; i++) {
    const char *topic_name =  (topics->elems[i]).topic;
    bool isRegex = false;
    if (streams_is_valid_topic_name(topic_name, &isRegex)) {
        if(isRegex) {
          char **reg_temp_ptr = rd_realloc(temp_reg_topics,
                                          (regex_topic_count+1)* sizeof(char *));
          if (reg_temp_ptr)
            temp_reg_topics = reg_temp_ptr;
          else
            continue;
          temp_reg_topics[regex_topic_count] = rd_strndup(topic_name,
                                                    strlen(topic_name));
          regex_topic_count++;
        } else {
          if (rk->rk_conf.topic_blacklist && 
              rd_kafka_pattern_match(rk->rk_conf.topic_blacklist, topic_name))
              continue;
          size_t size = (streams_topic_count+1)*sizeof(char *);
          char **temp_ptr = rd_realloc(temp_topics, size);
          if (temp_ptr )
            temp_topics = temp_ptr;
          else
            continue;
          temp_topics[streams_topic_count] = rd_strndup(topic_name,
                                                         strlen(topic_name));

          streams_topic_count++;
        }
    } else {
        kafka_topic_count++;
    }

    if ((regex_topic_count!=0 && streams_topic_count!=0)||
        (streams_topic_count!=0 && kafka_topic_count!=0)) {
      //Both streams and kafka topic names provided
      *streams_topics = temp_topics;
      *regex_topics = temp_reg_topics;
      *regex_count    = regex_topic_count;
      *streams_count  = streams_topic_count;
      return MIX_TOPICS;
    }
  }
  *streams_topics = temp_topics;
  *regex_topics = temp_reg_topics;
  *regex_count    = regex_topic_count;
  *streams_count  = streams_topic_count;
  if (regex_topic_count > 0 || streams_topic_count > 0  ) {
    return STREAMS_TOPICS;
  } else if(kafka_topic_count > 0 ) {
    return KAFKA_TOPICS;
  } else {
    return MIX_TOPICS;
  }
}

void streams_topic_partition_free (streams_topic_partition_t *tps,
				   int num_topics) {
	int i;
	for (i=0; i< num_topics; i++)
		streams_topic_partition_destroy(tps[i]);

	rd_free(tps);
}

int streams_get_topic_commit_info (rd_kafka_t *rk,
				   const rd_kafka_topic_partition_list_t *topics,
				   streams_topic_partition_t *tp,
				   int64_t *cursor,
				   uint32_t *tp_size ) {

	int streams_topic_count = 0;
	int kafka_topic_count = 0;
	int i;
	*tp_size = (uint32_t) topics->cnt;

	for (i=0; i < topics->cnt; i++) {
		const char *topic_name = (topics->elems[i]).topic;
    if (streams_is_valid_topic_name(topic_name, NULL)) {
			streams_topic_partition_create (topic_name,
				(topics->elems[i]).partition,
				 &tp[streams_topic_count]);
			cursor[streams_topic_count] = (topics->elems[i]).offset;
      streams_topic_count++;

		} else {
			kafka_topic_count++;
		}

		if (streams_topic_count!=0 && kafka_topic_count!=0) {
			*tp_size = (uint32_t) streams_topic_count;
			return MIX_TOPICS;
		}
	}

	if (streams_topic_count > 0 ) {
		*tp_size = (uint32_t) streams_topic_count;
		return STREAMS_TOPICS;
	} else if (kafka_topic_count > 0 ) {
	  *tp_size = (uint32_t) streams_topic_count;
    return KAFKA_TOPICS;
	} else {
                return MIX_TOPICS;
        }
}

void streams_populate_topic_partition_list(rd_kafka_t *rk,
					   streams_topic_partition_t *topic_partitions,
					   const int64_t *offsets,
					   uint32_t topic_partition_size,
					   rd_kafka_topic_partition_list_t *outList ) {
        uint32_t i = 0;
        for (i=0; i< topic_partition_size; i++) {
	        char *topic_name;
	        streams_topic_partition_get_topic_name (topic_partitions[i],
							&topic_name);
	        int32_t partition_id = RD_KAFKA_PARTITION_UA;
	        streams_topic_partition_get_partition_id(topic_partitions[i],
							 &partition_id);
		rd_kafka_topic_partition_t *rkt = rd_kafka_topic_partition_list_add (outList,
										     topic_name,
										     partition_id);

		if (offsets != NULL)
			rkt->offset = offsets[i];
	}
}

int streams_check_topic_name_is_valid (const char *topicName, uint32_t topicSz,
                                       bool *isRegex) {
  if (isRegex) *isRegex = false;

  if (topicSz < 1 || topicSz > 255)
    return -1;

  if (((topicSz == 1) && (topicName[0] == '.')) ||
      ((topicSz == 2) && (topicName[0] == '.') && (topicName[1] == '.'))) {
    return -1;
  }
  if (isRegex && topicName[0] == '^') {
        *isRegex = true;
        return 0;
  }
  uint32_t i ;
  for (i = 0; i < topicSz; i++){
    if (isalnum(topicName[i]) || topicName[i] == '.' ||
        topicName[i] == '_' || topicName[i] == '-' ) {
      continue;
    } else {
      return -1;
    }
  }

  return 0;
}

int streams_check_full_path_is_valid(const char *fullPath,
                     uint32_t pathSize,
                     bool *isRegex) {
  if (isRegex) *isRegex = false;
  if (pathSize < 1 || fullPath[0] != '/')
      return -1;
  char *topicName;
  if (streams_get_name_from_full_path( fullPath, pathSize, NULL, &topicName) == -1)
    return -1;
  int err = streams_check_topic_name_is_valid(topicName, strlen(topicName), isRegex);
  free(topicName);
  if (err)
    return err;

  return 0;
}
/*
* Utility function to get stream and topic name 
* \note
* \param[out] stream. Caller needs to free if SUCCESS(0) is returned.
* \param[out] topic. Caller needs to free if SUCCESS(0) is returned.
*/
int streams_get_name_from_full_path (const char*fullPath, uint32_t pathSize,
                                     char** stream, char **topic){
  uint32_t slashIdx = 0;
  uint32_t colonIdx = 0;
  // Look for the last '/' and first ':' after the last '/'l
  uint32_t i ;
  for ( i = pathSize - 1 ; i > 0; --i) {
    char curChar = fullPath[i];
    if (curChar == ':') {
      colonIdx = i;
    } else if (curChar == '/') {
      slashIdx = i;
      break;
    }
  }

  if (colonIdx == 0 || colonIdx - slashIdx <= 1)
    return -1;

  if(stream) {
    uint32_t streamLen = colonIdx;
    char *streamName;
    streamName = (char *) malloc(sizeof (char) * (streamLen + 1));
    memcpy( streamName, &fullPath[0], streamLen );
    streamName[streamLen] = '\0';
    *stream = streamName;
  }
  if(topic) {
    uint32_t topicLen = pathSize - colonIdx - 1;
    char *topicName;
    topicName = (char *) malloc(sizeof (char) * (topicLen + 1));
    memcpy( topicName, &fullPath[colonIdx +1], topicLen );
    topicName[topicLen] = '\0';
    *topic = topicName;
  }
  return 0;
}

/*
 * \brief Init streams_topic_partition_t data structures.
 * \pre irkt->streams_mtx lock is held
 * \post irkt->streams_mtx lock is held
 * \return ENOENT if there are no partitions.
 */
static int streams_init_topic_partition_objects(rd_kafka_itopic_t *irkt) {
    assert (irkt != NULL);
    assert (irkt->streams_tp_array == NULL);
    assert (irkt->streams_tp_default == NULL);

    irkt->streams_tp_default = NULL;

    streams_producer_get_num_partitions(irkt->rkt_rk->streams_producer,
                                        irkt->rkt_topic->str,
                                        &irkt->num_partitions);

    if (irkt->num_partitions == 0) {
        return ENOENT;
    }

    irkt->streams_tp_array = rd_calloc(irkt->num_partitions,
                                       sizeof(streams_topic_partition_t));
    return 0;
}

/*
 * \brief Destroy the streams_topic_partition_t objects for topic.
 * \pre irkt->streams_mtx lock is held
 * \post irkt->streams_mtx lock is held
 */
static void streams_destroy_topic_partition_objects(rd_kafka_itopic_t *irkt) {

    assert (irkt != NULL);

    if ((irkt->streams_tp_default == NULL) &&
        (irkt->streams_tp_array == NULL)) {
        assert (irkt->ref_cnt == 0);
        return;
    }

    while (irkt->ref_cnt > 0) {
        cnd_wait(&irkt->streams_cnd, &irkt->streams_mtx);
    }

    if (irkt->streams_tp_array) {
        int i = 0;
        for (i = 0; i < irkt->num_partitions; i++) {
            if (irkt->streams_tp_array[i]) {
                streams_topic_partition_destroy(irkt->streams_tp_array[i]);
            }
        }
        rd_free(irkt->streams_tp_array);
        irkt->streams_tp_array = NULL;
    }

    if (irkt->streams_tp_default) {
        streams_topic_partition_destroy(irkt->streams_tp_default);
        irkt->streams_tp_default = NULL;
    }
}

static void streams_init_topic(rd_kafka_itopic_t *irkt) {

    assert (irkt != NULL);
    irkt->is_streams_topic = true;
    mtx_init(&irkt->streams_mtx, mtx_plain);
    cnd_init(&irkt->streams_cnd);
    irkt->ref_cnt = 0;
    irkt->num_partitions = 0;
    irkt->streams_tp_array = NULL;
    irkt->streams_tp_default = NULL;
}


void streams_destroy_topic(rd_kafka_itopic_t *irkt) {
    assert (irkt != NULL);
    assert (is_streams_topic(irkt));
    assert (is_streams_user(irkt->rkt_rk));

    mtx_lock(&irkt->streams_mtx);
    streams_destroy_topic_partition_objects(irkt);
    mtx_unlock(&irkt->streams_mtx);

    mtx_destroy(&irkt->streams_mtx);
    cnd_destroy(&irkt->streams_cnd);
}

/*
 * \brief Get a streams_topic_partition object for a given partition / topic.
 * \param[in] topic
 * \param[in] partition Partition number in the topic.
 * \param[out] Pointer to stream partition topic object.
 * \return Returns a streams_topic_partition_t object.
 *         NULL if invalid input is provided.
 * \note Caller should release the topic_partition using
 *        streams_put_topic_partition.
 */
int
streams_get_topic_partition(rd_kafka_itopic_t *irkt, int32_t partition,
                            streams_topic_partition_t *tp) {

    assert (*tp == NULL);
    assert (irkt != NULL);
    assert (irkt->is_streams_topic == true);
    assert (is_streams_producer(irkt->rkt_rk));

    int ret = 0;

    mtx_lock(&irkt->streams_mtx);

    if (unlikely((partition >= irkt->num_partitions) ||
        (irkt->num_partitions == 0)) )  {
        streams_destroy_topic_partition_objects(irkt);
        ret = streams_init_topic_partition_objects(irkt);
    }

    if (ret != 0) {
        goto out;
    }

    if (partition < INVALID_PARTITION_ID ||
        partition >= irkt->num_partitions) {
        ret = EINVAL;
        goto out;
    }

    assert (irkt->ref_cnt >= 0);

    irkt->ref_cnt++;

    if (partition == INVALID_PARTITION_ID) {
        if (irkt->streams_tp_default == NULL) {
            streams_topic_partition_create(irkt->rkt_topic->str,
                    INVALID_PARTITION_ID, &irkt->streams_tp_default);
        }
        *tp = irkt->streams_tp_default;
    } else {
        if (irkt->streams_tp_array[partition] == NULL) {
            streams_topic_partition_create(irkt->rkt_topic->str,
                    partition, &irkt->streams_tp_array[partition]);
        }
        *tp = irkt->streams_tp_array[partition];
    }

  out:
    mtx_unlock(&irkt->streams_mtx);
    return ret;
}

/*
 * \brief Decrement the refcnt for topic partition objects.
 */
void
streams_put_topic_partition(rd_kafka_itopic_t *irkt) {
    assert (irkt != NULL);
    mtx_lock(&irkt->streams_mtx);
    assert (irkt->ref_cnt > 0);
    irkt->ref_cnt--;
    if (irkt->ref_cnt == 0) {
        cnd_signal(&irkt->streams_cnd);
    }
    mtx_unlock(&irkt->streams_mtx);
}

/*
 * \brief Check and init streams topic/producer.
 * \return 0 is success.
 *         EINVAL if invalid topic<->producer combination is used.
 */
int
streams_check_and_init_topic(rd_kafka_itopic_t *rkt) {
    // Streams specific.
    int ret = 0;

    if (streams_is_valid_topic_name(rkt->rkt_topic->str, NULL)) {
        // kafka user using to streams topic format.
        if (rkt->rkt_rk->is_kafka_user) {
            ret = EINVAL;
        } else {
            streams_init_topic(rkt);
            rkt->rkt_rk->is_streams_user = true;
        }
    } else {
        // Streams user using kafka topic format.
        if (is_streams_user(rkt->rkt_rk)) {
            ret = EINVAL;
        } else {
            rkt->is_streams_topic = false;
            rkt->rkt_rk->is_kafka_user = true;
        }
    }

    return ret;
}
