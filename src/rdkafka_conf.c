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

#include "rdkafka_int.h"
#include "rd.h"

#include <stdlib.h>
#include <ctype.h>
#include <stddef.h>

#include "rdkafka_int.h"
#include "rdkafka_feature.h"

static bool is_streams_compatible = false;

struct rd_kafka_property {
	rd_kafka_conf_scope_t scope;
	const char *name;
	enum {
		_RK_C_STR,
		_RK_C_INT,
		_RK_C_S2I,  /* String to Integer mapping.
			     * Supports limited canonical str->int mappings
			     * using s2i[] */
		_RK_C_S2F,  /* CSV String to Integer flag mapping (OR:ed) */
		_RK_C_BOOL,
		_RK_C_PTR,  /* Only settable through special set functions */
                _RK_C_PATLIST, /* Pattern list */
                _RK_C_KSTR, /* Kafka string */
		_RK_C_ALIAS /* Alias: points to other property through .sdef */
	} type;
	int   offset;
	const char *desc;
	int   vmin;
	int   vmax;
	int   vdef;        /* Default value (int) */
	const char *sdef;  /* Default value (string) */
        void  *pdef;       /* Default value (pointer) */
	struct {
		int val;
		const char *str;
	} s2i[16];  /* _RK_C_S2I and _RK_C_S2F */

	/* Value validator (STR) */
	int (*validate) (const struct rd_kafka_property *prop,
			 const char *val, int ival);
};


#define _RK(field)  offsetof(rd_kafka_conf_t, field)
#define _RKT(field) offsetof(rd_kafka_topic_conf_t, field)



/**
 * @brief Validate \p broker.version.fallback property.
 */
static int
rd_kafka_conf_validate_broker_version (const struct rd_kafka_property *prop,
				       const char *val, int ival) {
	struct rd_kafka_ApiVersion *apis;
	size_t api_cnt;
	return rd_kafka_get_legacy_ApiVersions(val, &apis, &api_cnt, NULL);
}

/**
 * @brief Validate that string is a single item, without delimters (, space).
 */
static int
rd_kafka_conf_validate_single (const struct rd_kafka_property *prop,
				const char *val, int ival) {
	return !strchr(val, ',') && !strchr(val, ' ');
}


/**
 * librdkafka configuration property definitions.
 */
