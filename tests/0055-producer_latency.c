/*
 * librdkafka - Apache Kafka C library
 *
 * Copyright (c) 2012-2022, Magnus Edenhill
 *               2025, Confluent Inc.
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

#include "test.h"
#include "rdkafka.h"
#include "../src/rdkafka_protocol.h"


#define _MSG_COUNT 10
struct latconf {
        const char *name;
        const char *conf[16];
        int min; /* Minimum expected latency */
        int max; /* Maximum expected latency */

        float rtt; /* Network+broker latency */


        char linger_ms_conf[32]; /**< Read back to show actual value */

        /* Result vector */
        rd_bool_t passed;
        float latency[_MSG_COUNT];
        float sum;
        int cnt;
        int wakeups;
};

static int tot_wakeups = 0;

static void
dr_msg_cb(rd_kafka_t *rk, const rd_kafka_message_t *rkmessage, void *opaque) {
        struct latconf *latconf = opaque;
        int64_t *ts_send        = (int64_t *)rkmessage->_private;
        float delivery_time;

        if (rkmessage->err)
                TEST_FAIL("%s: delivery failed: %s\n", latconf->name,
                          rd_kafka_err2str(rkmessage->err));

        if (!rkmessage->_private)
                return; /* Priming message, ignore. */

        delivery_time = (float)(test_clock() - *ts_send) / 1000.0f;

        free(ts_send);

        TEST_ASSERT(latconf->cnt < _MSG_COUNT, "");

        TEST_SAY("%s: Message %d delivered in %.3fms\n", latconf->name,
                 latconf->cnt, delivery_time);

        latconf->latency[latconf->cnt++] = delivery_time;
        latconf->sum += delivery_time;
}


/**
 * @brief A stats callback to get the per-broker wakeup counts.
 *
 * The JSON "parsing" here is crude..
 */
static int stats_cb(rd_kafka_t *rk, char *json, size_t json_len, void *opaque) {
        const char *t = json;
        int cnt       = 0;
        int total     = 0;

        /* Since we're only producing to one partition there will only be
         * one broker, the leader, who's wakeup counts we're interested in, but
         * we also want to know that other broker threads aren't spinning
         * like crazy. So just summarize all the wakeups from all brokers. */
        while ((t = strstr(t, "\"wakeups\":"))) {
                int wakeups;
                const char *next;

                t += strlen("\"wakeups\":");
                while (isspace((int)*t))
                        t++;
                wakeups = strtol(t, (char **)&next, 0);

                TEST_ASSERT(t != next, "No wakeup number found at \"%.*s...\"",
                            16, t);

                total += wakeups;
                cnt++;

                t = next;
        }

        TEST_ASSERT(cnt > 0, "No brokers found in stats");

        tot_wakeups = total;

        return 0;
}


static int verify_latency(struct latconf *latconf) {
        float avg;
        int fails = 0;
        double ext_overhead =
            latconf->rtt + 5.0 /* broker ProduceRequest handling time, maybe */;

        ext_overhead *= test_timeout_multiplier;

        avg = latconf->sum / (float)latconf->cnt;

        TEST_SAY(
            "%s: average latency %.3fms, allowed range %d..%d +%.0fms, "
            "%d wakeups\n",
            latconf->name, avg, latconf->min, latconf->max, ext_overhead,
            tot_wakeups);

        if (avg < (float)latconf->min ||
            avg > (float)latconf->max + ext_overhead) {
                TEST_FAIL_LATER(
                    "%s: average latency %.3fms is "
                    "outside range %d..%d +%.0fms",
                    latconf->name, avg, latconf->min, latconf->max,
                    ext_overhead);
                fails++;
        }

        latconf->wakeups = tot_wakeups;
        if (latconf->wakeups > 1000) {
                TEST_FAIL_LATER(
                    "%s: broker wakeups out of range: %d, "
                    "expected 5..1000",
                    latconf->name, latconf->wakeups);
                fails++;
        }


        return fails;
}

static void measure_rtt(struct latconf *latconf, rd_kafka_t *rk) {
        rd_kafka_resp_err_t err;
        const struct rd_kafka_metadata *md;
        int64_t ts = test_clock();

        err = rd_kafka_metadata(rk, 0, NULL, &md, tmout_multip(5000));
        TEST_ASSERT(!err, "%s", rd_kafka_err2str(err));
        latconf->rtt = (float)(test_clock() - ts) / 1000.0f;

        TEST_SAY("%s: broker base RTT is %.3fms\n", latconf->name,
                 latconf->rtt);
        rd_kafka_metadata_destroy(md);
}



