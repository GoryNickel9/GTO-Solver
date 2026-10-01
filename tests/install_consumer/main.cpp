#include "gtosd/storage/storage.hpp"
#include "gtosd/version.hpp"

#include <algorithm>

int main() {
  const auto key = gtosd::generate_storage_key();
  const bool nonzero =
      std::any_of(key.begin(), key.end(), [](const auto value) { return value != 0U; });
  return gtosd::api_version_minor == 9U && nonzero ? 0 : 1;
}