static const struct rd_kafka_property rd_kafka_properties[] = {
	/* Global properties */
	{ _RK_GLOBAL, "builtin.features", _RK_C_S2F, _RK(builtin_features),
	"Indicates the builtin features for this build of librdkafka. "
	"An application can either query this value or attempt to set it "
	"with its list of required features to check for library support.",
	0, 0x7fffffff, 0xff,
	.s2i = {
#if WITH_ZLIB
		{ 0x1, "gzip" },
#endif
#if WITH_SNAPPY
		{ 0x2, "snappy" },
#endif
#if WITH_SSL
		{ 0x4, "ssl" },
#endif
#if WITH_SASL
		{ 0x8, "sasl" },
#endif
#if HAVE_REGEX
		{ 0x10, "regex" },
#endif
#if WITH_LZ4
		{ 0x20, "lz4" },
#endif
		{ 0, NULL }
		}
	},
	{ _RK_GLOBAL, "client.id", _RK_C_STR, _RK(client_id_str),
	  "Client identifier.",
	  .sdef =  "rdkafka" },
	{ _RK_GLOBAL, "metadata.broker.list", _RK_C_STR, _RK(brokerlist),
	  "Initial list of brokers. "
	  "The application may also use `rd_kafka_brokers_add()` to add "
	  "brokers during runtime." },
	{ _RK_GLOBAL, "bootstrap.servers", _RK_C_ALIAS, 0,
	  "See metadata.broker.list",
	  .sdef = "metadata.broker.list" },
	{ _RK_GLOBAL, "message.max.bytes", _RK_C_INT, _RK(max_msg_size),
	  "Maximum transmit message size.",
	  1000, 1000000000, 1000000 },
	{ _RK_GLOBAL, "receive.message.max.bytes", _RK_C_INT,
          _RK(recv_max_msg_size),
	  "Maximum receive message size. "
	  "This is a safety precaution to avoid memory exhaustion in case of "
	  "protocol hickups. "
          "The value should be at least fetch.message.max.bytes * number of "
          "partitions consumed from + messaging overhead (e.g. 200000 bytes).",
	  1000, 1000000000, 100000000 },
	{ _RK_GLOBAL, "max.in.flight.requests.per.connection", _RK_C_INT,
	  _RK(max_inflight),
	  "Maximum number of in-flight requests the client will send. "
	  "This setting applies per broker connection.",
	  1, 1000000, 1000000 },
	{ _RK_GLOBAL, "metadata.request.timeout.ms", _RK_C_INT,
	  _RK(metadata_request_timeout_ms),
	  "Non-topic request timeout in milliseconds. "
	  "This is for metadata requests, etc.",
	  10, 900*1000, 60*1000},
	{ _RK_GLOBAL, "topic.metadata.refresh.interval.ms", _RK_C_INT,
	  _RK(metadata_refresh_interval_ms),
	  "Topic metadata refresh interval in milliseconds. "
	  "The metadata is automatically refreshed on error and connect. "
	  "Use -1 to disable the intervalled refresh.",
	  -1, 3600*1000, 5*60*1000 },
	{ _RK_GLOBAL, "topic.metadata.refresh.fast.cnt", _RK_C_INT,
	  _RK(metadata_refresh_fast_cnt),
	  "When a topic looses its leader this number of metadata requests "
	  "are sent with `topic.metadata.refresh.fast.interval.ms` interval "
	  "disregarding the `topic.metadata.refresh.interval.ms` value. "
	  "This is used to recover quickly from transitioning leader brokers.",
	  0, 1000, 10 },
	{ _RK_GLOBAL, "topic.metadata.refresh.fast.interval.ms", _RK_C_INT,
	  _RK(metadata_refresh_fast_interval_ms),
	  "See `topic.metadata.refresh.fast.cnt` description",
	  1, 60*1000, 250 },
        { _RK_GLOBAL, "topic.metadata.refresh.sparse", _RK_C_BOOL,
          _RK(metadata_refresh_sparse),
          "Sparse metadata requests (consumes less network bandwidth)",
          0, 1, 1 },
        { _RK_GLOBAL, "topic.blacklist", _RK_C_PATLIST,
          _RK(topic_blacklist),
          "Topic blacklist, a comma-separated list of regular expressions "
          "for matching topic names that should be ignored in "
          "broker metadata information as if the topics did not exist." },
	{ _RK_GLOBAL, "debug", _RK_C_S2F, _RK(debug),
	  "A comma-separated list of debug contexts to enable.",
	  .s2i = {
                        { RD_KAFKA_DBG_GENERIC,  "generic" },
			{ RD_KAFKA_DBG_BROKER,   "broker" },
			{ RD_KAFKA_DBG_TOPIC,    "topic" },
			{ RD_KAFKA_DBG_METADATA, "metadata" },
			{ RD_KAFKA_DBG_PRODUCER, "producer" },
			{ RD_KAFKA_DBG_QUEUE,    "queue" },
			{ RD_KAFKA_DBG_MSG,      "msg" },
			{ RD_KAFKA_DBG_PROTOCOL, "protocol" },
                        { RD_KAFKA_DBG_CGRP,     "cgrp" },
			{ RD_KAFKA_DBG_SECURITY, "security" },
			{ RD_KAFKA_DBG_FETCH,    "fetch" },
			{ RD_KAFKA_DBG_ALL,      "all" },
		} },
	{ _RK_GLOBAL, "socket.timeout.ms", _RK_C_INT, _RK(socket_timeout_ms),
	  "Timeout for network requests.",
	  10, 300*1000, 60*1000 },
	{ _RK_GLOBAL, "socket.blocking.max.ms", _RK_C_INT,
	  _RK(socket_blocking_max_ms),
	  "Maximum time a broker socket operation may block. "
          "A lower value improves responsiveness at the expense of "
          "slightly higher CPU usage.",
	  1, 60*1000, 100 },
	{ _RK_GLOBAL, "socket.send.buffer.bytes", _RK_C_INT,
	  _RK(socket_sndbuf_size),
	  "Broker socket send buffer size. System default is used if 0.",
	  0, 100000000, 0 },
	{ _RK_GLOBAL, "socket.receive.buffer.bytes", _RK_C_INT,
	  _RK(socket_rcvbuf_size),
	  "Broker socket receive buffer size. System default is used if 0.",
	  0, 100000000, 0 },
	{ _RK_GLOBAL, "socket.keepalive.enable", _RK_C_BOOL,
	  _RK(socket_keepalive),
          "Enable TCP keep-alives (SO_KEEPALIVE) on broker sockets",
          0, 1, 0 },
        { _RK_GLOBAL, "socket.max.fails", _RK_C_INT,
          _RK(socket_max_fails),
          "Disconnect from broker when this number of send failures "
          "(e.g., timed out requests) is reached. Disable with 0. "
          "NOTE: The connection is automatically re-established.",
          0, 1000000, 3 },
	{ _RK_GLOBAL, "broker.address.ttl", _RK_C_INT,
	  _RK(broker_addr_ttl),
	  "How long to cache the broker address resolving "
          "results (milliseconds).",
	  0, 86400*1000, 1*1000 },
        { _RK_GLOBAL, "broker.address.family", _RK_C_S2I,
          _RK(broker_addr_family),
          "Allowed broker IP address families: any, v4, v6",
          .vdef = AF_UNSPEC,
          .s2i = {
                        { AF_UNSPEC, "any" },
                        { AF_INET, "v4" },
                        { AF_INET6, "v6" },
                } },
        { _RK_GLOBAL, "reconnect.backoff.jitter.ms", _RK_C_INT,
          _RK(reconnect_jitter_ms),
          "Throttle broker reconnection attempts by this value +-50%.",
          0, 60*60*1000, 500 },
	{ _RK_GLOBAL, "statistics.interval.ms", _RK_C_INT,
	  _RK(stats_interval_ms),
	  "librdkafka statistics emit interval. The application also needs to "
	  "register a stats callback using `rd_kafka_conf_set_stats_cb()`. "
	  "The granularity is 1000ms. A value of 0 disables statistics.",
	  0, 86400*1000, 0 },
	{ _RK_GLOBAL, "error_cb", _RK_C_PTR,
	  _RK(error_cb),
	  "Error callback (set with rd_kafka_conf_set_error_cb())" },
	{ _RK_GLOBAL, "throttle_cb", _RK_C_PTR,
	  _RK(throttle_cb),
	  "Throttle callback (set with rd_kafka_conf_set_throttle_cb())" },
	{ _RK_GLOBAL, "stats_cb", _RK_C_PTR,
	  _RK(stats_cb),
	  "Statistics callback (set with rd_kafka_conf_set_stats_cb())" },
	{ _RK_GLOBAL, "log_cb", _RK_C_PTR,
	  _RK(log_cb),
	  "Log callback (set with rd_kafka_conf_set_log_cb())",
          .pdef = rd_kafka_log_print },
        { _RK_GLOBAL, "log_level", _RK_C_INT,
          _RK(log_level),
          "Logging level (syslog(3) levels)",
          0, 7, 6 },
	{ _RK_GLOBAL, "log.thread.name", _RK_C_BOOL,
	  _RK(log_thread_name),
	  "Print internal thread name in log messages "
	  "(useful for debugging librdkafka internals)",
	  0, 1, 0 },
	{ _RK_GLOBAL, "log.connection.close", _RK_C_BOOL,
	  _RK(log_connection_close),
	  "Log broker disconnects. "
          "It might be useful to turn this off when interacting with "
          "0.9 brokers with an aggressive `connection.max.idle.ms` value.",
	  0, 1, 1 },
        { _RK_GLOBAL, "socket_cb", _RK_C_PTR,
          _RK(socket_cb),
          "Socket creation callback to provide race-free CLOEXEC",
          .pdef =
#ifdef __linux__
          rd_kafka_socket_cb_linux
#else
          rd_kafka_socket_cb_generic
#endif
        },
        { _RK_GLOBAL, "open_cb", _RK_C_PTR,
          _RK(open_cb),
          "File open callback to provide race-free CLOEXEC",
          .pdef =
#ifdef __linux__
          rd_kafka_open_cb_linux
#else
          rd_kafka_open_cb_generic
#endif
        },
	{ _RK_GLOBAL, "opaque", _RK_C_PTR,
	  _RK(opaque),
	  "Application opaque (set with rd_kafka_conf_set_opaque())" },
        { _RK_GLOBAL, "default_topic_conf", _RK_C_PTR,
          _RK(topic_conf),
          "Default topic configuration for automatically subscribed topics" },
	{ _RK_GLOBAL, "internal.termination.signal", _RK_C_INT,
	  _RK(term_sig),
	  "Signal that librdkafka will use to quickly terminate on "
	  "rd_kafka_destroy(). If this signal is not set then there will be a "
	  "delay before rd_kafka_wait_destroyed() returns true "
	  "as internal threads are timing out their system calls. "
	  "If this signal is set however the delay will be minimal. "
	  "The application should mask this signal as an internal "
	  "signal handler is installed.",
	  0, 128, 0 },
	{ _RK_GLOBAL, "api.version.request", _RK_C_BOOL,
	  _RK(api_version_request),
	  "Request broker's supported API versions to adjust functionality to "
	  "available protocol features. If set to false the fallback version "
	  "`broker.version.fallback` will be used. "
	  "**NOTE**: Depends on broker version >=0.10.0. If the request is not "
	  "supported by (an older) broker the `broker.version.fallback` fallback is used.",
	  0, 1, 0 },
	{ _RK_GLOBAL, "api.version.fallback.ms", _RK_C_INT,
	  _RK(api_version_fallback_ms),
	  "Dictates how long the `broker.version.fallback` fallback is used "
	  "in the case the ApiVersionRequest fails. "
	  "**NOTE**: The ApiVersionRequest is only issued when a new connection "
	  "to the broker is made (such as after an upgrade).",
	  0, 86400*7*1000, 20*60*1000 /* longer than default Idle timeout (10m)*/ },

	{ _RK_GLOBAL, "broker.version.fallback", _RK_C_STR,
	  _RK(broker_version_fallback),
	  "Older broker versions (<0.10.0) provides no way for a client to query "
	  "for supported protocol features "
	  "(ApiVersionRequest, see `api.version.request`) making it impossible "
	  "for the client to know what features it may use. "
	  "As a workaround a user may set this property to the expected broker "
	  "version and the client will automatically adjust its feature set "
	  "accordingly if the ApiVersionRequest fails (or is disabled). "
	  "The fallback broker version will be used for `api.version.fallback.ms`. "
	  "Valid values are: 0.9.0, 0.8.2, 0.8.1, 0.8.0.",
	  .sdef = "0.9.0",
	  .validate = rd_kafka_conf_validate_broker_version },

	/* Security related global properties */
	{ _RK_GLOBAL, "security.protocol", _RK_C_S2I,
	  _RK(security_protocol),
	  "Protocol used to communicate with brokers.",
	  .vdef = RD_KAFKA_PROTO_PLAINTEXT,
	  .s2i = {
			{ RD_KAFKA_PROTO_PLAINTEXT, "plaintext" },
#if WITH_SSL
			{ RD_KAFKA_PROTO_SSL, "ssl" },
#endif
#if WITH_SASL
			{ RD_KAFKA_PROTO_SASL_PLAINTEXT, "sasl_plaintext" },
#if WITH_SSL
			{ RD_KAFKA_PROTO_SASL_SSL, "sasl_ssl" },
#endif
#endif
			{ 0, NULL }
		} },

#if WITH_SSL
	{ _RK_GLOBAL, "ssl.cipher.suites", _RK_C_STR,
	  _RK(ssl.cipher_suites),
	  "A cipher suite is a named combination of authentication, "
	  "encryption, MAC and key exchange algorithm used to negotiate the "
	  "security settings for a network connection using TLS or SSL network "
	  "protocol. See manual page for `ciphers(1)` and "
	  "`SSL_CTX_set_cipher_list(3)."
	},
	{ _RK_GLOBAL, "ssl.key.location", _RK_C_STR,
	  _RK(ssl.key_location),
	  "Path to client's private key (PEM) used for authentication."
	},
	{ _RK_GLOBAL, "ssl.key.password", _RK_C_STR,
	  _RK(ssl.key_password),
	  "Private key passphrase"
	},
	{ _RK_GLOBAL, "ssl.certificate.location", _RK_C_STR,
	  _RK(ssl.cert_location),
	  "Path to client's public key (PEM) used for authentication."
	},
	{ _RK_GLOBAL, "ssl.ca.location", _RK_C_STR,
	  _RK(ssl.ca_location),
	  "File or directory path to CA certificate(s) for verifying "
	  "the broker's key."
	},
	{ _RK_GLOBAL, "ssl.crl.location", _RK_C_STR,
	  _RK(ssl.crl_location),
	  "Path to CRL for verifying broker's certificate validity."
	},
#endif /* WITH_SSL */

#if WITH_SASL
	{_RK_GLOBAL,"sasl.mechanisms", _RK_C_STR,
	 _RK(sasl.mechanisms),
	 "SASL mechanism to use for authentication. "
	 "Supported: GSSAPI, PLAIN. "
	 "**NOTE**: Despite the name only one mechanism must be configured.",
	 .sdef = "GSSAPI",
	 .s2i = {
			{ 0, "GSSAPI" },
			{ 0, "PLAIN" }
		},
	 .validate = rd_kafka_conf_validate_single },
	{ _RK_GLOBAL, "sasl.kerberos.service.name", _RK_C_STR,
	  _RK(sasl.service_name),
	  "Kerberos principal name that Kafka runs as.",
	  .sdef = "kafka" },
	{ _RK_GLOBAL, "sasl.kerberos.principal", _RK_C_STR,
	  _RK(sasl.principal),
	  "This client's Kerberos principal name.",
	  .sdef = "kafkaclient" },
	{ _RK_GLOBAL, "sasl.kerberos.kinit.cmd", _RK_C_STR,
	  _RK(sasl.kinit_cmd),
	  "Full kerberos kinit command string, %{config.prop.name} is replaced "
	  "by corresponding config object value, %{broker.name} returns the "
	  "broker's hostname.",
	  .sdef = "kinit -S \"%{sasl.kerberos.service.name}/%{broker.name}\" "
	  "-k -t \"%{sasl.kerberos.keytab}\" %{sasl.kerberos.principal}" },
	{ _RK_GLOBAL, "sasl.kerberos.keytab", _RK_C_STR,
	  _RK(sasl.keytab),
	  "Path to Kerberos keytab file. Uses system default if not set."
	  "**NOTE**: This is not automatically used but must be added to the "
	  "template in sasl.kerberos.kinit.cmd as "
	  "` ... -t %{sasl.kerberos.keytab}`." },
	{ _RK_GLOBAL, "sasl.kerberos.min.time.before.relogin", _RK_C_INT,
	  _RK(sasl.relogin_min_time),
	  "Minimum time in milliseconds between key refresh attempts.",
	  1, 86400*1000, 60*1000 },
	{ _RK_GLOBAL, "sasl.username", _RK_C_STR,
	  _RK(sasl.username),
	  "SASL username for use with the PLAIN mechanism" },
	{ _RK_GLOBAL, "sasl.password", _RK_C_STR,
	  _RK(sasl.password),
	  "SASL password for use with the PLAIN mechanism" },
#endif

        /* Global client group properties */
        { _RK_GLOBAL|_RK_CGRP, "group.id", _RK_C_STR,
          _RK(group_id_str),
          "Client group id string. All clients sharing the same group.id "
          "belong to the same group." },
        { _RK_GLOBAL|_RK_CGRP, "partition.assignment.strategy", _RK_C_STR,
          _RK(partition_assignment_strategy),
          "Name of partition assignment strategy to use when elected "
          "group leader assigns partitions to group members.",
	  .sdef = "range,roundrobin" },
        { _RK_GLOBAL|_RK_CGRP, "session.timeout.ms", _RK_C_INT,
          _RK(group_session_timeout_ms),
          "Client group session and failure detection timeout.",
          1, 3600*1000, 30*1000 },
        { _RK_GLOBAL|_RK_CGRP, "heartbeat.interval.ms", _RK_C_INT,
          _RK(group_heartbeat_intvl_ms),
          "Group session keepalive heartbeat interval.",
          1, 3600*1000, 1*1000 },
        { _RK_GLOBAL|_RK_CGRP, "group.protocol.type", _RK_C_KSTR,
          _RK(group_protocol_type),
          "Group protocol type",
          .sdef = "consumer" },
        { _RK_GLOBAL|_RK_CGRP, "coordinator.query.interval.ms", _RK_C_INT,
          _RK(coord_query_intvl_ms),
          "How often to query for the current client group coordinator. "
          "If the currently assigned coordinator is down the configured "
          "query interval will be divided by ten to more quickly recover "
          "in case of coordinator reassignment.",
          1, 3600*1000, 10*60*1000 },

        /* Global consumer properties */
        { _RK_GLOBAL|_RK_CONSUMER, "enable.auto.commit", _RK_C_BOOL,
          _RK(enable_auto_commit),
          "Automatically and periodically commit offsets in the background.",
          0, 1, 1 },
        { _RK_GLOBAL|_RK_CONSUMER, "auto.commit.interval.ms", _RK_C_INT,
	  _RK(auto_commit_interval_ms),
	  "The frequency in milliseconds that the consumer offsets "
	  "are committed (written) to offset storage. (0 = disable)",
          0, 86400*1000, 5*1000 },
        { _RK_GLOBAL|_RK_CONSUMER, "enable.auto.offset.store", _RK_C_BOOL,
          _RK(enable_auto_offset_store),
          "Automatically store offset of last message provided to "
	  "application.",
          0, 1, 1 },
	{ _RK_GLOBAL|_RK_CONSUMER, "queued.min.messages", _RK_C_INT,
	  _RK(queued_min_msgs),
	  "Minimum number of messages per topic+partition in the "
          "local consumer queue.",
	  1, 10000000, 100000 },
	{ _RK_GLOBAL|_RK_CONSUMER, "queued.max.messages.kbytes", _RK_C_INT,
	  _RK(queued_max_msg_kbytes),
          "Maximum number of kilobytes per topic+partition in the "
          "local consumer queue. "
          "This value may be overshot by fetch.message.max.bytes.",
          1, 1000000000, 1000000 /* 1 Gig */ },
	{ _RK_GLOBAL|_RK_CONSUMER, "fetch.wait.max.ms", _RK_C_INT,
	  _RK(fetch_wait_max_ms),
	  "Maximum time the broker may wait to fill the response "
	  "with fetch.min.bytes.",
	  0, 300*1000, 100 },
        { _RK_GLOBAL|_RK_CONSUMER, "fetch.message.max.bytes", _RK_C_INT,
          _RK(fetch_msg_max_bytes),
          "Maximum number of bytes per topic+partition to request when "
          "fetching messages from the broker.",
          1, 1000000000, 1024*1024 },
	{ _RK_GLOBAL|_RK_CONSUMER, "max.partition.fetch.bytes", _RK_C_ALIAS,
	  .sdef = "fetch.message.max.bytes" },
	{ _RK_GLOBAL|_RK_CONSUMER, "fetch.min.bytes", _RK_C_INT,
	  _RK(fetch_min_bytes),
	  "Minimum number of bytes the broker responds with. "
	  "If fetch.wait.max.ms expires the accumulated data will "
	  "be sent to the client regardless of this setting.",
	  1, 100000000, 1 },
	{ _RK_GLOBAL|_RK_CONSUMER, "fetch.error.backoff.ms", _RK_C_INT,
	  _RK(fetch_error_backoff_ms),
	  "How long to postpone the next fetch request for a "
	  "topic+partition in case of a fetch error.",
	  0, 300*1000, 500 },
        { _RK_GLOBAL|_RK_CONSUMER, "offset.store.method", _RK_C_S2I,
          _RK(offset_store_method),
          "Offset commit store method: "
          "'file' - local file store (offset.store.path, et.al), "
          "'broker' - broker commit store "
          "(requires Apache Kafka 0.8.2 or later on the broker).",
          .vdef = RD_KAFKA_OFFSET_METHOD_BROKER,
          .s2i = {
                        { RD_KAFKA_OFFSET_METHOD_NONE, "none" },
                        { RD_KAFKA_OFFSET_METHOD_FILE, "file" },
                        { RD_KAFKA_OFFSET_METHOD_BROKER, "broker" }
                }
        },
        { _RK_GLOBAL|_RK_CONSUMER, "consume_cb", _RK_C_PTR,
	  _RK(consume_cb),
	  "Message consume callback (set with rd_kafka_conf_set_consume_cb())"},
	{ _RK_GLOBAL|_RK_CONSUMER, "rebalance_cb", _RK_C_PTR,
	  _RK(rebalance_cb),
	  "Called after consumer group has been rebalanced "
          "(set with rd_kafka_conf_set_rebalance_cb())" },
	{ _RK_GLOBAL|_RK_CONSUMER, "offset_commit_cb", _RK_C_PTR,
	  _RK(offset_commit_cb),
	  "Offset commit result propagation callback. "
          "(set with rd_kafka_conf_set_offset_commit_cb())" },


	/* Global producer properties */
	{ _RK_GLOBAL|_RK_PRODUCER, "queue.buffering.max.messages", _RK_C_INT,
	  _RK(queue_buffering_max_msgs),
	  "Maximum number of messages allowed on the producer queue.",
	  1, 10000000, 100000 },
	{ _RK_GLOBAL|_RK_PRODUCER, "queue.buffering.max.ms", _RK_C_INT,
	  _RK(buffering_max_ms),
	  "Maximum time, in milliseconds, for buffering data "
	  "on the producer queue.",
	  1, 900*1000, 1000 },
	{ _RK_GLOBAL|_RK_PRODUCER, "message.send.max.retries", _RK_C_INT,
	  _RK(max_retries),
	  "How many times to retry sending a failing MessageSet. "
	  "**Note:** retrying may cause reordering.",
          0, 10000000, 2 },
          { _RK_GLOBAL | _RK_PRODUCER, "retries", _RK_C_ALIAS,
                .sdef = "message.send.max.retries" },
	{ _RK_GLOBAL|_RK_PRODUCER, "retry.backoff.ms", _RK_C_INT,
	  _RK(retry_backoff_ms),
	  "The backoff time in milliseconds before retrying a message send.",
	  1, 300*1000, 100 },
	{ _RK_GLOBAL|_RK_PRODUCER, "compression.codec", _RK_C_S2I,
	  _RK(compression_codec),
	  "compression codec to use for compressing message sets. "
	  "This is the default value for all topics, may be overriden by "
	  "the topic configuration property `compression.codec`. ",
	  .vdef = RD_KAFKA_COMPRESSION_NONE,
	  .s2i = {
			{ RD_KAFKA_COMPRESSION_NONE,   "none" },
#if WITH_ZLIB
			{ RD_KAFKA_COMPRESSION_GZIP,   "gzip" },
#endif
#if WITH_SNAPPY
			{ RD_KAFKA_COMPRESSION_SNAPPY, "snappy" },
#endif
#if WITH_LZ4
                        { RD_KAFKA_COMPRESSION_LZ4, "lz4" },
#endif
			{ 0 }
		} },
	{ _RK_GLOBAL|_RK_PRODUCER, "batch.num.messages", _RK_C_INT,
	  _RK(batch_num_messages),
	  "Maximum number of messages batched in one MessageSet.",
	  1, 1000000, 1000 },
	{ _RK_GLOBAL|_RK_PRODUCER, "delivery.report.only.error", _RK_C_BOOL,
	  _RK(dr_err_only),
	  "Only provide delivery reports for failed messages.",
	  0, 1, 0 },
	{ _RK_GLOBAL|_RK_PRODUCER, "dr_cb", _RK_C_PTR,
	  _RK(dr_cb),
	  "Delivery report callback (set with rd_kafka_conf_set_dr_cb())" },
	{ _RK_GLOBAL|_RK_PRODUCER, "dr_msg_cb", _RK_C_PTR,
	  _RK(dr_msg_cb),
	  "Delivery report callback (set with rd_kafka_conf_set_dr_msg_cb())" },
  { _RK_GLOBAL|_RK_PRODUCER, "streams.producer.default.stream", _RK_C_STR,
    _RK(streams_producer_default_stream_name),
    "Producer default stream name, if configured requests will always go to mapr"
    " streams. This default stream name is prepended to the provided topic name"},
  { _RK_GLOBAL|_RK_CONSUMER, "streams.consumer.default.stream", _RK_C_STR,
    _RK(streams_consumer_default_stream_name),
    "Consumer default stream name, if configured requests will always go to mapr"
    "streams. This default stream name is prepended to the provided topic name"},
  { _RK_GLOBAL|_RK_PRODUCER, "streams.parallel.flushers.per.partition", _RK_C_BOOL,
    _RK(streams_parallel_flushers_per_partition),
    "If enabled, producer may have multiple parallel send requests to the server"
    " for each topic partition. If this setting is set to true, it is possible"
    " for messages to be sent out of order.",
    0, 1, 1 },


        /*
         * Topic properties
         */

        /* Topic producer properties */
	{ _RK_TOPIC|_RK_PRODUCER, "request.required.acks", _RK_C_INT,
	  _RKT(required_acks),
	  "This field indicates how many acknowledgements the leader broker "
	  "must receive from ISR brokers before responding to the request: "
	  "*0*=Broker does not send any response/ack to client, "
	  "*1*=Only the leader broker will need to ack the message, "
	  "*-1* or *all*=broker will block until message is committed by all "
	  "in sync replicas (ISRs) or broker's `in.sync.replicas` "
	  "setting before sending response. ",
	  -1, 1000, 1,
	  .s2i = {
			{ -1, "all" },
		}
	},
	{ _RK_TOPIC | _RK_PRODUCER, "acks", _RK_C_ALIAS,
	  .sdef = "request.required.acks" },

	{ _RK_TOPIC|_RK_PRODUCER, "request.timeout.ms", _RK_C_INT,
	  _RKT(request_timeout_ms),
	  "The ack timeout of the producer request in milliseconds. "
	  "This value is only enforced by the broker and relies "
	  "on `request.required.acks` being > 0.",
	  1, 900*1000, 5*1000 },
	{ _RK_TOPIC|_RK_PRODUCER, "message.timeout.ms", _RK_C_INT,
	  _RKT(message_timeout_ms),
	  "Local message timeout. "
	  "This value is only enforced locally and limits the time a "
	  "produced message waits for successful delivery. "
          "A time of 0 is infinite.",
	  0, 900*1000, 300*1000 },
        { _RK_TOPIC|_RK_PRODUCER, "produce.offset.report", _RK_C_BOOL,
          _RKT(produce_offset_report),
          "Report offset of produced message back to application. "
          "The application must be use the `dr_msg_cb` to retrieve the offset "
          "from `rd_kafka_message_t.offset`.",
          0, 1, 0 },
	{ _RK_TOPIC|_RK_PRODUCER, "partitioner_cb", _RK_C_PTR,
	  _RKT(partitioner),
	  "Partitioner callback "
	  "(set with rd_kafka_topic_conf_set_partitioner_cb())" },
	{ _RK_TOPIC|_RK_PRODUCER, "user_defined_partitioner", _RK_C_BOOL,
	  _RKT(user_defined_partitioner),
	  "User defined partitioner callback "
	  "(set with rd_kafka_topic_conf_set_partitioner_cb())",
	  .vdef = 0 },
	{ _RK_TOPIC, "opaque", _RK_C_PTR,
	  _RKT(opaque),
	  "Application opaque (set with rd_kafka_topic_conf_set_opaque())" },
	{ _RK_TOPIC | _RK_PRODUCER, "compression.codec", _RK_C_S2I,
	  _RKT(compression_codec),
	  "Compression codec to use for compressing message sets. ",
	  .vdef = RD_KAFKA_COMPRESSION_INHERIT,
	  .s2i = {
		  { RD_KAFKA_COMPRESSION_NONE, "none" },
#if WITH_ZLIB
		  { RD_KAFKA_COMPRESSION_GZIP, "gzip" },
#endif
#if WITH_SNAPPY
		  { RD_KAFKA_COMPRESSION_SNAPPY, "snappy" },
#endif
#if WITH_LZ4
		  { RD_KAFKA_COMPRESSION_LZ4, "lz4" },
#endif

		  { RD_KAFKA_COMPRESSION_INHERIT, "inherit" },
		  { 0 }
		} },


        /* Topic consumer properties */
	{ _RK_TOPIC|_RK_CONSUMER, "auto.commit.enable", _RK_C_BOOL,
	  _RKT(auto_commit),
	  "If true, periodically commit offset of the last message handed "
	  "to the application. This committed offset will be used when the "
	  "process restarts to pick up where it left off. "
	  "If false, the application will have to call "
	  "`rd_kafka_offset_store()` to store an offset (optional). "
          "**NOTE:** This property should only be used with the simple "
          "legacy consumer, when using the high-level KafkaConsumer the global "
          "`auto.commit.enable` property must be used instead. "
	  "**NOTE:** There is currently no zookeeper integration, offsets "
	  "will be written to broker or local file according to "
          "offset.store.method.",
	  0, 1, 1 },
	{ _RK_TOPIC|_RK_CONSUMER, "enable.auto.commit", _RK_C_ALIAS,
	  .sdef = "auto.commit.enable" },
	{ _RK_TOPIC|_RK_CONSUMER, "auto.commit.interval.ms", _RK_C_INT,
	  _RKT(auto_commit_interval_ms),
	  "The frequency in milliseconds that the consumer offsets "
	  "are committed (written) to offset storage.",
	  10, 86400*1000, 60*1000 },
	{ _RK_TOPIC|_RK_CONSUMER, "auto.offset.reset", _RK_C_S2I,
	  _RKT(auto_offset_reset),
	  "Action to take when there is no initial offset in offset store "
	  "or the desired offset is out of range: "
	  "'smallest','earliest' - automatically reset the offset to the smallest offset, "
	  "'largest','latest' - automatically reset the offset to the largest offset, "
	  "'error' - trigger an error which is retrieved by consuming messages "
	  "and checking 'message->err'.",
	  .vdef = RD_KAFKA_OFFSET_END,
	  .s2i = {
			{ RD_KAFKA_OFFSET_BEGINNING, "smallest" },
			{ RD_KAFKA_OFFSET_BEGINNING, "earliest" },
			{ RD_KAFKA_OFFSET_END, "largest" },
			{ RD_KAFKA_OFFSET_END, "latest" },
			{ RD_KAFKA_OFFSET_INVALID, "error" },
		}
	},
	{ _RK_TOPIC|_RK_CONSUMER, "offset.store.path", _RK_C_STR,
	  _RKT(offset_store_path),
	  "Path to local file for storing offsets. If the path is a directory "
	  "a filename will be automatically generated in that directory based "
	  "on the topic and partition.",
	  .sdef = "." },

	{ _RK_TOPIC|_RK_CONSUMER, "offset.store.sync.interval.ms", _RK_C_INT,
	  _RKT(offset_store_sync_interval_ms),
	  "fsync() interval for the offset file, in milliseconds. "
	  "Use -1 to disable syncing, and 0 for immediate sync after "
	  "each write.",
	  -1, 86400*1000, -1 },

        { _RK_TOPIC|_RK_CONSUMER, "offset.store.method", _RK_C_S2I,
          _RKT(offset_store_method),
          "Offset commit store method: "
          "'file' - local file store (offset.store.path, et.al), "
          "'broker' - broker commit store "
          "(requires \"group.id\" to be configured and "
	  "Apache Kafka 0.8.2 or later on the broker.).",
          .vdef = RD_KAFKA_OFFSET_METHOD_BROKER, /* FIXME: warn about default change */
          .s2i = {
                        { RD_KAFKA_OFFSET_METHOD_FILE, "file" },
                        { RD_KAFKA_OFFSET_METHOD_BROKER, "broker" }
                }
        },

        { _RK_TOPIC|_RK_CONSUMER, "consume.callback.max.messages", _RK_C_INT,
          _RKT(consume_callback_max_msgs),
          "Maximum number of messages to dispatch in "
          "one `rd_kafka_consume_callback*()` call (0 = unlimited)",
          0, 1000000, 0 },

	{ 0, /* End */ }
};

