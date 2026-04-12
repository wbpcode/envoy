#pragma once

#include "envoy/stats/context.h"

#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Stats {

#define ALL_WELL_KNOWN_TAG_STAT_NAMES(GENERATE)                                                    \
  GENERATE(cluster_name, CLUSTER_NAME)                                                             \
  GENERATE(listener_address, LISTENER_ADDRESS)                                                     \
  GENERATE(http_conn_manager_prefix, HTTP_CONN_MANAGER_PREFIX)                                     \
  GENERATE(http_user_agent, HTTP_USER_AGENT)                                                       \
  GENERATE(ssl_cipher, SSL_CIPHER)                                                                 \
  GENERATE(ssl_curve, SSL_CURVE)                                                                   \
  GENERATE(ssl_sigalg, SSL_SIGALG)                                                                 \
  GENERATE(ssl_version, SSL_VERSION)                                                               \
  GENERATE(ssl_cipher_suite, SSL_CIPHER_SUITE)                                                     \
  GENERATE(clientssl_prefix, CLIENTSSL_PREFIX)                                                     \
  GENERATE(mongo_prefix, MONGO_PREFIX)                                                             \
  GENERATE(mongo_cmd, MONGO_CMD)                                                                   \
  GENERATE(mongo_collection, MONGO_COLLECTION)                                                     \
  GENERATE(mongo_callsite, MONGO_CALLSITE)                                                         \
  GENERATE(ratelimit_prefix, RATELIMIT_PREFIX)                                                     \
  GENERATE(local_http_ratelimit_prefix, LOCAL_HTTP_RATELIMIT_PREFIX)                               \
  GENERATE(local_network_ratelimit_prefix, LOCAL_NETWORK_RATELIMIT_PREFIX)                         \
  GENERATE(local_listener_ratelimit_prefix, LOCAL_LISTENER_RATELIMIT_PREFIX)                       \
  GENERATE(dns_filter_prefix, DNS_FILTER_PREFIX)                                                   \
  GENERATE(connection_limit_prefix, CONNECTION_LIMIT_PREFIX)                                       \
  GENERATE(rbac_prefix, RBAC_PREFIX)                                                               \
  GENERATE(rbac_http_prefix, RBAC_HTTP_PREFIX)                                                     \
  GENERATE(rbac_policy_name, RBAC_POLICY_NAME)                                                     \
  GENERATE(tcp_prefix, TCP_PREFIX)                                                                 \
  GENERATE(udp_prefix, UDP_PREFIX)                                                                 \
  GENERATE(fault_downstream_cluster, FAULT_DOWNSTREAM_CLUSTER)                                     \
  GENERATE(dynamo_operation, DYNAMO_OPERATION)                                                     \
  GENERATE(dynamo_table, DYNAMO_TABLE)                                                             \
  GENERATE(dynamo_partition_id, DYNAMO_PARTITION_ID)                                               \
  GENERATE(grpc_bridge_service, GRPC_BRIDGE_SERVICE)                                               \
  GENERATE(grpc_bridge_method, GRPC_BRIDGE_METHOD)                                                 \
  GENERATE(virtual_host, VIRTUAL_HOST)                                                             \
  GENERATE(virtual_cluster, VIRTUAL_CLUSTER)                                                       \
  GENERATE(response_code, RESPONSE_CODE)                                                           \
  GENERATE(response_code_class, RESPONSE_CODE_CLASS)                                               \
  GENERATE(rds_route_config, RDS_ROUTE_CONFIG)                                                     \
  GENERATE(scoped_rds_config, SCOPED_RDS_CONFIG)                                                   \
  GENERATE(route, ROUTE)                                                                           \
  GENERATE(ext_authz_prefix, EXT_AUTHZ_PREFIX)                                                     \
  GENERATE(worker_id, WORKER_ID)                                                                   \
  GENERATE(thrift_prefix, THRIFT_PREFIX)                                                           \
  GENERATE(redis_prefix, REDIS_PREFIX)                                                             \
  GENERATE(proxy_protocol_version, PROXY_PROTOCOL_VERSION)                                         \
  GENERATE(proxy_protocol_prefix, PROXY_PROTOCOL_PREFIX)                                           \
  GENERATE(google_grpc_client_prefix, GOOGLE_GRPC_CLIENT_PREFIX)                                   \
  GENERATE(tls_certificate, TLS_CERTIFICATE)                                                       \
  GENERATE(xds_resource_name, XDS_RESOURCE_NAME)

struct WellKnownTagStatNames {
  explicit WellKnownTagStatNames(SymbolTable& symbol_table);

  StatNamePool pool_;

#define DECLARE_WELL_KNOWN_TAG_STAT_NAME(name, unused) const StatName name##_;
  ALL_WELL_KNOWN_TAG_STAT_NAMES(DECLARE_WELL_KNOWN_TAG_STAT_NAME)
#undef DECLARE_WELL_KNOWN_TAG_STAT_NAME
};

class ContextImpl : public Context {
public:
  explicit ContextImpl(SymbolTable& symbol_table);
  ~ContextImpl() override = default;

  const WellKnownTagStatNames& wellKnownTagStatNames() const override {
    return well_known_tag_stat_names_;
  }

private:
  const WellKnownTagStatNames well_known_tag_stat_names_;
};

} // namespace Stats
} // namespace Envoy
