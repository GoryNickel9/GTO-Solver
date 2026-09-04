#include "gtosd/gui_prototype/gui_prototype.hpp"
#include "gtosd/version.hpp"

#include <algorithm>

int main() {
  const auto key = gtosd::generate_storage_key();
  const bool nonzero =
      std::any_of(key.begin(), key.end(), [](const auto value) { return value != 0U; });
  const auto dpi = gtosd::make_gui_dpi_layout(150U);
  return gtosd::api_version_minor == 9U && nonzero && dpi.has_value() &&
                 dpi.value().matrix_extent_pixels == 675U
             ? 0
             : 1;
}