/**
 * List of config properties which will not be
 * defined in Mapr's version of librdkafka.
 */
static const char* streams_unsupported_properties[] = {
           "ssl.cipher.suites",
           "ssl.key.location",
           "ssl.key.password",
           "ssl.certificate.location",
           "ssl.ca.location",
           "ssl.crl.location"
};

typedef enum {
	_PROP_SET_REPLACE,  /* Replace current value (default) */
	_PROP_SET_ADD,      /* Add value (S2F) */
	_PROP_SET_DEL      /* Remove value (S2F) */
} prop_set_mode_t;

static rd_kafka_conf_res_t
rd_kafka_anyconf_set_prop0 (int scope, void *conf,
			    const struct rd_kafka_property *prop,
			    const char *istr, int ival, prop_set_mode_t set_mode,
                            char *errstr, size_t errstr_size) {

#define _RK_PTR(TYPE,BASE,OFFSET)  (TYPE)(((char *)(BASE))+(OFFSET))
	switch (prop->type)
	{
	case _RK_C_STR:
	{
		char **str = _RK_PTR(char **, conf, prop->offset);
		if (*str)
			rd_free(*str);
		if (istr)
			*str = rd_strdup(istr);
		else
			*str = NULL;
		return RD_KAFKA_CONF_OK;
	}
        case _RK_C_KSTR:
        {
                rd_kafkap_str_t **kstr = _RK_PTR(rd_kafkap_str_t **, conf,
                                                 prop->offset);
                if (*kstr)
                        rd_kafkap_str_destroy(*kstr);
                if (istr)
                        *kstr = rd_kafkap_str_new(istr, -1);
                else
                        *kstr = NULL;
                return RD_KAFKA_CONF_OK;
        }
	case _RK_C_PTR:
		*_RK_PTR(const void **, conf, prop->offset) = istr;
		return RD_KAFKA_CONF_OK;
	case _RK_C_BOOL:
	case _RK_C_INT:
	case _RK_C_S2I:
	case _RK_C_S2F:
	{
		int *val = _RK_PTR(int *, conf, prop->offset);

		if (prop->type == _RK_C_S2F) {
			switch (set_mode)
			{
			case _PROP_SET_REPLACE:
				*val = ival;
				break;
			case _PROP_SET_ADD:
				*val |= ival;
				break;
			case _PROP_SET_DEL:
				*val &= ~ival;
				break;
			}
		} else {
			/* Single assignment */
			*val = ival;

		}
		return RD_KAFKA_CONF_OK;
	}
        case _RK_C_PATLIST:
        {
                /* Split comma-separated list into individual regex expressions
                 * that are verified and then append to the provided list. */
                rd_kafka_pattern_list_t **plist;

                plist = _RK_PTR(rd_kafka_pattern_list_t **, conf, prop->offset);

		if (*plist)
			rd_kafka_pattern_list_destroy(*plist);

		if (istr) {
			if (!(*plist =
			      rd_kafka_pattern_list_new(istr,
							errstr,
							errstr_size)))
				return RD_KAFKA_CONF_INVALID;
		} else
			*plist = NULL;

                return RD_KAFKA_CONF_OK;
        }

	default:
		rd_kafka_assert(NULL, !*"unknown conf type");
	}

	/* unreachable */
	return RD_KAFKA_CONF_INVALID;
}


