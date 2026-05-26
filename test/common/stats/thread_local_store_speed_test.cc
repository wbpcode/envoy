// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/thread.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/stats/allocator.h"
#include "source/common/stats/deferred_creation.h"
#include "source/common/stats/stats_matcher_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/tag_producer_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/common/stats/utility.h"
#include "source/common/thread_local/thread_local_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"

namespace Envoy {

class ThreadLocalStorePerf {
public:
  explicit ThreadLocalStorePerf(bool use_element_scope = false, bool populate_sample_stats = true)
      : heap_alloc_(symbol_table_), store_(heap_alloc_, use_element_scope),
        api_(Api::createApiForTest(store_, time_system_)) {
    const Stats::TagVector tags;
    store_.setTagProducer(Stats::TagProducerImpl::createTagProducer(stats_config_, tags).value());

    if (populate_sample_stats) {
      Stats::TestUtil::forEachSampleStat(1000, true, [this](absl::string_view name) {
        stat_names_.push_back(std::make_unique<Stats::StatNameManagedStorage>(name, symbol_table_));
      });
    }
  }

  virtual ~ThreadLocalStorePerf() {
    if (tls_) {
      tls_->shutdownGlobalThreading();
    }
    store_.shutdownThreading();
    if (tls_) {
      tls_->shutdownThread();
    }
    if (dispatcher_) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  void accessCounters() {
    Stats::Scope& scope = *store_.rootScope();
    for (auto& stat_name_storage : stat_names_) {
      scope.counterFromStatName(stat_name_storage->statName());
    }
  }

  // Element-API counterpart to accessCounters(). Each call constructs a
  // 1-element StatElementSpan via brace-init — the typical real-world pattern
  // for the new API — and on a legacy (non-element) scope hits the
  // size<=1 fast path in the StatElementSpan overload, which forwards
  // names[0].value to counterFromStatName without going through joinElementValues.
  void accessCountersViaElementApi() {
    Stats::Scope& scope = *store_.rootScope();
    for (auto& stat_name_storage : stat_names_) {
      scope.getOrCreateCounter({{.value = stat_name_storage->statName()}});
    }
  }

  void initThreading() {
    if (!Envoy::Event::Libevent::Global::initialized()) {
      Envoy::Event::Libevent::Global::initialize();
    }
    dispatcher_ = api_->allocateDispatcher("test_thread");
    tls_ = std::make_unique<ThreadLocal::InstanceImpl>();
    tls_->registerThread(*dispatcher_, true);
    store_.initializeThreading(*dispatcher_, *tls_);
  }

  void initPrefixRejections(const std::string& prefix) {
    stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
        prefix);
    store_.setStatsMatcher(
        std::make_unique<Stats::StatsMatcherImpl>(stats_config_, symbol_table_, context_));
  }

protected:
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Stats::SymbolTableImpl symbol_table_;
  Event::SimulatedTimeSystem time_system_;
  Stats::Allocator heap_alloc_;
  Event::DispatcherPtr dispatcher_;
  ThreadLocal::InstanceImplPtr tls_;
  Stats::ThreadLocalStoreImpl store_;
  Api::ApiPtr api_;
  envoy::config::metrics::v3::StatsConfig stats_config_;
  std::vector<std::unique_ptr<Stats::StatNameManagedStorage>> stat_names_;
};

// Drives end-to-end cluster stats creation through the same code paths used by
// ClusterInfoImpl: per-cluster scope plus traffic / config-update / endpoint / lb /
// request-response-size / timeout-budget stat structs, and default+high priority
// circuit-breakers gauges. defer_creation_ is forced false so traffic stats are
// materialized eagerly and the benchmark measures actual stat allocation cost
// rather than the deferred-wrapper overhead.
class ClusterStatsCreationPerf : public ThreadLocalStorePerf {
public:
  // A snapshot of every stat struct a typical cluster owns. Held by the caller
  // so caches behave like long-lived clusters, not transient ones.
  struct ClusterStats {
    Stats::ScopeSharedPtr scope;
    Upstream::DeferredCreationCompatibleClusterTrafficStats traffic;
    Upstream::ClusterConfigUpdateStats config_update;
    Upstream::ClusterEndpointStats endpoint;
    Upstream::ClusterLbStats lb;
    Upstream::ClusterCircuitBreakersStats circuit_breakers_default;
    Upstream::ClusterCircuitBreakersStats circuit_breakers_high;
    Upstream::ClusterRequestResponseSizeStats request_response_size;
    Upstream::ClusterTimeoutBudgetStats timeout_budget;
  };

