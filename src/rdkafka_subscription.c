/*
 * librdkafka - Apache Kafka C library
 *
 * Copyright (c) 2012-2015, Magnus Edenhill
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


/**
 * This is the high level consumer API which is mutually exclusive
 * with the old legacy simple consumer.
 * Only one of these interfaces may be used on a given rd_kafka_t handle.
 */

#include "rdkafka_int.h"
#include "rdkafka_subscription.h"
#include "streams_wrapper.h"


/**
 * Checks that a high level consumer can be added (no previous simple consumers)
 * Returns 1 if okay, else 0.
 *
 * A rd_kafka_t handle can never migrate from simple to high-level, or
 * vice versa, so we dont need a ..consumer_del().
 */
static RD_INLINE int rd_kafka_high_level_consumer_add (rd_kafka_t *rk) {
        if (rd_atomic32_get(&rk->rk_simple_cnt) > 0)
                return 0;

        rd_atomic32_sub(&rk->rk_simple_cnt, 1);
        return 1;
}


/* FIXME: Remove these */
rd_kafka_resp_err_t rd_kafka_subscribe_rkt (rd_kafka_itopic_t *rkt) {

        rd_kafka_topic_wrlock(rkt);
        /* FIXME: mark for subscription */
        rd_kafka_topic_wrunlock(rkt);

        return RD_KAFKA_RESP_ERR_NO_ERROR;
}


rd_kafka_resp_err_t rd_kafka_unsubscribe (rd_kafka_t *rk) {

  if(!rk)
    return RD_KAFKA_RESP_ERR__INVALID_ARG;
	if (is_streams_consumer(rk)) {
		return streams_consumer_unsubscribe((const streams_consumer_t)
                                         rk->streams_consumer);
	} else {
		rd_kafka_cgrp_t *rkcg;

		if (!(rkcg = rd_kafka_cgrp_get(rk)))
			return RD_KAFKA_RESP_ERR__UNKNOWN_GROUP;

	        return rd_kafka_op_err_destroy(rd_kafka_op_req2(&rkcg->rkcg_ops,
                                               RD_KAFKA_OP_SUBSCRIBE));
	}
}

static void streams_assign_rebalance_wrapper_cb (
		streams_topic_partition_t *topic_partitions,
		uint32_t topic_partition_size,
		void *ctx) {

	streams_consumer_callback_ctx *wrapper_cb_ctx =
                            (streams_consumer_callback_ctx *) ctx;
	rd_kafka_t *rk = wrapper_cb_ctx->rk;

	if (rk->rk_conf.rebalance_cb && topic_partition_size >0) {
		rd_kafka_topic_partition_list_t *assigned_tp_list = rd_kafka_topic_partition_list_new(1);
		streams_populate_topic_partition_list(rk,
						      topic_partitions,
						      NULL,
						      topic_partition_size,
						      assigned_tp_list);

		int err = RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS;
		rd_kafka_op_app(&(rk->rk_cgrp->rkcg_q),
				RD_KAFKA_OP_REBALANCE,
				RD_KAFKA_OP_F_FREE,
				NULL, err,
				rd_kafka_topic_partition_list_copy(assigned_tp_list),
				0,
				(void *)rd_kafka_topic_partition_list_destroy);

		rd_kafka_topic_partition_list_destroy(assigned_tp_list);
	}
	//TODO: DO NOT DESTROY WRAPPER CTX here, same context is used till the end of life of consumer.
};

static void streams_revoke_rebalance_wrapper_cb ( streams_topic_partition_t *topic_partitions,
						  uint32_t topic_partition_size,
						  void *ctx) {

	streams_consumer_callback_ctx *wrapper_cb_ctx = (streams_consumer_callback_ctx *) ctx;
	rd_kafka_t *rk = wrapper_cb_ctx->rk;

	if (rk->rk_conf.rebalance_cb && topic_partition_size > 0) {
		rd_kafka_topic_partition_list_t *revoked_tp_list = rd_kafka_topic_partition_list_new(1);
		streams_populate_topic_partition_list(rk,
						      topic_partitions,
						      NULL,
						      topic_partition_size,
						      revoked_tp_list);

		int err = RD_KAFKA_RESP_ERR__REVOKE_PARTITIONS;
		rd_kafka_op_app (&(rk->rk_cgrp->rkcg_q),
				RD_KAFKA_OP_REBALANCE,
				RD_KAFKA_OP_F_FREE,
				NULL, err,
				rd_kafka_topic_partition_list_copy(revoked_tp_list),
				0,
				(void *)rd_kafka_topic_partition_list_destroy);

		rd_kafka_topic_partition_list_destroy(revoked_tp_list);
	}
	//TODO: DO NOT DESTROY WRAPPER CTX here, same context is used till the end of life of consumer.
};