/**
 * @brief Find s2i (string-to-int mapping) entry and return its array index,
 *        or -1 on miss.
 */
static int rd_kafka_conf_s2i_find (const struct rd_kafka_property *prop,
				   const char *value) {
	int j;

	for (j = 0 ; j < (int)RD_ARRAYSIZE(prop->s2i); j++) {
		if (prop->s2i[j].str &&
		    !rd_strcasecmp(prop->s2i[j].str, value))
			return j;
	}

	return -1;
}


static rd_kafka_conf_res_t
rd_kafka_anyconf_set_prop (int scope, void *conf,
			   const struct rd_kafka_property *prop,
			   const char *value,
			   char *errstr, size_t errstr_size) {
	int ival;

	switch (prop->type)
	{
	case _RK_C_STR:
        case _RK_C_KSTR:
		if (prop->s2i[0].str) {
			int match;

			if (!value ||
			    (match = rd_kafka_conf_s2i_find(prop, value)) == -1){
				rd_snprintf(errstr, errstr_size,
					    "Invalid value for "
					    "configuration property \"%s\": "
					    "%s",
					    prop->name, value);
				return RD_KAFKA_CONF_INVALID;
			}

			/* Replace value string with canonical form */
			value = prop->s2i[match].str;
		}
		/* FALLTHRU */
        case _RK_C_PATLIST:
		if (prop->validate &&
		    (!value || !prop->validate(prop, value, -1))) {
			rd_snprintf(errstr, errstr_size,
				    "Invalid value: %s", value);
			return RD_KAFKA_CONF_INVALID;
		}

		return rd_kafka_anyconf_set_prop0(scope, conf, prop, value, 0,
						  _PROP_SET_REPLACE,
                                                  errstr, errstr_size);

	case _RK_C_PTR:
		rd_snprintf(errstr, errstr_size,
			 "Property \"%s\" must be set through dedicated "
			 ".._set_..() function", prop->name);
		return RD_KAFKA_CONF_INVALID;

	case _RK_C_BOOL:
		if (!value) {
			rd_snprintf(errstr, errstr_size,
				 "Bool configuration property \"%s\" cannot "
				 "be set to empty value", prop->name);
			return RD_KAFKA_CONF_INVALID;
		}


		if (!rd_strcasecmp(value, "true") ||
		    !rd_strcasecmp(value, "t") ||
		    !strcmp(value, "1"))
			ival = 1;
		else if (!rd_strcasecmp(value, "false") ||
			 !rd_strcasecmp(value, "f") ||
			 !strcmp(value, "0"))
			ival = 0;
		else {
			rd_snprintf(errstr, errstr_size,
				 "Expected bool value for \"%s\": "
				 "true or false", prop->name);
			return RD_KAFKA_CONF_INVALID;
		}

		rd_kafka_anyconf_set_prop0(scope, conf, prop, NULL, ival,
					   _PROP_SET_REPLACE,
                                           errstr, errstr_size);
		return RD_KAFKA_CONF_OK;

	case _RK_C_INT:
	{
		const char *end;

		if (!value) {
			rd_snprintf(errstr, errstr_size,
				 "Integer configuration "
				 "property \"%s\" cannot be set "
				 "to empty value", prop->name);
			return RD_KAFKA_CONF_INVALID;
		}

		ival = (int)strtol(value, (char **)&end, 0);
		if (end == value) {
			/* Non numeric, check s2i for string mapping */
			int match = rd_kafka_conf_s2i_find(prop, value);

			if (match == -1) {
				rd_snprintf(errstr, errstr_size,
					    "Invalid value for "
					    "configuration property \"%s\"",
					    prop->name);
				return RD_KAFKA_CONF_INVALID;
			}

			ival = prop->s2i[match].val;
		}

		if (ival < prop->vmin ||
		    ival > prop->vmax) {
			rd_snprintf(errstr, errstr_size,
				 "Configuration property \"%s\" value "
				 "%i is outside allowed range %i..%i\n",
				 prop->name, ival,
				 prop->vmin,
				 prop->vmax);
			return RD_KAFKA_CONF_INVALID;
		}

		rd_kafka_anyconf_set_prop0(scope, conf, prop, NULL, ival,
					   _PROP_SET_REPLACE,
                                           errstr, errstr_size);
		return RD_KAFKA_CONF_OK;
	}

	case _RK_C_S2I:
	case _RK_C_S2F:
	{
		int j;
		const char *next;

		if (!value) {
			rd_snprintf(errstr, errstr_size,
				 "Configuration "
				 "property \"%s\" cannot be set "
				 "to empty value", prop->name);
			return RD_KAFKA_CONF_INVALID;
		}

		next = value;
		while (next && *next) {
			const char *s, *t;
			prop_set_mode_t set_mode = _PROP_SET_ADD; /* S2F */

			s = next;

			if (prop->type == _RK_C_S2F &&
			    (t = strchr(s, ','))) {
				/* CSV flag field */
				next = t+1;
			} else {
				/* Single string */
				t = s+strlen(s);
				next = NULL;
			}


			/* Left trim */
			while (s < t && isspace((int)*s))
				s++;

			/* Right trim */
			while (t > s && isspace((int)*t))
				t--;

			/* S2F: +/- prefix */
			if (prop->type == _RK_C_S2F) {
				if (*s == '+') {
					set_mode = _PROP_SET_ADD;
					s++;
				} else if (*s == '-') {
					set_mode = _PROP_SET_DEL;
					s++;
				}
			}

			/* Empty string? */
			if (s == t)
				continue;

			/* Match string to s2i table entry */
			for (j = 0 ; j < (int)RD_ARRAYSIZE(prop->s2i); j++) {
				int new_val;

				if (!prop->s2i[j].str)
					continue;

				if (strlen(prop->s2i[j].str) == (size_t)(t-s) &&
					 !rd_strncasecmp(prop->s2i[j].str, s,
							 (int)(t-s)))
					new_val = prop->s2i[j].val;
				else
					continue;

				rd_kafka_anyconf_set_prop0(scope, conf, prop,
							   NULL, new_val, set_mode,
                                                           errstr, errstr_size);

				if (prop->type == _RK_C_S2F) {
					/* Flags: OR it in: do next */
					break;
				} else {
					/* Single assignment */
					return RD_KAFKA_CONF_OK;
				}
			}

			/* S2F: Good match: continue with next */
			if (j < (int)RD_ARRAYSIZE(prop->s2i))
				continue;

			/* No match */
			rd_snprintf(errstr, errstr_size,
				 "Invalid value for "
				 "configuration property \"%s\"", prop->name);
			return RD_KAFKA_CONF_INVALID;

		}
		return RD_KAFKA_CONF_OK;
	}

	default:
                rd_kafka_assert(NULL, !*"unknown conf type");
	}

	/* not reachable */
	return RD_KAFKA_CONF_INVALID;
}



