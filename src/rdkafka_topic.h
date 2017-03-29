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

#pragma once

#include "rdlist.h"

extern const char *rd_kafka_topic_state_names[];


/* rd_kafka_itopic_t: internal representation of a topic */
struct rd_kafka_itopic_s {
	TAILQ_ENTRY(rd_kafka_itopic_s) rkt_link;

	rd_refcnt_t        rkt_refcnt;

	rwlock_t           rkt_lock;
	rd_kafkap_str_t   *rkt_topic;

	shptr_rd_kafka_toppar_t  *rkt_ua;  /* unassigned partition */
	shptr_rd_kafka_toppar_t **rkt_p;
	int32_t            rkt_partition_cnt;

        rd_list_t          rkt_desp;              /* Desired partitions
                                                   * that are not yet seen
                                                   * in the cluster. */

	rd_ts_t            rkt_ts_metadata; /* Timestamp of last metadata
					     * update for this topic. */


    mtx_t              rkt_app_lock;    /* Protects rkt_app_* */
    rd_kafka_topic_t *rkt_app_rkt;      /* A shared topic pointer
                                         * to be used for callbacks
                                         * to the application. */

    int               rkt_app_refcnt;   /* Number of active rkt's new()ed
                                         * by application. */

	enum {
		RD_KAFKA_TOPIC_S_UNKNOWN,   /* No cluster information yet */
		RD_KAFKA_TOPIC_S_EXISTS,    /* Topic exists in cluster */
		RD_KAFKA_TOPIC_S_NOTEXISTS, /* Topic is not known in cluster */
	} rkt_state;

        int                rkt_flags;
#define RD_KAFKA_TOPIC_F_LEADER_QUERY  0x1 /* There is an outstanding
                                            * leader query for this topic */
	rd_kafka_t       *rkt_rk;

        shptr_rd_kafka_itopic_t *rkt_shptr_app; /* Application's topic_new() */

	rd_kafka_topic_conf_t rkt_conf;

	bool is_streams_topic;  /* Is this MapR streams topic */
	int32_t num_partitions;  /* Number of partitions in this topic */
	streams_topic_partition_t *streams_tp_array;/* Streams topic partition
	                                              * array */
	streams_topic_partition_t streams_tp_default;/* Streams topic partition for
	                                              * INVALID_PARTITION_ID (-1) */
	uint32_t ref_cnt; /* Ref cnt for number of references to streams
	                   * topic partition objects */
	mtx_t streams_mtx; /* Mtx protecting streams topic data structures */
	cnd_t streams_cnd;
};

#define rd_kafka_topic_rdlock(rkt)     rwlock_rdlock(&(rkt)->rkt_lock)
#define rd_kafka_topic_wrlock(rkt)     rwlock_wrlock(&(rkt)->rkt_lock)
#define rd_kafka_topic_rdunlock(rkt)   rwlock_rdunlock(&(rkt)->rkt_lock)
#define rd_kafka_topic_wrunlock(rkt)   rwlock_wrunlock(&(rkt)->rkt_lock)


/* Converts a shptr..itopic_t to an internal itopic_t */
#define rd_kafka_topic_s2i(s_rkt) rd_shared_ptr_obj(s_rkt)

/* Converts an application topic_t (a shptr topic) to an internal itopic_t */
#define rd_kafka_topic_a2i(app_rkt) \
        rd_kafka_topic_s2i((shptr_rd_kafka_itopic_t *)app_rkt)

/* Converts a shptr..itopic_t to an app topic_t (they are the same thing) */
#define rd_kafka_topic_s2a(s_rkt) ((rd_kafka_topic_t *)(s_rkt))

/* Converts an app topic_t to a shptr..itopic_t (they are the same thing) */
#define rd_kafka_topic_a2s(app_rkt) ((shptr_rd_kafka_itopic_t *)(app_rkt))





/**
 * Returns a shared pointer for the topic.
 */
#define rd_kafka_topic_keep(rkt) \
        rd_shared_ptr_get(rkt, &(rkt)->rkt_refcnt, shptr_rd_kafka_itopic_t)

