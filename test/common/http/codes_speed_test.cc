// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/stats/stats.h"

#include "source/common/http/codes.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/http/header_map_impl.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Http {

// template <class SymbolTableClass> class CodeUtilitySpeedTest {
// public:
//   CodeUtilitySpeedTest()
//       : global_store_(symbol_table_), cluster_scope_(symbol_table_), code_stats_(symbol_table_),
//         pool_(symbol_table_), from_az_(pool_.add("from_az")), prefix_(pool_.add("prefix")),
//         req_vcluster_name_(pool_.add("req_vcluster_name")),
//         test_cluster_(pool_.add("test-cluster")), test_vhost_(pool_.add("test-vhost")),
//         to_az_(pool_.add("to_az")), vhost_name_(pool_.add("vhost_name")) {}

//   void addResponse(uint64_t code, bool canary, bool internal_request,
//                    Stats::StatName request_vhost_name = Stats::StatName(),
//                    Stats::StatName request_vcluster_name = Stats::StatName(),
//                    Stats::StatName request_route_name = Stats::StatName(),
//                    Stats::StatName from_az = Stats::StatName(),
//                    Stats::StatName to_az = Stats::StatName()) {
//     Http::CodeStats::ResponseStatInfo info{global_store_,
//                                            cluster_scope_,
//                                            prefix_,
//                                            code,
//                                            internal_request,
//                                            request_vhost_name,
//                                            request_route_name,
//                                            request_vcluster_name,
//                                            from_az,
//                                            to_az,
//                                            canary};

//     code_stats_.chargeResponseStat(info, false);
//   }

//   void addResponses() {
//     addResponse(201, false, false);
//     addResponse(301, false, true);
//     addResponse(401, false, false);
//     addResponse(501, false, true);
//     addResponse(200, true, true);
//     addResponse(300, false, false);
//     Stats::StatName empty_stat_name;
//     addResponse(500, true, false);
//     addResponse(200, false, false, test_vhost_, test_cluster_);
//     addResponse(200, false, false, empty_stat_name, empty_stat_name, from_az_, to_az_);
//   }

//   void responseTiming() {
//     Stats::StatName empty_stat_name;
//     Http::CodeStats::ResponseTimingInfo info{
//         global_store_, cluster_scope_, prefix_,         std::chrono::milliseconds(5), true,
//         true,          vhost_name_,    empty_stat_name, req_vcluster_name_,           from_az_,
//         to_az_};
//     code_stats_.chargeResponseTiming(info);
//   }

//   SymbolTableClass symbol_table_;
//   Stats::IsolatedStoreImpl global_store_;
//   Stats::IsolatedStoreImpl cluster_scope_;
//   Http::CodeStatsImpl code_stats_;
//   Stats::StatNamePool pool_;
//   const Stats::StatName from_az_;
//   const Stats::StatName prefix_;
//   const Stats::StatName req_vcluster_name_;
//   const Stats::StatName test_cluster_;
//   const Stats::StatName test_vhost_;
//   const Stats::StatName to_az_;
//   const Stats::StatName vhost_name_;
// };

static void newHeaderStringVsHeaderStringSetReference(benchmark::State& state) {
  const uint64_t new_header_string = state.range(0);
  const std::string data(256, 'a');

  if (new_header_string == 0) {
    for (auto _ : state) {
      for (size_t i = 0; i < 128; i++) {
        Http::HeaderString a(data);
      }
    }
  } else {
    for (auto _ : state) {
      for (size_t i = 0; i < 128; i++) {
        Http::NewHeaderString a(data);
      }
    }
  }
}

BENCHMARK(newHeaderStringVsHeaderStringSetReference)->Args({0})->Args({1});

static void newHeaderStringVsHeaderStringSetCopy(benchmark::State& state) {
  const uint64_t new_header_string = state.range(0);
  const uint64_t size = state.range(1);
  const std::string data(size, 'a');

  if (new_header_string == 0) {
    for (auto _ : state) {
      for (size_t i = 0; i < 128; i++) {
        Http::HeaderString a;
        a.setCopy(data);
      }
    }
  } else {
    for (auto _ : state) {
      for (size_t i = 0; i < 128; i++) {
        Http::NewHeaderString a;
        a.setCopy(data);
      }
    }
  }
}

BENCHMARK(newHeaderStringVsHeaderStringSetCopy)
    ->Args({0, 32})
    ->Args({0, 64})
    ->Args({0, 128})
    ->Args({0, 256})
    ->Args({0, 512})
    ->Args({1, 32})
    ->Args({1, 64})
    ->Args({1, 128})
    ->Args({1, 256})
    ->Args({1, 512});

static void newHeaderStringVsHeaderStringRefMove(benchmark::State& state) {
  const uint64_t new_header_string = state.range(0);
  const uint64_t size = state.range(1);
  const std::string data(size, 'a');

  if (new_header_string == 0) {
    for (auto _ : state) {
      for (size_t i = 0; i < 128; i++) {
        Http::HeaderString a(data);
        Http::HeaderString b(std::move(a));
      }
    }
  } else {
    for (auto _ : state) {
      for (size_t i = 0; i < 128; i++) {
        Http::NewHeaderString a(data);
        Http::NewHeaderString b(std::move(a));
      }
    }
  }
}

BENCHMARK(newHeaderStringVsHeaderStringRefMove)
    ->Args({0, 32})
    ->Args({0, 64})
    ->Args({0, 128})
    ->Args({0, 256})
    ->Args({0, 512})
    ->Args({1, 32})
    ->Args({1, 64})
    ->Args({1, 128})
    ->Args({1, 256})
    ->Args({1, 512});

static void newHeaderStringVsHeaderStringInlineMove(benchmark::State& state) {
  const uint64_t new_header_string = state.range(0);
  const uint64_t size = state.range(1);
  const std::string data(size, 'a');

  if (new_header_string == 0) {
    for (auto _ : state) {
      for (size_t i = 0; i < 128; i++) {
        Http::HeaderString a;
        a.setCopy(data);
        Http::HeaderString b(std::move(a));
      }
    }
  } else {
    for (auto _ : state) {
      for (size_t i = 0; i < 128; i++) {
        Http::NewHeaderString a;
        a.setCopy(data);
        Http::NewHeaderString b(std::move(a));
      }
    }
  }
}

BENCHMARK(newHeaderStringVsHeaderStringInlineMove)
    ->Args({0, 32})
    ->Args({0, 64})
    ->Args({0, 128})
    ->Args({0, 256})
    ->Args({0, 512})
    ->Args({1, 32})
    ->Args({1, 64})
    ->Args({1, 128})
    ->Args({1, 256})
    ->Args({1, 512});

} // namespace Http
} // namespace Envoy

// // NOLINTNEXTLINE(readability-identifier-naming)
// static void BM_AddResponsesRealSymtab(benchmark::State& state) {
//   Envoy::Http::CodeUtilitySpeedTest<Envoy::Stats::SymbolTableImpl> context;

//   for (auto _ : state) {
//     context.addResponses();
//   }
// }
// BENCHMARK(BM_AddResponsesRealSymtab);

// // NOLINTNEXTLINE(readability-identifier-naming)
// static void BM_ResponseTimingRealSymtab(benchmark::State& state) {
//   Envoy::Http::CodeUtilitySpeedTest<Envoy::Stats::SymbolTableImpl> context;

//   for (auto _ : state) {
//     context.responseTiming();
//   }
// }
// BENCHMARK(BM_ResponseTimingRealSymtab);