  explicit ClusterStatsCreationPerf(bool use_element_scope)
      : ThreadLocalStorePerf(use_element_scope, /*populate_sample_stats=*/false),
        cluster_traffic_stat_names_(symbol_table_),
        cluster_config_update_stat_names_(symbol_table_),
        cluster_endpoint_stat_names_(symbol_table_), cluster_lb_stat_names_(symbol_table_),
        cluster_circuit_breakers_stat_names_(symbol_table_),
        cluster_request_response_size_stat_names_(symbol_table_),
        cluster_timeout_budget_stat_names_(symbol_table_) {}

  // Pre-populates the store with `num_clusters` real cluster stat sets so the
  // symbol table, allocator, and scope tables resemble a running server before
  // the timed creation begins. Warm up always uses the eager path so the central
  // cache ends up fully populated (matches a long-running server).
  void warmup(size_t num_clusters) {
    warmup_clusters_.reserve(warmup_clusters_.size() + num_clusters);
    for (size_t i = 0; i < num_clusters; ++i) {
      warmup_clusters_.push_back(createClusterStats(absl::StrCat("warmup_", i),
                                                    /*defer_creation=*/false,
                                                    /*touch_traffic_stats=*/false));
    }
  }

  // defer_creation toggles between DirectStats (eager — all traffic stats
  // allocated in the constructor) and DeferredStats (lazy — wrapper only).
  // touch_traffic_stats forces materialization of the deferred wrapper by
  // calling getOrCreate, modeling the cost a cluster pays on its first request
  // when deferred-creation is enabled.
  ClusterStats createClusterStats(absl::string_view cluster_name, bool defer_creation,
                                  bool touch_traffic_stats) {
    Stats::ScopeSharedPtr scope =
        store_.rootScope()->createScope(absl::StrCat("cluster.", cluster_name, "."));
    auto traffic = Stats::createDeferredCompatibleStats<Upstream::ClusterTrafficStats>(
        scope, cluster_traffic_stat_names_, defer_creation);
    if (touch_traffic_stats) {
      // operator* forwards to getOrCreate on the underlying interface, forcing
      // materialization of the deferred StatsStructType.
      benchmark::DoNotOptimize(&(*traffic));
    }
    return ClusterStats{
        scope,
        std::move(traffic),
        Upstream::ClusterConfigUpdateStats{cluster_config_update_stat_names_, *scope},
        Upstream::ClusterEndpointStats{cluster_endpoint_stat_names_, *scope},
        Upstream::ClusterLbStats{cluster_lb_stat_names_, *scope},
        makeCircuitBreakersStats(*scope, cluster_circuit_breakers_stat_names_.default_),
        makeCircuitBreakersStats(*scope, cluster_circuit_breakers_stat_names_.high_),
        Upstream::ClusterRequestResponseSizeStats{cluster_request_response_size_stat_names_,
                                                  *scope},
        Upstream::ClusterTimeoutBudgetStats{cluster_timeout_budget_stat_names_, *scope},
    };
  }

private:
  // Mirrors ClusterInfoImpl::generateCircuitBreakersStats with track_remaining=true,
  // joining circuit_breakers.{priority}.{stat} via element-backed scopes — the same
  // path production uses for these gauges.
  Upstream::ClusterCircuitBreakersStats makeCircuitBreakersStats(Stats::Scope& scope,
                                                                 Stats::StatName priority) {
    const auto& sn = cluster_circuit_breakers_stat_names_;
    auto make_gauge = [&](Stats::StatName name) -> Stats::Gauge& {
      return Stats::Utility::gaugeFromElements(scope, {sn.circuit_breakers_, priority, name},
                                               Stats::Gauge::ImportMode::Accumulate);
    };
    return Upstream::ClusterCircuitBreakersStats{
        make_gauge(sn.cx_open_),
        make_gauge(sn.cx_pool_open_),
        make_gauge(sn.rq_open_),
        make_gauge(sn.rq_pending_open_),
        make_gauge(sn.rq_retry_open_),
        make_gauge(sn.remaining_cx_),
        make_gauge(sn.remaining_cx_pools_),
        make_gauge(sn.remaining_pending_),
        make_gauge(sn.remaining_retries_),
        make_gauge(sn.remaining_rq_),
    };
  }

  Upstream::ClusterTrafficStatNames cluster_traffic_stat_names_;
  Upstream::ClusterConfigUpdateStatNames cluster_config_update_stat_names_;
  Upstream::ClusterEndpointStatNames cluster_endpoint_stat_names_;
  Upstream::ClusterLbStatNames cluster_lb_stat_names_;
  Upstream::ClusterCircuitBreakersStatNames cluster_circuit_breakers_stat_names_;
  Upstream::ClusterRequestResponseSizeStatNames cluster_request_response_size_stat_names_;
  Upstream::ClusterTimeoutBudgetStatNames cluster_timeout_budget_stat_names_;

