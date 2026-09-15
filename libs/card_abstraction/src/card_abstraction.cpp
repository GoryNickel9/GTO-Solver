#include "gtosd/card_abstraction/card_abstraction.hpp"

#include "gtosd/equity/seven_card_table.hpp"

#include <string>

namespace gtosd::card_abstraction {

std::string library_identity() {
  std::string identity(library_id);
  identity += '/';
  identity += std::to_string(format_major);
  identity += '.';
  identity += std::to_string(format_minor);
  identity += '|';
  identity += deck_id;
  identity += '|';
  identity += std::string(seven_card_table_ruleset_fingerprint);
  return identity;
}

} // namespace gtosd::card_abstraction