void streams_consumer_create_wrapper(rd_kafka_t *rk) {
	//create streams config
	streams_config_t config;
	streams_config_create(&config);
	char *group_id = (rk->rk_conf).group_id->str;
	if (group_id == NULL) {
		return;
  }

	streams_config_set(config, "group.id", group_id);
	//TODO: Set correct conf for auto.offset.reset
	streams_config_set(config, "auto.offset.reset", "earliest");
	char *autoCommit = "true";
	if(!(rk->rk_conf).enable_auto_commit)
		autoCommit = "false";
	streams_config_set(config, "enable.auto.commit", autoCommit);
	//create streams consumer
	streams_consumer_t consumer;
	streams_consumer_create(config, &consumer);

	//update corresponding rd_kafka_new object
	rk->streams_consumer = consumer;
}



void streams_topic_regex_list_free (char **strTopics, int strCount,
                                    char **regTopics, int regCount) {
    streams_topic_free(strTopics, strCount);
		streams_topic_free(regTopics, regCount);
}

bool streams_check_regex_for_same_stream_and_combine (char **regex_topics,
                                                      int count,
                                                      char **outRegex) {
  bool match = false;
  if(regex_topics){
    int i, err = 0;
    char *temp_reg_stream;
    char **temp_reg_topic= (char **)malloc (count * sizeof (char*));
    char *out;
    int totalLen = 0;

    for (i = 0; i < count; i++) {

      char *temp_stream;
      err = streams_get_name_from_full_path (regex_topics[i],
                                              strlen(regex_topics[i]),
                                              &temp_stream,
                                              &temp_reg_topic[i] );
      if(err) {
        streams_topic_free(temp_reg_topic, count);
        return false;
      }
      if(i == 0) {
        temp_reg_stream = temp_stream;
        totalLen += strlen(temp_reg_stream)+1; //+1 for ':' after stream name
      }
      if (strcmp (temp_reg_stream, temp_stream) == 0) {
          match = true;
          memmove(temp_reg_topic[i], temp_reg_topic[i]+1,  strlen(temp_reg_topic[i]));
          if(i == count-1)
            totalLen += strlen(temp_reg_topic[i]); // Last regex
          else
            totalLen += strlen(temp_reg_topic[i])+1; //+1 for '|'
      } else {
          match = false;
          break;
      }
    }
    if (match) {
      out = malloc (totalLen * sizeof(char));
      sprintf (out, "%s:", temp_reg_stream);
      for (i = 0; i < count; i++) {
          strcat(out, temp_reg_topic[i]);
          if(i != count-1)
            strcat(out, "|");
      }
    } else {
      count = i;
    } 

    *outRegex = out;
    streams_topic_free(temp_reg_topic, count);
  }
  return match;
}

void streams_get_topic_blacklist_for_stream (rd_kafka_pattern_list_t *blacklist,
                                             char *compareStream, char **outStream) {
    rd_kafka_pattern_t *rkpat;
    char *stream;
    char *topic;
    char *combinedStream = malloc (0);
    int count = 0;
    bool isBlackList = false;
    TAILQ_FOREACH(rkpat, &blacklist->rkpl_head, rkpat_link) {
      isBlackList = true;
      int err = 0;
      err  = streams_get_name_from_full_path(rkpat->rkpat_orig,
                                             strlen(rkpat->rkpat_orig),
                                             &stream, &topic);
      if (err || (strcmp (stream, compareStream) != 0))
        continue;

      if (count == 0) {
        combinedStream = realloc (combinedStream, sizeof (char) *
                                        (strlen(stream) + strlen (topic)+1));
        sprintf (combinedStream, "%s:%s", stream, topic);
      } else {
        combinedStream = realloc (combinedStream, sizeof (char) *
                                  (strlen(combinedStream) + strlen (topic)+1));
        strcat(combinedStream, "|");
        strcat(combinedStream, topic);
      }
      count ++;
      if (!err) {
        free (stream);
        free (topic);
      }
    }
    if(isBlackList) {
      *outStream = combinedStream;
    } else {
      *outStream = NULL;
      //free (combinedStream);
    }
}