static void rd_kafka_defaultconf_set (int scope, void *conf) {
	const struct rd_kafka_property *prop;

	for (prop = rd_kafka_properties ; prop->name ; prop++) {
		if (!(prop->scope & scope))
			continue;

		if (prop->type == _RK_C_ALIAS)
			continue;

		if (prop->sdef || prop->vdef || prop->pdef)
			rd_kafka_anyconf_set_prop0(scope, conf, prop,
						   prop->sdef ?
                                                   prop->sdef : prop->pdef,
                                                   prop->vdef, _PROP_SET_REPLACE,
                                                   NULL, 0);
	}
}

bool streams_version_compare (char *buf1, char *buf2){
  bool ver_allowed = false;
  if (!buf1 || !buf2)
    return ver_allowed;

  char curr_version [strlen(buf1)+1];
  char min_version [strlen(buf2)+1];
  strcpy(curr_version, buf1);
  strcpy(min_version, buf2);
  char *currPtr;
  char *minPtr;
  char *min_token = strtok_r(min_version, ".", &minPtr);
  char *curr_token = strtok_r(curr_version, ".", &currPtr);
  int count = 0;
  //Only compare Major, minor and revision in a version
  while (curr_token && min_token && (count < 3)) {
    if (atoi(curr_token) >= atoi(min_token)) {
      ver_allowed = true;
      count ++;
      curr_token = strtok_r(NULL, ".", &currPtr);
      min_token = strtok_r(NULL, ".", &minPtr);
    } else {
      ver_allowed = false;
      break;
    }
  }
  return ver_allowed;
}