  std::vector<ClusterStats> warmup_clusters_;
};

void runCreateClusterStatsBenchmark(benchmark::State& state, bool use_element_scope,
                                    bool defer_creation, bool touch_traffic_stats) {
  ClusterStatsCreationPerf perf(use_element_scope);
  perf.initThreading();
  // Warm the symbol table, allocator, and central scope map with a realistic
  // number of pre-existing clusters so the timed creations measure steady-state
  // cost rather than first-cluster-ever cost.
  perf.warmup(static_cast<size_t>(state.range(0)));

  int64_t i = 0;
  for (auto _ : state) { // NOLINT
    // Unique name per iteration so every measurement is an actual creation —
    // otherwise iteration 2+ would hit the central cache and be a no-op.
    auto cluster =
        perf.createClusterStats(absl::StrCat("bench_", i++), defer_creation, touch_traffic_stats);
    benchmark::DoNotOptimize(&cluster);
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations());
}

} // namespace Envoy

// Tests the single-threaded performance of the thread-local-store stats caches
// without having initialized tls.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StatsNoTls(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context;

  for (auto _ : state) { // NOLINT
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsNoTls);

// Tests the single-threaded performance of the thread-local-store stats caches
// with tls. Note that this test is still single-threaded, and so there's only
// one replica of the tls cache.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StatsWithTls(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context;
  context.initThreading();

  for (auto _ : state) { // NOLINT
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsWithTls);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StatsWithTlsAndRejectionsWithDot(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context;
  context.initThreading();
  context.initPrefixRejections("cluster.");

  for (auto _ : state) { // NOLINT
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsWithTlsAndRejectionsWithDot);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StatsWithTlsAndRejectionsWithoutDot(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context;
  context.initThreading();
  context.initPrefixRejections("cluster");

  for (auto _ : state) { // NOLINT
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsWithTlsAndRejectionsWithoutDot);

// End-to-end cluster stat creation via the legacy string-backed scope path,
// exercising the same struct constructors ClusterInfoImpl uses. Traffic stats
// are constructed eagerly (DirectStats), matching the non-deferred config.
// state.range(0) is the number of pre-warmed clusters created before timing
// starts.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_CreateClusterStatsWithTlsLegacyScope(benchmark::State& state) {
  Envoy::runCreateClusterStatsBenchmark(state, /*use_element_scope=*/false,
                                        /*defer_creation=*/false,
                                        /*touch_traffic_stats=*/false);
}
BENCHMARK(BM_CreateClusterStatsWithTlsLegacyScope)->Arg(0)->Arg(100)->Arg(1000);

// Same as above but via the element-backed scope path. This is the headline
// before/after comparison for the per-scope stat creation work.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_CreateClusterStatsWithTlsElementScope(benchmark::State& state) {
  Envoy::runCreateClusterStatsBenchmark(state, /*use_element_scope=*/true,
                                        /*defer_creation=*/false,
                                        /*touch_traffic_stats=*/false);
}
BENCHMARK(BM_CreateClusterStatsWithTlsElementScope)->Arg(0)->Arg(100)->Arg(1000);

// Paired cache-hit benchmarks for the new element API vs the old StatName API
// on a legacy (use_element_scope=false) scope. Same 1000 pre-allocated stats
// in both, same store, same loop body shape — the only difference is which
// API is invoked. Measures the per-call overhead of the new API's legacy
// fallback (specifically the 1-element fast path in
// ScopeImpl::getOrCreateCounter(StatElementSpan)) against a direct StatName
// lookup. Sister of BM_StatsWithTls.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_LegacyScopeOldApiCounterFromStatName(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context(/*use_element_scope=*/false);
  context.initThreading();
  for (auto _ : state) { // NOLINT
    context.accessCounters();
  }
}
BENCHMARK(BM_LegacyScopeOldApiCounterFromStatName);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_LegacyScopeNewApiGetOrCreateCounter(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context(/*use_element_scope=*/false);
  context.initThreading();
  for (auto _ : state) { // NOLINT
    context.accessCountersViaElementApi();
  }
}
BENCHMARK(BM_LegacyScopeNewApiGetOrCreateCounter);

// TODO(jmarantz): add multi-threaded variant of this test, that aggressively
// looks up stats in multiple threads to try to trigger contention issues.