rd_kafka_resp_err_t
streams_rd_kafka_subscribe_wrapper (rd_kafka_t *rk,
																		const rd_kafka_topic_partition_list_t *topics,
																		bool *is_kafka_subscribe) {
	*is_kafka_subscribe = false;
  char **streams_topics = rd_malloc(0);
  char **regex_topics = rd_malloc(0);
  char *regex = NULL;

  int streams_topic_count = 0;
  int regex_topic_count = 0;
  int topic_validity = streams_get_regex_topic_names(rk, topics,
        &streams_topics, &regex_topics,
        &streams_topic_count,
        &regex_topic_count);

	switch (topic_validity) {

	case -1:
    streams_topic_regex_list_free(streams_topics, streams_topic_count,
                                  regex_topics, regex_topic_count);
    return RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC;	// TODO: return proper error code

	case 0:
    if (rk->kafka_consumer) {
      streams_topic_regex_list_free(streams_topics, streams_topic_count,
                                  regex_topics, regex_topic_count);

      free(regex);
			return RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC;
		}
		//all streams topics
		if(regex_topic_count > 0) {
      if(!streams_check_regex_for_same_stream_and_combine (regex_topics,
                                                           regex_topic_count,
                                                           &regex)){
        streams_topic_regex_list_free(streams_topics, streams_topic_count,
                                             regex_topics, regex_topic_count);
        return RD_KAFKA_RESP_ERR__INVALID_ARG; // return new error code -
                                           // "not supported"
      }
    }
		if(!is_streams_consumer(rk)) {
			streams_consumer_create_wrapper (rk);
		}

		streams_consumer_callback_ctx *opaque_wrapper;
		size_t ctxlen = sizeof(*opaque_wrapper);
		opaque_wrapper = rd_malloc(ctxlen);
		opaque_wrapper->rk = rk;
    if (streams_topic_count > 0) {

		  streams_consumer_subscribe_topics((const streams_consumer_t) rk->streams_consumer,
              (const char**) streams_topics,
              topics->cnt,
              (const streams_rebalance_cb) streams_assign_rebalance_wrapper_cb,
              (const streams_rebalance_cb) streams_revoke_rebalance_wrapper_cb,
              (void *)opaque_wrapper);
    }
    if (regex_topic_count > 0) {
      if (regex) {
        char *str;
        char *blacklist;
        streams_get_name_from_full_path(regex, strlen(regex), &str, NULL);
        streams_get_topic_blacklist_for_stream (&rk->rk_conf.topic_blacklist,
                                                str, &blacklist);

        streams_consumer_subscribe_regex ((const streams_consumer_t) rk->streams_consumer,
              (const char*) regex,
              (const char*) blacklist,
              (const streams_rebalance_cb) streams_assign_rebalance_wrapper_cb,
              (const streams_rebalance_cb) streams_revoke_rebalance_wrapper_cb,
              (void *)opaque_wrapper);
      }
    }
		break;

	case 1:
		//all kafka topics
		streams_topic_regex_list_free(streams_topics, streams_topic_count,
                                  regex_topics, regex_topic_count);
    if (is_streams_consumer(rk))
			return RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC;
		else
			*is_kafka_subscribe = true;
      break;

	default:
    streams_topic_regex_list_free(streams_topics, streams_topic_count,
                                  regex_topics, regex_topic_count);
		rd_dassert (topic_validity >1 || topic_validity < -1);
		break;
	}

	return RD_KAFKA_RESP_ERR_NO_ERROR;
}