bool streams_version_check () {
  bool err = true;
  char errStr[512];
  char *version = NULL;
  bool isPresent = is_funct_present("streams_mapr_build_version_get");
  snprintf(errStr, 512, "minimum required version: %s",
                          STREAMS_MIN_VERSION);
  if(!isPresent)
    goto verErr;

  streams_mapr_build_version_get(&version);

  if (!version)
    goto verErr;

  if (!streams_version_compare (version, STREAMS_MIN_VERSION)) {
    memset (errStr, 0, 512);
    snprintf(errStr, 512, "libMapRClient.so version: %s\
        minimum required version: %s",version, STREAMS_MIN_VERSION);
    goto verErr;
  } else {
    goto end;
  }

  verErr:
    err = false;
    rd_kafka_log_print(NULL, LOG_ERR, "LIBRARY_MISMATCH", errStr);

  end:
    if(version) streams_mapr_build_version_destroy(version);
    return err;
}

rd_kafka_conf_t *rd_kafka_conf_new (void) {
  if (!is_streams_compatible) {
    if (!streams_version_check ())
      return NULL;

    is_streams_compatible = true;
  }
  rd_kafka_conf_t *conf = rd_calloc(1, sizeof(*conf));
	rd_kafka_defaultconf_set(_RK_GLOBAL, conf);
	return conf;
}

rd_kafka_topic_conf_t *rd_kafka_topic_conf_new (void) {
	rd_kafka_topic_conf_t *tconf = rd_calloc(1, sizeof(*tconf));
	rd_kafka_defaultconf_set(_RK_TOPIC, tconf);
	return tconf;
}

static bool streams_is_config_supported(const char *name) {
        int arr_len = sizeof(streams_unsupported_properties)/sizeof (char *);
        int i = 0;
        for (i = 0; i < arr_len; i++ ) {
                if (strcmp (streams_unsupported_properties[i], name))
                        continue;
                return false;
        }
        return true;
}


static int rd_kafka_anyconf_set (int scope, void *conf,
				 const char *name, const char *value,
				 char *errstr, size_t errstr_size) {
	if(!conf || !name)
		return RD_KAFKA_CONF_UNKNOWN;
	char estmp[1];
	const struct rd_kafka_property *prop;

	if (!errstr) {
		errstr = estmp;
		errstr_size = 0;
	}

	if (value && !*value)
		value = NULL;

	for (prop = rd_kafka_properties ; prop->name ; prop++) {

		if (!(prop->scope & scope))
			continue;

		if (strcmp(prop->name, name))
			continue;

		if (prop->type == _RK_C_ALIAS)
			return rd_kafka_anyconf_set(scope, conf,
						    prop->sdef, value,
						    errstr, errstr_size);

		return rd_kafka_anyconf_set_prop(scope, conf, prop, value,
						 errstr, errstr_size);
	}

	if(!streams_is_config_supported(name)) {
		fprintf(stderr, "WARNING: Mapr-librdkafka does not support '%s' configuration property. Ignoring ...", name);
		return RD_KAFKA_CONF_OK;
	} else {
		rd_snprintf(errstr, errstr_size,
			"No such configuration property: \"%s\"", name);
	}
	return RD_KAFKA_CONF_UNKNOWN;
}


rd_kafka_conf_res_t rd_kafka_conf_set (rd_kafka_conf_t *conf,
				       const char *name,
				       const char *value,
				       char *errstr, size_t errstr_size) {
	return rd_kafka_anyconf_set(_RK_GLOBAL, conf, name, value,
				    errstr, errstr_size);
}


rd_kafka_conf_res_t rd_kafka_topic_conf_set (rd_kafka_topic_conf_t *conf,
					     const char *name,
					     const char *value,
					     char *errstr, size_t errstr_size) {
	if(!name)
		return RD_KAFKA_CONF_UNKNOWN;
	if (!strncmp(name, "topic.", strlen("topic.")))
		name += strlen("topic.");

	return rd_kafka_anyconf_set(_RK_TOPIC, conf, name, value,
				    errstr, errstr_size);
}


static void rd_kafka_anyconf_clear (void *conf,
				    const struct rd_kafka_property *prop) {
	switch (prop->type)
	{
	case _RK_C_STR:
	{
		char **str = _RK_PTR(char **, conf, prop->offset);
		if (*str) {
			rd_free(*str);
			*str = NULL;
		}
	}
	break;

        case _RK_C_KSTR:
        {
                rd_kafkap_str_t **kstr = _RK_PTR(rd_kafkap_str_t **, conf,
                                                 prop->offset);
                if (*kstr) {
                        rd_kafkap_str_destroy(*kstr);
                        *kstr = NULL;
                }
        }
        break;

        case _RK_C_PATLIST:
        {
                rd_kafka_pattern_list_t **plist;
                plist = _RK_PTR(rd_kafka_pattern_list_t **, conf, prop->offset);
		if (*plist) {
			rd_kafka_pattern_list_destroy(*plist);
			*plist = NULL;
		}
        }
        break;

        case _RK_C_PTR:
                if (_RK_PTR(void *, conf, prop->offset) != NULL) {
                        if (!strcmp(prop->name, "default_topic_conf")) {
                                rd_kafka_topic_conf_t **tconf;

                                tconf = _RK_PTR(rd_kafka_topic_conf_t **,
                                                conf, prop->offset);
                                if (*tconf) {
                                        rd_kafka_topic_conf_destroy(*tconf);
                                        *tconf = NULL;
                                }
                        }
                }
                break;

	default:
		break;
	}

}

void rd_kafka_anyconf_destroy (int scope, void *conf) {
	if(!conf)
		return;
	const struct rd_kafka_property *prop;

	for (prop = rd_kafka_properties; prop->name ; prop++) {
		if (!(prop->scope & scope))
			continue;

		rd_kafka_anyconf_clear(conf, prop);
	}
}


void rd_kafka_conf_destroy (rd_kafka_conf_t *conf) {
	rd_kafka_anyconf_destroy(_RK_GLOBAL, conf);
        //FIXME: partition_assignors
	rd_free(conf);
}

void rd_kafka_topic_conf_destroy (rd_kafka_topic_conf_t *topic_conf) {
	rd_kafka_anyconf_destroy(_RK_TOPIC, topic_conf);
	rd_free(topic_conf);
}



static void rd_kafka_anyconf_copy (int scope, void *dst, const void *src) {
	if(!src || !dst)
		return;
	const struct rd_kafka_property *prop;

	for (prop = rd_kafka_properties ; prop->name ; prop++) {
		const char *val = NULL;
		int ival = 0;

		if (!(prop->scope & scope))
			continue;

		if (prop->type == _RK_C_ALIAS)
			continue;

		switch (prop->type)
		{
		case _RK_C_STR:
		case _RK_C_PTR:
			val = *_RK_PTR(const char **, src, prop->offset);

                        if (!strcmp(prop->name, "default_topic_conf") && val)
                                val = (void *)rd_kafka_topic_conf_dup(
                                        (const rd_kafka_topic_conf_t *)val);
			break;
                case _RK_C_KSTR:
                {
                        rd_kafkap_str_t **kstr = _RK_PTR(rd_kafkap_str_t **,
                                                         src, prop->offset);
                        if (*kstr)
                                val = (*kstr)->str;
                        break;
                }

		case _RK_C_BOOL:
		case _RK_C_INT:
		case _RK_C_S2I:
		case _RK_C_S2F:
			ival = *_RK_PTR(const int *, src, prop->offset);
			break;
                case _RK_C_PATLIST:
                {
                        const rd_kafka_pattern_list_t **plist;
                        plist = _RK_PTR(const rd_kafka_pattern_list_t **,
                                        src, prop->offset);
			if (*plist)
				val = (*plist)->rkpl_orig;
                        break;
                }
		default:
			continue;
		}

		rd_kafka_anyconf_set_prop0(scope, dst, prop, val, ival,
                                           _PROP_SET_REPLACE, NULL, 0);
	}
}


rd_kafka_conf_t *rd_kafka_conf_dup (const rd_kafka_conf_t *conf) {
	rd_kafka_conf_t *new = rd_kafka_conf_new();

	rd_kafka_anyconf_copy(_RK_GLOBAL, new, conf);

	return new;
}


rd_kafka_topic_conf_t *rd_kafka_topic_conf_dup (const rd_kafka_topic_conf_t
						*conf) {
	rd_kafka_topic_conf_t *new = rd_kafka_topic_conf_new();

	rd_kafka_anyconf_copy(_RK_TOPIC, new, conf);

	return new;
}


void rd_kafka_conf_set_dr_cb (rd_kafka_conf_t *conf,
			      void (*dr_cb) (rd_kafka_t *rk,
					     void *payload, size_t len,
					     rd_kafka_resp_err_t err,
					     void *opaque, void *msg_opaque)) {
	if(conf)
	  conf->dr_cb = dr_cb;
}