/* Same, but casts to an app topic_t */
#define rd_kafka_topic_keep_a(rkt)                                      \
        ((rd_kafka_topic_t *)rd_shared_ptr_get(rkt, &(rkt)->rkt_refcnt, \
                                               shptr_rd_kafka_itopic_t))

/**
 * Frees a shared pointer previously returned by ..topic_keep()
 */
#define rd_kafka_topic_destroy0(s_rkt)                                  \
        rd_shared_ptr_put(s_rkt,                                        \
                          &rd_kafka_topic_s2i(s_rkt)->rkt_refcnt,       \
                          rd_kafka_topic_destroy_final(                 \
                                  rd_kafka_topic_s2i(s_rkt)))

void rd_kafka_topic_destroy_final (rd_kafka_itopic_t *rkt);


shptr_rd_kafka_itopic_t *rd_kafka_topic_new0 (rd_kafka_t *rk, const char *topic,
                                              rd_kafka_topic_conf_t *conf,
                                              int *existing, int do_lock);

shptr_rd_kafka_itopic_t *rd_kafka_topic_find_fl (const char *func, int line,
                                                 rd_kafka_t *rk,
                                                 const char *topic,
                                                 int do_lock);
shptr_rd_kafka_itopic_t *rd_kafka_topic_find0_fl (const char *func, int line,
                                                  rd_kafka_t *rk,
                                                  const rd_kafkap_str_t *topic);
#define rd_kafka_topic_find(rk,topic,do_lock)                           \
        rd_kafka_topic_find_fl(__FUNCTION__,__LINE__,rk,topic,do_lock)
#define rd_kafka_topic_find0(rk,topic)                                  \
        rd_kafka_topic_find0_fl(__FUNCTION__,__LINE__,rk,topic)

void rd_kafka_topic_partitions_remove (rd_kafka_itopic_t *rkt);

void rd_kafka_topic_metadata_none (rd_kafka_itopic_t *rkt);

int rd_kafka_topic_metadata_update (rd_kafka_broker_t *rkb,
				    const struct rd_kafka_metadata_topic *mdt);

int rd_kafka_topic_scan_all (rd_kafka_t *rk, rd_ts_t now);


// Streams specific methods.
bool is_streams_topic (rd_kafka_itopic_t *irkt);

bool streams_is_valid_topic_name (const char * topic_name, bool *isRegex);

int streams_get_topic_partition_list (rd_kafka_t *rk,
                                      const rd_kafka_topic_partition_list_t *topics,
                                      streams_topic_partition_t **streams_tps,
                                      int *streams_count);

int streams_get_topic_names (const rd_kafka_topic_partition_list_t *topics,
			     char ** streams_topics,
					 int *tcount);
int streams_get_regex_topic_names (rd_kafka_t *rk,
                                   const rd_kafka_topic_partition_list_t *topics,
                                   char ***streams_topics, char ***regex_topics,
                                   int *streams_count, int*regex_count);
int streams_get_topic_commit_info (rd_kafka_t *rk,
				   const rd_kafka_topic_partition_list_t *topics,
				   streams_topic_partition_t *tp,
				   int64_t *cursor,
				   uint32_t *tp_size );

void streams_topic_partition_free (streams_topic_partition_t *tps,
				   int num_topics);

void streams_topic_free (char **streams_topics,
		       int num_topics);
void streams_populate_topic_partition_list (rd_kafka_t *rk,
					    streams_topic_partition_t *topic_partitions,
					    const int64_t *offsets,
					    uint32_t topic_partition_size,
					    rd_kafka_topic_partition_list_t *outList );

int
streams_get_topic_partition(rd_kafka_itopic_t *irkt, int32_t partition,
                            streams_topic_partition_t *tp);

void
streams_put_topic_partition(rd_kafka_itopic_t *irkt);

int streams_check_and_init_topic(rd_kafka_itopic_t *irkt);
void streams_destroy_topic(rd_kafka_itopic_t *irkt);