rd_kafka_resp_err_t
rd_kafka_subscribe (rd_kafka_t *rk,
		    const rd_kafka_topic_partition_list_t *topics) {
  if(!rk)
     return RD_KAFKA_RESP_ERR__INVALID_ARG;
	rd_kafka_resp_err_t err;
  rd_kafka_op_t *rko;
  rd_kafka_cgrp_t *rkcg;

	if (!(rkcg = rd_kafka_cgrp_get(rk)))
    return RD_KAFKA_RESP_ERR__UNKNOWN_GROUP;

	bool is_kafka_subscribe = false;
	err = streams_rd_kafka_subscribe_wrapper (rk, topics, &is_kafka_subscribe);
  if (is_kafka_subscribe) {
      rk->kafka_consumer = true;
			rko = rd_kafka_op_new(RD_KAFKA_OP_SUBSCRIBE);
			rd_kafka_op_payload_set(rko,
															rd_kafka_topic_partition_list_copy(topics),
															(void *)rd_kafka_topic_partition_list_destroy);

			err = rd_kafka_op_err_destroy(
							rd_kafka_op_req(&rkcg->rkcg_ops, rko, RD_POLL_INFINITE));
		}
  return err;
}

rd_kafka_resp_err_t
streams_rd_kafka_assign_wrapper (rd_kafka_t *rk,
                                const rd_kafka_topic_partition_list_t *topics,
                                bool *is_kafka_assign) {
  *is_kafka_assign = false;
  rd_kafka_resp_err_t err = 0;
  int streams_topic_count = 0;
  int kafka_topic_count = 0;
  int i;
  streams_topic_partition_t *streams_tps =
           rd_malloc(topics->cnt * sizeof(streams_topic_partition_t));
  //streams_topic_partition_t streams_tps[topics->cnt];
  for (i=0; i < topics->cnt; i++) {
    const char *topic_name =  (topics->elems[i]).topic;
    if (streams_is_valid_topic_name(topic_name, NULL)) {

      streams_topic_partition_create(topic_name, (topics->elems[i]).partition,
                                                  &streams_tps[i]);
      streams_topic_count++;
    } else {
      kafka_topic_count++;
    }

    if (streams_topic_count!=0 && kafka_topic_count!=0) {
        streams_topic_partition_free (streams_tps, streams_topic_count);
        return RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC;
    }
  }

  if (streams_topic_count > 0 ) {
      if (rk->kafka_consumer) {
          streams_topic_partition_free (streams_tps, streams_topic_count);
          return RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC;
      }
      //all streams topics
      if(!is_streams_consumer(rk)) {
          streams_consumer_create_wrapper (rk);
      }

      err = streams_consumer_assign_partitions (rk->streams_consumer,
                                        streams_tps, streams_topic_count);
      streams_topic_partition_free (streams_tps, streams_topic_count);
      return err;
  } else if (kafka_topic_count > 0 ) {
      streams_topic_partition_free (streams_tps, streams_topic_count);
      *is_kafka_assign = true;
      return 1;
  } else {
    return RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC;
  }

}

rd_kafka_resp_err_t
rd_kafka_assign (rd_kafka_t *rk,
                 const rd_kafka_topic_partition_list_t *partitions) {
        if (!rk)
          return RD_KAFKA_RESP_ERR__INVALID_ARG;
        rd_kafka_op_t *rko;
        rd_kafka_cgrp_t *rkcg;
        rd_kafka_resp_err_t err;
        if (!(rkcg = rd_kafka_cgrp_get(rk)))
                return RD_KAFKA_RESP_ERR__UNKNOWN_GROUP;
        bool is_kafka_assign = false;

        err = streams_rd_kafka_assign_wrapper (rk, partitions, &is_kafka_assign);
        if(is_kafka_assign) {

          if ( is_streams_consumer(rk))
              return RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC;

          rko = rd_kafka_op_new(RD_KAFKA_OP_ASSIGN);
          if (partitions)
                rd_kafka_op_payload_set(
                        rko,
                        rd_kafka_topic_partition_list_copy(partitions),
                        (void *)rd_kafka_topic_partition_list_destroy);

          return rd_kafka_op_err_destroy(
                rd_kafka_op_req(&rkcg->rkcg_ops, rko, RD_POLL_INFINITE));
        }
        return err;
}

rd_kafka_resp_err_t
streams_rd_kafka_assignment_wrapper (rd_kafka_t *rk,
                     rd_kafka_topic_partition_list_t **partitions) {
        if (!is_streams_consumer (rk))
           return RD_KAFKA_RESP_ERR__INVALID_ARG;

        uint32_t tCount = 0;
        streams_topic_partition_t *tps;
        int32_t ret = 0;
        ret = streams_consumer_get_assignment (rk->streams_consumer, &tps, &tCount);
        if(!*partitions)
              *partitions = rd_kafka_topic_partition_list_new(0);
        if(!ret) {
            streams_populate_topic_partition_list (rk, tps, NULL,
                                                  tCount, *partitions );
              return RD_KAFKA_RESP_ERR_NO_ERROR;
        } else {
          //TODO: return correct error after mapping is implemented
              return RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC;
        }
}

