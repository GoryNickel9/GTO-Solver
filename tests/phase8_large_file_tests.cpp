#include "gtosd/storage/storage.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(const bool condition, const char *const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

int main() {
  const auto path = std::filesystem::current_path() / "phase8_simulated_250mb.gtsd";
  try {
    constexpr std::size_t payload_size = 250U * 1024U * 1024U;
    std::vector<std::byte> payload(payload_size);
    std::uint64_t state = 0x4d595df4d0f33173ULL;
    for (auto &value : payload) {
      state ^= state >> 12U;
      state ^= state << 25U;
      state ^= state >> 27U;
      value = static_cast<std::byte>((state * 0x2545f4914f6cdd1dULL) >> 56U);
    }
    gtosd::SolutionArchive archive;
    archive.features = static_cast<std::uint64_t>(gtosd::SolutionFeature::ExactStrategy);
    archive.chunks.push_back({gtosd::SolutionChunkType::Strategy, std::move(payload)});
    const auto key = gtosd::generate_storage_key();
    const auto saved = gtosd::save_solution(path, archive, key, 1);
    require(saved.has_value(), "250 MB simulated solution saves");
    const auto reader = gtosd::open_solution(path, key);
    require(reader.has_value(), "250 MB simulated solution opens");
    require(reader.value().metrics().file_size >= 249U * 1024U * 1024U,
            "simulated file is approximately 250 MB");
    require(reader.value().metrics().peak_open_bytes < 1024U,
            "250 MB root opens with bounded index memory");
    std::cout << "F8_LARGE_FILE_TEST=PASS"
              << " file_bytes=" << reader.value().metrics().file_size
              << " raw_bytes=" << reader.value().metrics().raw_size
              << " compressed_bytes=" << reader.value().metrics().compressed_size
              << " peak_open_bytes=" << reader.value().metrics().peak_open_bytes << '\n';
    std::error_code error;
    std::filesystem::remove(path, error);
    require(!error, "250 MB simulated file is removed");
    return 0;
  } catch (const std::exception &error) {
    std::error_code remove_error;
    std::filesystem::remove(path, remove_error);
    std::cerr << "F8_LARGE_FILE_TEST=FAIL error=" << error.what() << '\n';
    return 1;
  }
}