void rd_kafka_conf_set_dr_msg_cb (rd_kafka_conf_t *conf,
                                  void (*dr_msg_cb) (rd_kafka_t *rk,
                                                     const rd_kafka_message_t *
                                                     rkmessage,
                                                     void *opaque)) {
	if(conf)
	  conf->dr_msg_cb = dr_msg_cb;
}


void rd_kafka_conf_set_consume_cb (rd_kafka_conf_t *conf,
                                   void (*consume_cb) (rd_kafka_message_t *
                                                       rkmessage,
                                                       void *opaque)) {
	if(conf)
	  conf->consume_cb = consume_cb;
}

void rd_kafka_conf_set_rebalance_cb (
        rd_kafka_conf_t *conf,
        void (*rebalance_cb) (rd_kafka_t *rk,
                              rd_kafka_resp_err_t err,
                              rd_kafka_topic_partition_list_t *partitions,
                              void *opaque)) {
	if(conf)
	  conf->rebalance_cb = rebalance_cb;
}

void rd_kafka_conf_set_offset_commit_cb (
        rd_kafka_conf_t *conf,
        void (*offset_commit_cb) (rd_kafka_t *rk,
                                  rd_kafka_resp_err_t err,
                                  rd_kafka_topic_partition_list_t *offsets,
                                  void *opaque)) {
	if(conf)
	  conf->offset_commit_cb = offset_commit_cb;
}



void rd_kafka_conf_set_error_cb (rd_kafka_conf_t *conf,
				 void  (*error_cb) (rd_kafka_t *rk, int err,
						    const char *reason,
						    void *opaque)) {
	if(conf)
	  conf->error_cb = error_cb;
}


void rd_kafka_conf_set_throttle_cb (rd_kafka_conf_t *conf,
				    void (*throttle_cb) (
					    rd_kafka_t *rk,
					    const char *broker_name,
					    int32_t broker_id,
					    int throttle_time_ms,
					    void *opaque)) {
	if(conf)
	  conf->throttle_cb = throttle_cb;
}


void rd_kafka_conf_set_log_cb (rd_kafka_conf_t *conf,
			  void (*log_cb) (const rd_kafka_t *rk, int level,
                                          const char *fac, const char *buf)) {
	if(conf)
	  conf->log_cb = log_cb;
}


void rd_kafka_conf_set_stats_cb (rd_kafka_conf_t *conf,
				 int (*stats_cb) (rd_kafka_t *rk,
						  char *json,
						  size_t json_len,
						  void *opaque)) {
	if(conf)
	  conf->stats_cb = stats_cb;
}

void rd_kafka_conf_set_socket_cb (rd_kafka_conf_t *conf,
                                  int (*socket_cb) (int domain, int type,
                                                    int protocol,
                                                    void *opaque)) {
	if(conf)
	  conf->socket_cb = socket_cb;
}


#ifndef _MSC_VER
void rd_kafka_conf_set_open_cb (rd_kafka_conf_t *conf,
                                int (*open_cb) (const char *pathname,
                                                int flags, mode_t mode,
                                                void *opaque)) {
	if(conf)
	  conf->open_cb = open_cb;
}
#endif

void rd_kafka_conf_set_opaque (rd_kafka_conf_t *conf, void *opaque) {
	if(conf)
	  conf->opaque = opaque;
}


void rd_kafka_conf_set_default_topic_conf (rd_kafka_conf_t *conf,
                                           rd_kafka_topic_conf_t *tconf) {
        if(!conf)
          return;
        if (conf->topic_conf)
                rd_kafka_topic_conf_destroy(conf->topic_conf);

        conf->topic_conf = tconf;
}


void
rd_kafka_topic_conf_set_partitioner_cb (rd_kafka_topic_conf_t *topic_conf,
					int32_t (*partitioner) (
						const rd_kafka_topic_t *rkt,
						const void *keydata,
						size_t keylen,
						int32_t partition_cnt,
						void *rkt_opaque,
						void *msg_opaque)) {

  if(topic_conf){
    topic_conf->user_defined_partitioner = true;
    topic_conf->partitioner = partitioner;
  }
}

void rd_kafka_topic_conf_set_opaque (rd_kafka_topic_conf_t *topic_conf,
				     void *opaque) {
  if(topic_conf)
	  topic_conf->opaque = opaque;
}




/**
 * @brief Convert flags \p ival to csv-string using S2F property \p prop.
 *
 * This function has two modes: size query and write.
 * To query for needed size call with dest==NULL,
 * to write to buffer of size dest_size call with dest!=NULL.
 *
 * An \p ival of -1 means all.
 *
 * @returns the number of bytes written to \p dest (if not NULL), else the
 *          total number of bytes needed.
 *
 */
size_t rd_kafka_conf_flags2str (char *dest, size_t dest_size, const char *delim,
				const struct rd_kafka_property *prop,
				int ival) {
	size_t of = 0;
	int j;

	if (dest)
		*dest = '\0';

	/* Phase 1: scan for set flags, accumulate needed size.
	 * Phase 2: write to dest */
	for (j = 0 ; prop->s2i[j].str ; j++) {
		if (prop->type == _RK_C_S2F && ival != -1 &&
		    (ival & prop->s2i[j].val) != prop->s2i[j].val)
			continue;
		else if (prop->type == _RK_C_S2I &&
			   ival != -1 && prop->s2i[j].val != ival)
			continue;

		if (!dest)
			of += strlen(prop->s2i[j].str) + (of > 0 ? 1 : 0);
		else {
			size_t r;
			r = rd_snprintf(dest+of, dest_size-of,
					"%s%s",
					of > 0 ? delim:"",
					prop->s2i[j].str);
			if (r > dest_size-of) {
				r = dest_size-of;
				break;
			}
			of += r;
		}
	}

	return of;
}


/**
 * Return "original"(re-created) configuration value string
 */
static rd_kafka_conf_res_t
rd_kafka_anyconf_get0 (const void *conf, const struct rd_kafka_property *prop,
                       char *dest, size_t *dest_size) {
        char tmp[22];
        const char *val = NULL;
        size_t val_len = 0;
        int j;

        switch (prop->type)
        {
        case _RK_C_STR:
                val = *_RK_PTR(const char **, conf, prop->offset);
                break;

        case _RK_C_KSTR:
        {
                const rd_kafkap_str_t **kstr = _RK_PTR(const rd_kafkap_str_t **,
                                                       conf, prop->offset);
                if (*kstr)
                        val = (*kstr)->str;
                break;
        }

        case _RK_C_PTR:
                val = *_RK_PTR(const void **, conf, prop->offset);
                if (val) {
                        rd_snprintf(tmp, sizeof(tmp), "%p", (void *)val);
                        val = tmp;
                }
                break;

        case _RK_C_BOOL:
                val = (*_RK_PTR(int *, conf, prop->offset) ? "true" : "false");
                break;

        case _RK_C_INT:
                rd_snprintf(tmp, sizeof(tmp), "%i",
                            *_RK_PTR(int *, conf, prop->offset));
                val = tmp;
                break;

        case _RK_C_S2I:
                for (j = 0 ; j < (int)RD_ARRAYSIZE(prop->s2i); j++) {
                        if (prop->s2i[j].val ==
                            *_RK_PTR(int *, conf, prop->offset)) {
                                val = prop->s2i[j].str;
                                break;
                        }
                }
                break;

        case _RK_C_S2F:
        {
                const int ival = *_RK_PTR(const int *, conf, prop->offset);

		val_len = rd_kafka_conf_flags2str(dest, *dest_size, ",",
						  prop, ival);
		if (dest) {
			val_len = 0;
			val = dest;
			dest = NULL;
		}
		break;
	}

        case _RK_C_PATLIST:
        {
                const rd_kafka_pattern_list_t **plist;
                plist = _RK_PTR(const rd_kafka_pattern_list_t **,
                                conf, prop->offset);
		if (*plist)
			val = (*plist)->rkpl_orig;
                break;
        }

        default:
                break;
        }

        if (val_len) {
                *dest_size = val_len+1;
                return RD_KAFKA_CONF_OK;
        }

        if (!val)
                return RD_KAFKA_CONF_INVALID;

        val_len = strlen(val);

        if (dest) {
                size_t use_len = RD_MIN(val_len, (*dest_size)-1);
                memcpy(dest, val, use_len);
                dest[use_len] = '\0';
        }

        /* Return needed size */
        *dest_size = val_len+1;

        return RD_KAFKA_CONF_OK;
}


static rd_kafka_conf_res_t rd_kafka_anyconf_get (int scope, const void *conf,
                                                 const char *name,
                                                 char *dest, size_t *dest_size){
	if(!conf || !name)
		return RD_KAFKA_CONF_UNKNOWN;
	const struct rd_kafka_property *prop;

	for (prop = rd_kafka_properties; prop->name ; prop++) {

		if (!(prop->scope & scope) || strcmp(prop->name, name))
			continue;

		if (prop->type == _RK_C_ALIAS)
			return rd_kafka_anyconf_get(scope, conf,
						    prop->sdef,
						    dest, dest_size);

                if (rd_kafka_anyconf_get0(conf, prop, dest, dest_size) ==
                    RD_KAFKA_CONF_OK)
                        return RD_KAFKA_CONF_OK;
        }

        return RD_KAFKA_CONF_UNKNOWN;
}

rd_kafka_conf_res_t rd_kafka_topic_conf_get (const rd_kafka_topic_conf_t *conf,
                                             const char *name,
                                             char *dest, size_t *dest_size) {
        return rd_kafka_anyconf_get(_RK_TOPIC, conf, name, dest, dest_size);
}

