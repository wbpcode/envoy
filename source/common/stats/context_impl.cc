#include "source/common/stats/context_impl.h"

#include "source/common/config/well_known_names.h"

namespace Envoy {
namespace Stats {

#define INITIALIZE_WELL_KNOWN_TAG_STAT_NAME(name, field)                                           \
  , name##_(pool_.add(Config::TagNames::get().field))

WellKnownTagStatNames::WellKnownTagStatNames(SymbolTable& symbol_table)
    : pool_(symbol_table) ALL_WELL_KNOWN_TAG_STAT_NAMES(INITIALIZE_WELL_KNOWN_TAG_STAT_NAME) {}

#undef INITIALIZE_WELL_KNOWN_TAG_STAT_NAME

ContextImpl::ContextImpl(SymbolTable& symbol_table) : well_known_tag_stat_names_(symbol_table) {}

} // namespace Stats
} // namespace Envoy