static void test_producer_latency(const char *topic, struct latconf *latconf) {
        rd_kafka_t *rk;
        rd_kafka_conf_t *conf;
        rd_kafka_resp_err_t err;
        int i;
        size_t sz;
        rd_bool_t with_transactions = rd_false;

        SUB_TEST("%s (linger.ms=%d)", latconf->name);

        test_conf_init(&conf, NULL, 60);

        rd_kafka_conf_set_dr_msg_cb(conf, dr_msg_cb);
        rd_kafka_conf_set_opaque(conf, latconf);
        rd_kafka_conf_set_stats_cb(conf, stats_cb);
        test_conf_set(conf, "statistics.interval.ms", "100");
        tot_wakeups = 0;

        for (i = 0; latconf->conf[i]; i += 2) {
                TEST_SAY("%s:  set conf %s = %s\n", latconf->name,
                         latconf->conf[i], latconf->conf[i + 1]);
                test_conf_set(conf, latconf->conf[i], latconf->conf[i + 1]);
                if (!strcmp(latconf->conf[i], "transactional.id"))
                        with_transactions = rd_true;
        }

        sz = sizeof(latconf->linger_ms_conf);
        rd_kafka_conf_get(conf, "linger.ms", latconf->linger_ms_conf, &sz);

        rk = test_create_handle(RD_KAFKA_PRODUCER, conf);

        if (with_transactions) {
                TEST_CALL_ERROR__(rd_kafka_init_transactions(rk, 10 * 1000));
                TEST_CALL_ERROR__(rd_kafka_begin_transaction(rk));
        }

        TEST_SAY("%s: priming producer\n", latconf->name);
        /* Send a priming message to make sure everything is up
         * and functional before starting measurements */
        err = rd_kafka_producev(
            rk, RD_KAFKA_V_TOPIC(topic), RD_KAFKA_V_PARTITION(0),
            RD_KAFKA_V_VALUE("priming", 7),
            RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY), RD_KAFKA_V_END);
        if (err)
                TEST_FAIL("%s: priming producev failed: %s", latconf->name,
                          rd_kafka_err2str(err));

        if (with_transactions) {
                TEST_CALL_ERROR__(rd_kafka_commit_transaction(rk, -1));
        } else {
                /* Await delivery */
                rd_kafka_flush(rk, tmout_multip(5000));
        }

        /* Get a network+broker round-trip-time base time. */
        measure_rtt(latconf, rk);

        TEST_SAY("%s: producing %d messages\n", latconf->name, _MSG_COUNT);
        for (i = 0; i < _MSG_COUNT; i++) {
                int64_t *ts_send;
                int pre_cnt = latconf->cnt;

                if (with_transactions)
                        TEST_CALL_ERROR__(rd_kafka_begin_transaction(rk));

                ts_send  = malloc(sizeof(*ts_send));
                *ts_send = test_clock();

                err = rd_kafka_producev(
                    rk, RD_KAFKA_V_TOPIC(topic), RD_KAFKA_V_PARTITION(0),
                    RD_KAFKA_V_VALUE("hi", 2),
                    RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
                    RD_KAFKA_V_OPAQUE(ts_send), RD_KAFKA_V_END);
                if (err)
                        TEST_FAIL("%s: producev #%d failed: %s", latconf->name,
                                  i, rd_kafka_err2str(err));

                /* Await delivery */
                while (latconf->cnt == pre_cnt)
                        rd_kafka_poll(rk, 5000);

                if (with_transactions) {
                        test_timing_t timing;
                        TIMING_START(&timing, "commit_transaction");
                        TEST_CALL_ERROR__(rd_kafka_commit_transaction(rk, -1));
                        TIMING_ASSERT_LATER(&timing, 0,
                                            (int)(latconf->rtt + 50.0));
                }
        }

        while (tot_wakeups == 0)
                rd_kafka_poll(rk, 100); /* Get final stats_cb */

        rd_kafka_destroy(rk);

        if (verify_latency(latconf))
                return; /* verify_latency() has already
                         * called TEST_FAIL_LATER() */


        latconf->passed = rd_true;

        SUB_TEST_PASS();
}


static float find_min(const struct latconf *latconf) {
        int i;
        float v = 1000000;

        for (i = 0; i < latconf->cnt; i++)
                if (latconf->latency[i] < v)
                        v = latconf->latency[i];

        return v;
}

static float find_max(const struct latconf *latconf) {
        int i;
        float v = 0;

        for (i = 0; i < latconf->cnt; i++)
                if (latconf->latency[i] > v)
                        v = latconf->latency[i];

        return v;
}