rd_kafka_conf_res_t rd_kafka_conf_get (const rd_kafka_conf_t *conf,
                                       const char *name,
                                       char *dest, size_t *dest_size) {
        return rd_kafka_anyconf_get(_RK_GLOBAL, conf, name, dest, dest_size);
}


static const char **rd_kafka_anyconf_dump (int scope, const void *conf,
					   size_t *cntp) {
	if(!conf)
		return NULL;
	const struct rd_kafka_property *prop;
	char **arr;
	int cnt = 0;

	arr = rd_calloc(sizeof(char *), RD_ARRAYSIZE(rd_kafka_properties)*2);

	for (prop = rd_kafka_properties; prop->name ; prop++) {
                char *val = NULL;
                size_t val_size;

		if (!(prop->scope & scope))
			continue;

		/* Skip aliases, show original property instead. */
		if (prop->type == _RK_C_ALIAS)
			continue;

                /* Query value size */
                if (rd_kafka_anyconf_get0(conf, prop, NULL, &val_size) !=
                    RD_KAFKA_CONF_OK)
                        continue;

                /* Get value */
                val = malloc(val_size);
                rd_kafka_anyconf_get0(conf, prop, val, &val_size);

                arr[cnt++] = rd_strdup(prop->name);
                arr[cnt++] = val;
	}

	*cntp = cnt;

	return (const char **)arr;
}


const char **rd_kafka_conf_dump (rd_kafka_conf_t *conf, size_t *cntp) {
	return rd_kafka_anyconf_dump(_RK_GLOBAL, conf, cntp);
}

const char **rd_kafka_topic_conf_dump (rd_kafka_topic_conf_t *conf,
				       size_t *cntp) {
	return rd_kafka_anyconf_dump(_RK_TOPIC, conf, cntp);
}

void rd_kafka_conf_dump_free (const char **arr, size_t cnt) {
	char **_arr = (char **)arr;
	unsigned int i;

	for (i = 0 ; i < cnt ; i++)
		if (_arr[i])
			rd_free(_arr[i]);

	rd_free(_arr);
}

void rd_kafka_conf_properties_show (FILE *fp) {
	const struct rd_kafka_property *prop;
	int last = 0;
	int j;
	char tmp[512];
	const char *dash80 = "----------------------------------------"
		"----------------------------------------";

	for (prop = rd_kafka_properties; prop->name ; prop++) {
		const char *typeinfo = "";

		if (!(prop->scope & last)) {
			fprintf(fp,
				"%s## %s configuration properties\n\n",
				last ? "\n\n":"",
				prop->scope == _RK_GLOBAL ? "Global": "Topic");

			fprintf(fp,
				"%-40s | %3s | %-15s | %13s | %-25s\n"
				"%.*s-|-%.*s-|-%.*s-|-%.*s:|-%.*s\n",
				"Property", "C/P", "Range",
				"Default", "Description",
				40, dash80, 3, dash80, 15, dash80,
				13, dash80, 25, dash80);

			last = prop->scope & (_RK_GLOBAL|_RK_TOPIC);

		}

		fprintf(fp, "%-40s | %3s | ", prop->name,
                        (!(prop->scope & _RK_PRODUCER) ==
                         !(prop->scope & _RK_CONSUMER) ? " * " :
                         ((prop->scope & _RK_PRODUCER) ? " P " :
                          (prop->scope & _RK_CONSUMER) ? " C " : "")));

		switch (prop->type)
		{
		case _RK_C_STR:
                case _RK_C_KSTR:
			typeinfo = "string";
                case _RK_C_PATLIST:
			if (prop->type == _RK_C_PATLIST)
				typeinfo = "pattern list";
			if (prop->s2i[0].str) {
				rd_kafka_conf_flags2str(tmp, sizeof(tmp), ", ",
							prop, -1);
				fprintf(fp, "%-15s | %13s",
					tmp, prop->sdef ? prop->sdef : "");
			} else {
				fprintf(fp, "%-15s | %13s",
					"", prop->sdef ? prop->sdef : "");
			}
			break;
		case _RK_C_BOOL:
			typeinfo = "boolean";
			fprintf(fp, "%-15s | %13s", "true, false",
				prop->vdef ? "true" : "false");
			break;
		case _RK_C_INT:
			typeinfo = "integer";
			rd_snprintf(tmp, sizeof(tmp),
				    "%d .. %d", prop->vmin, prop->vmax);
			fprintf(fp, "%-15s | %13i", tmp, prop->vdef);
			break;
		case _RK_C_S2I:
			typeinfo = "enum value";
			rd_kafka_conf_flags2str(tmp, sizeof(tmp), ", ",
						prop, -1);
			fprintf(fp, "%-15s | ", tmp);

			for (j = 0 ; j < (int)RD_ARRAYSIZE(prop->s2i); j++) {
				if (prop->s2i[j].val == prop->vdef) {
					fprintf(fp, "%13s", prop->s2i[j].str);
					break;
				}
			}
			if (j == RD_ARRAYSIZE(prop->s2i))
				fprintf(fp, "%13s", " ");
			break;

		case _RK_C_S2F:
			typeinfo = "CSV flags";
			/* Dont duplicate builtin.features value in
			 * both Range and Default */
			if (!strcmp(prop->name, "builtin.features"))
				*tmp = '\0';
			else
				rd_kafka_conf_flags2str(tmp, sizeof(tmp), ", ",
							prop, -1);
			fprintf(fp, "%-15s | ", tmp);
			rd_kafka_conf_flags2str(tmp, sizeof(tmp), ", ",
						prop, prop->vdef);
			fprintf(fp, "%13s", tmp);

			break;

		case _RK_C_PTR:
			typeinfo = "pointer";
			/* FALLTHRU */
		default:
			fprintf(fp, "%-15s | %-13s", "", " ");
			break;
		}

		if (prop->type == _RK_C_ALIAS)
			fprintf(fp, " | Alias for `%s`\n", prop->sdef);
		else
			fprintf(fp, " | %s <br>*Type: %s*\n", prop->desc,
				typeinfo);
	}
	fprintf(fp, "\n");
        fprintf(fp, "### C/P legend: C = Consumer, P = Producer, * = both\n");
}

void streams_kafka_mapped_streams_config_set(rd_kafka_t *rk, bool isSubscribe, streams_config_t *config) {
  //create default stream config.
  streams_config_create (config);
  //map and set librdkafka config to streams.
  rd_kafka_conf_t conf = rk->rk_conf;
  if (conf.client_id_str)
    streams_config_set (*config, "client.id", conf.client_id_str);


  /* Producer configs*/
  char t_ms_str[16];
  memset (t_ms_str, 0, sizeof(t_ms_str));
  snprintf(t_ms_str, sizeof (t_ms_str), "%d", conf.metadata_refresh_fast_cnt);
  streams_config_set (*config, "metadata.max.age.ms", t_ms_str);

  if (conf.streams_producer_default_stream_name)
    streams_config_set (*config, "streams.producer.default.stream",
                        conf.streams_producer_default_stream_name);

  memset (t_ms_str, 0, sizeof(t_ms_str));
  snprintf(t_ms_str, sizeof (t_ms_str), "%d", conf.buffering_max_ms);
  streams_config_set (*config, "streams.buffer.max.time.ms", t_ms_str);

  streams_config_set (*config, "streams.parallel.flushers.per.partition",
                      conf.streams_parallel_flushers_per_partition?"true":"false");
  char size_str[16];
  memset (size_str, 0, sizeof(size_str));
  snprintf(size_str, sizeof (size_str), "%d", conf.max_msg_size);
  streams_config_set (*config, "message.max.bytes", size_str);

  /* Consumer configs*/
  if (isSubscribe && conf.group_id_str)
    streams_config_set (*config, "group.id", conf.group_id_str);

  streams_config_set (*config, "enable.auto.commit",
                      conf.enable_auto_commit?"true":"false");
  streams_config_set (*config, "streams.zerooffset.record.on.eof", "true");

  memset (t_ms_str, 0, sizeof(t_ms_str));
  snprintf(t_ms_str, sizeof (t_ms_str), "%d", conf.auto_commit_interval_ms);
  streams_config_set (*config, "auto.commit.interval.ms", t_ms_str);

  if (conf.topic_conf) {
   char offset_str[16];
   if (conf.topic_conf->auto_offset_reset == RD_KAFKA_OFFSET_BEGINNING)
     snprintf (offset_str, sizeof (offset_str), "earliest" );
   else if (conf.topic_conf->auto_offset_reset == RD_KAFKA_OFFSET_END)
     snprintf (offset_str, sizeof (offset_str), "latest" );
   else if (conf.topic_conf->auto_offset_reset == RD_KAFKA_OFFSET_INVALID)
     printf("NOT SUPPORTED");//throw error?

   streams_config_set (*config, "auto.offset.reset", offset_str);
  }

  if (conf.streams_consumer_default_stream_name)
    streams_config_set (*config, "streams.consumer.default.stream",
                        conf.streams_consumer_default_stream_name);
}

bool is_funct_present(const char *funct) {
  char *library = NULL; /*dlopen returns handle of main program if filename=NULL */
  char* dlerr;

  /* Library Handle */
  void* libHandle;
  libHandle = dlopen(library, RTLD_NOW | RTLD_GLOBAL);
  if (libHandle == NULL)
  {
    dlerr = dlerror();
    fprintf(stderr, "Failed to load librdkafka library %s: (dlopen)", dlerr);
    return false;
  }

  void *funcExists = dlsym(libHandle, funct);
  if (!funcExists)
    return false;

  return true;
}