rd_kafka_resp_err_t
rd_kafka_assignment (rd_kafka_t *rk,
                     rd_kafka_topic_partition_list_t **partitions) {

        if (!rk)
          return RD_KAFKA_RESP_ERR__INVALID_ARG;
        rd_kafka_op_t *rko;
        rd_kafka_resp_err_t err;
        rd_kafka_cgrp_t *rkcg;

        if (!(rkcg = rd_kafka_cgrp_get(rk)))
                return RD_KAFKA_RESP_ERR__UNKNOWN_GROUP;
        if (is_streams_consumer (rk)) {
          err = streams_rd_kafka_assignment_wrapper (rk, partitions);
        } else {
          rko = rd_kafka_op_req2(&rkcg->rkcg_ops, RD_KAFKA_OP_GET_ASSIGNMENT);
            if (!rko)
              return RD_KAFKA_RESP_ERR__TIMED_OUT;

          err = rko->rko_err;

          *partitions = rko->rko_payload;
          rko->rko_payload = NULL;
          rd_kafka_op_destroy(rko);

          if (!*partitions && !err) {
                  /* Create an empty list for convenience of the caller */
                  *partitions = rd_kafka_topic_partition_list_new(0);
          }
        }
        return err;
}

rd_kafka_resp_err_t
streams_rd_kafka_subscription_wrapper (rd_kafka_t *rk,
                               rd_kafka_topic_partition_list_t **topics) {
  char **streams_topics;
  uint32_t tcount = 0;

  rd_kafka_resp_err_t err =   streams_consumer_get_subscription (rk->streams_consumer,
                                     &streams_topics, &tcount );
  if(!*topics)
    *topics = rd_kafka_topic_partition_list_new(0);
  if (!err) {
    uint32_t i = 0;
    for (i = 0; i < tcount ; i++) {
      rd_kafka_topic_partition_list_add
        (*topics, streams_topics[i], RD_KAFKA_PARTITION_UA);
    }
  }
  return err;
}

rd_kafka_resp_err_t
rd_kafka_subscription (rd_kafka_t *rk,
                       rd_kafka_topic_partition_list_t **topics){
        if(!rk)
          return RD_KAFKA_RESP_ERR__INVALID_ARG;
        if (rk->rk_type != RD_KAFKA_CONSUMER)
          return RD_KAFKA_RESP_ERR__INVALID_ARG;
        rd_kafka_op_t *rko;
        rd_kafka_resp_err_t err;
        rd_kafka_cgrp_t *rkcg;

        if (!(rkcg = rd_kafka_cgrp_get(rk)))
                return RD_KAFKA_RESP_ERR__UNKNOWN_GROUP;

        if (is_streams_consumer(rk)) {
                err = streams_rd_kafka_subscription_wrapper (rk, topics);
        } else {
          rko = rd_kafka_op_req2(&rkcg->rkcg_ops, RD_KAFKA_OP_GET_SUBSCRIPTION);
          if (!rko)
            return RD_KAFKA_RESP_ERR__TIMED_OUT;

          err = rko->rko_err;

          *topics = rko->rko_payload;
          rko->rko_payload = NULL;
          rd_kafka_op_destroy(rko);

          if (!*topics && !err) {
                /* Create an empty list for convenience of the caller */
                *topics = rd_kafka_topic_partition_list_new(0);
          }
        }
        return err;
}


rd_kafka_resp_err_t
rd_kafka_pause_partitions (rd_kafka_t *rk,
			   rd_kafka_topic_partition_list_t *partitions) {
	return rd_kafka_toppars_pause_resume(rk, 1, RD_KAFKA_TOPPAR_F_APP_PAUSE,
					     partitions);
}


rd_kafka_resp_err_t
rd_kafka_resume_partitions (rd_kafka_t *rk,
			   rd_kafka_topic_partition_list_t *partitions) {
	return rd_kafka_toppars_pause_resume(rk, 0, RD_KAFKA_TOPPAR_F_APP_PAUSE,
					     partitions);
}