int main_0055_producer_latency(int argc, char **argv) {
        const char *topic = test_mk_topic_name("0055_producer_latency", 1);
        struct latconf latconfs[] = {
            {"standard settings", {NULL}, 5, 5}, /* default is now 5ms */
            {"low linger.ms (0ms)", {"linger.ms", "0", NULL}, 0, 0},
            {"microsecond linger.ms (0.001ms)",
             {"linger.ms", "0.001", NULL},
             0,
             1},
            {"high linger.ms (3000ms)",
             {"linger.ms", "3000", NULL},
             3000,
             3100},
            {"linger.ms < 1000 (500ms)", /* internal block_max_ms */
             {"linger.ms", "500", NULL},
             500,
             600},
            {"no acks (0ms)",
             {"linger.ms", "0", "acks", "0", "enable.idempotence", "false",
              NULL},
             0,
             0},
            {"idempotence (10ms)",
             {"linger.ms", "10", "enable.idempotence", "true", NULL},
             10,
             10},
            {"transactions (35ms)",
             {"linger.ms", "35", "transactional.id", topic, NULL},
             35,
             50 + 35 /* extra time for AddPartitions..*/},
            {NULL}};
        struct latconf *latconf;

        if (test_on_ci) {
                TEST_SKIP("Latency measurements not reliable on CI\n");
                return 0;
        }

        /* Create topic without replicas to keep broker-side latency down */
        test_create_topic_wait_exists(NULL, topic, 1, 1, 5000);

        for (latconf = latconfs; latconf->name; latconf++)
                test_producer_latency(topic, latconf);

        TEST_SAY(_C_YEL "Latency tests summary:\n" _C_CLR);
        TEST_SAY("%-40s %9s  %6s..%-6s  %7s  %9s %9s %9s %8s\n", "Name",
                 "linger.ms", "MinExp", "MaxExp", "RTT", "Min", "Average",
                 "Max", "Wakeups");

        for (latconf = latconfs; latconf->name; latconf++)
                TEST_SAY("%-40s %9s  %6d..%-6d  %7g  %9g %9g %9g %8d%s\n",
                         latconf->name, latconf->linger_ms_conf, latconf->min,
                         latconf->max, latconf->rtt, find_min(latconf),
                         latconf->sum / latconf->cnt, find_max(latconf),
                         latconf->wakeups,
                         latconf->passed ? "" : _C_RED "  FAILED");


        TEST_LATER_CHECK("");

        return 0;
}

static void dr_msg_cb_first_message(rd_kafka_t *rk,
                                    const rd_kafka_message_t *rkmessage,
                                    void *opaque) {
        test_timing_t *t_produce = (test_timing_t *)rkmessage->_private;
        TIMING_STOP(t_produce);
        /* The reason for setting such a low value is that both the mcluster and
         * the producer are running locally, and there is no linger. This
         * prevents the test from passing spuriously. */
        TIMING_ASSERT_LATER(t_produce, 0, 100);

        TEST_ASSERT(rkmessage->err == RD_KAFKA_RESP_ERR_NO_ERROR,
                    "expected no error, got %s",
                    rd_kafka_err2str(rkmessage->err));
        TEST_ASSERT(rkmessage->offset == 0);
}

/**
 * Test producer latency of the first message to 50 different topics.
 *
 * Cases:
 * 0: rd_kafka_produce
 * 1: rd_kafka_producev
 * 2: rd_kafka_produceva
 * 3: rd_kafka_produce_batch
 */
static void test_producer_latency_first_message(int case_number) {
        rd_kafka_t *rk;
        rd_kafka_conf_t *conf;
        const char *topic;
        test_timing_t t_produce;
        rd_kafka_resp_err_t err;
        rd_kafka_mock_cluster_t *mcluster;
        rd_kafka_resp_err_t err_produce;
        const char *bootstrap_servers;
        const char *case_name = NULL;
        int i;
        size_t cnt;
        rd_kafka_mock_request_t **reqs = NULL;
        int metadata_request_cnt       = 0;
        const int iterations           = 50;

        if (case_number == 0) {
                case_name = "rd_kafka_produce";
        } else if (case_number == 1) {
                case_name = "rd_kafka_producev";
        } else if (case_number == 2) {
                case_name = "rd_kafka_produceva";
        } else if (case_number == 3) {
                case_name = "rd_kafka_produce_batch";
        }

        SUB_TEST("Starting test for %s", case_name);
        mcluster = test_mock_cluster_new(1, &bootstrap_servers);

        test_conf_init(&conf, NULL, 60);
        test_conf_set(conf, "bootstrap.servers", bootstrap_servers);
        test_conf_set(conf, "linger.ms", "0");
        rd_kafka_conf_set_dr_msg_cb(conf, dr_msg_cb_first_message);
        rk = test_create_handle(RD_KAFKA_PRODUCER, conf);

        rd_kafka_mock_start_request_tracking(mcluster);

        /* Run the case 50 times. */
        for (i = 0; i < iterations; i++) {
                topic = test_mk_topic_name("0055_producer_latency_mock", 1);
                rd_kafka_mock_topic_create(mcluster, topic, 1, 1);

                switch (case_number) {
                case 0: {
                        char *payload = "value";
                        rd_kafka_topic_t *rkt =
                            rd_kafka_topic_new(rk, topic, NULL);
                        int res;
                        TIMING_START(&t_produce, "Produce message");

                        res = rd_kafka_produce(rkt, 0, RD_KAFKA_MSG_F_COPY,
                                               payload, 5, NULL, 0, &t_produce);
                        rd_kafka_topic_destroy(rkt);
                        TEST_ASSERT(res == 0, "expected no error");
                        break;
                }
                case 1: {
                        TIMING_START(&t_produce, "Produce message");
                        err_produce = rd_kafka_producev(
                            rk, RD_KAFKA_V_TOPIC(topic),
                            RD_KAFKA_V_PARTITION(0),
                            RD_KAFKA_V_VALUE("value", 5),
                            RD_KAFKA_V_OPAQUE((&t_produce)), RD_KAFKA_V_END);
                        TEST_ASSERT(err_produce == RD_KAFKA_RESP_ERR_NO_ERROR,
                                    "expected no error, got %s",
                                    rd_kafka_err2str(err_produce));
                        break;
                }
                case 2: {
                        rd_kafka_vu_t vus[3];
                        vus[0].vtype      = RD_KAFKA_VTYPE_TOPIC;
                        vus[0].u.cstr     = topic;
                        vus[1].vtype      = RD_KAFKA_VTYPE_VALUE;
                        vus[1].u.mem.ptr  = "value";
                        vus[1].u.mem.size = 5;
                        vus[2].vtype      = RD_KAFKA_VTYPE_OPAQUE;
                        vus[2].u.ptr      = &t_produce;

                        TIMING_START(&t_produce, "Produce message");
                        TEST_CALL_ERROR__(rd_kafka_produceva(rk, vus, 3));
                        break;
                }
                case 3: {
                        rd_kafka_message_t rkmessages[1] = {0};
                        rd_kafka_topic_t *rkt =
                            rd_kafka_topic_new(rk, topic, NULL);
                        int res;

                        rkmessages[0].payload  = "value";
                        rkmessages[0].len      = 5;
                        rkmessages[0]._private = &t_produce;

                        TIMING_START(&t_produce, "Produce message");
                        res = rd_kafka_produce_batch(
                            rkt, 0, RD_KAFKA_MSG_F_COPY, rkmessages, 1);
                        rd_kafka_topic_destroy(rkt);
                        TEST_ASSERT(res == 1, "expected 1 msg enqueued, got %d",
                                    res);
                        break;
                }
                }
                rd_kafka_poll(rk, 0);
                err = rd_kafka_flush(rk, 10000);
                TEST_ASSERT(err == RD_KAFKA_RESP_ERR_NO_ERROR,
                            "expected all messages to be flushed, got %s",
                            rd_kafka_err2str(err));

                /* The topic_scan_all timer runs every 1 second. Originally, the
                 * issue was that the topic metadata was only fetched on the
                 * topic scan rather than when we were queueing the message.
                 * Running a loop and waiting 1ms between iterations means we
                 * will avoid passing tests due to coincidence. */
                rd_usleep(1 * 1000ll, 0);
        }
        rd_kafka_destroy(rk);

        reqs = rd_kafka_mock_get_requests(mcluster, &cnt);
        for (i = 0; i < (int)cnt; i++) {
                if (rd_kafka_mock_request_api_key(reqs[i]) ==
                    RD_KAFKAP_Metadata) {
                        metadata_request_cnt++;
                }
        }
        TEST_ASSERT(metadata_request_cnt == iterations,
                    "Expected exactly %d MetadataRequest, got %d", iterations,
                    metadata_request_cnt);
        rd_kafka_mock_request_destroy_array(reqs, cnt);
        rd_kafka_mock_stop_request_tracking(mcluster);

        test_mock_cluster_destroy(mcluster);

        TEST_LATER_CHECK();
        SUB_TEST_PASS();
}

int main_0055_producer_latency_mock(int argc, char **argv) {
        TEST_SKIP_MOCK_CLUSTER(0);

        int case_number;
        for (case_number = 0; case_number < 4; case_number++) {
                test_producer_latency_first_message(case_number);
        }

        return 0;
}
