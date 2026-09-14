#include "gtosd/equity/seven_card_table.hpp"
#include "gtosd/preflop/hu_preflop.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::uint64_t assertions = 0U;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

template <typename Function> void for_each_seven(Function &&function) {
  const auto deck = gtosd::short_deck();
  for (std::uint8_t a = 0U; a < 30U; ++a) {
    for (std::uint8_t b = static_cast<std::uint8_t>(a + 1U); b < 31U; ++b) {
      for (std::uint8_t c = static_cast<std::uint8_t>(b + 1U); c < 32U; ++c) {
        for (std::uint8_t d = static_cast<std::uint8_t>(c + 1U); d < 33U; ++d) {
          for (std::uint8_t e = static_cast<std::uint8_t>(d + 1U); e < 34U; ++e) {
            for (std::uint8_t f = static_cast<std::uint8_t>(e + 1U); f < 35U; ++f) {
              for (std::uint8_t g = static_cast<std::uint8_t>(f + 1U); g < 36U; ++g) {
                function(std::array<gtosd::CardId, 7>{deck[a], deck[b], deck[c], deck[d], deck[e],
                                                      deck[f], deck[g]});
              }
            }
          }
        }
      }
    }
  }
}

double seconds_since(const std::chrono::steady_clock::time_point started) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
}

} // namespace

int main(const int argc, char **argv) {
  try {
    const auto output =
        argc > 1 ? std::filesystem::path{argv[1]}
                 : std::filesystem::temp_directory_path() / "gtosd_r3_seven_card_table_test.bin";
    const auto first_cards =
        std::array{gtosd::CardId::from_index(0U).value(), gtosd::CardId::from_index(1U).value(),
                   gtosd::CardId::from_index(2U).value(), gtosd::CardId::from_index(3U).value(),
                   gtosd::CardId::from_index(4U).value(), gtosd::CardId::from_index(5U).value(),
                   gtosd::CardId::from_index(6U).value()};
    const auto last_cards =
        std::array{gtosd::CardId::from_index(29U).value(), gtosd::CardId::from_index(30U).value(),
                   gtosd::CardId::from_index(31U).value(), gtosd::CardId::from_index(32U).value(),
                   gtosd::CardId::from_index(33U).value(), gtosd::CardId::from_index(34U).value(),
                   gtosd::CardId::from_index(35U).value()};
    require(gtosd::seven_card_combination_index(first_cards).value() == 0U,
            "first combinadic index is zero");
    require(gtosd::seven_card_combination_index(last_cards).value() ==
                gtosd::seven_card_table_entry_count - 1U,
            "last combinadic index is dense");
    auto duplicate = first_cards;
    duplicate[6] = duplicate[0];
    require(!gtosd::seven_card_combination_index(duplicate),
            "duplicate card is rejected by the table index");

    auto started = std::chrono::steady_clock::now();
    auto built = gtosd::SevenCardLookupTable::build();
    const auto build_seconds = seconds_since(started);
    require(built.has_value(), "seven-card table builds");
    require(built.value().entry_count() == gtosd::seven_card_table_entry_count &&
                built.value().payload_bytes() ==
                    gtosd::seven_card_table_entry_count * sizeof(std::uint32_t) &&
                built.value().checksum() != 0U,
            "table has exact dense payload and checksum");

    started = std::chrono::steady_clock::now();
    const auto saved = built.value().save(output);
    const auto save_seconds = seconds_since(started);
    require(saved.has_value(), "seven-card table saves atomically");
    started = std::chrono::steady_clock::now();
    auto loaded = gtosd::SevenCardLookupTable::load(output);
    const auto load_seconds = seconds_since(started);
    require(loaded.has_value() && loaded.value().checksum() == built.value().checksum(),
            "saved table reloads with the same checksum");

    std::uint64_t compared = 0U;
    started = std::chrono::steady_clock::now();
    for_each_seven([&](const auto &cards) {
      const auto oracle = gtosd::evaluate_seven(cards);
      const auto table = loaded.value().evaluate_seven(cards);
      if (!oracle || !table || oracle.value() != table.value()) {
        throw std::runtime_error("table differs from exact oracle");
      }
      ++compared;
    });
    const auto exhaustive_seconds = seconds_since(started);
    require(compared == gtosd::seven_card_table_entry_count,
            "all 8,347,680 seven-card combinations match the oracle");

    std::uint64_t warm_digest = 0U;
    started = std::chrono::steady_clock::now();
    for_each_seven([&](const auto &cards) {
      const auto table = loaded.value().evaluate_seven(cards);
      if (!table) {
        throw std::runtime_error("warm lookup failed");
      }
      warm_digest +=
          static_cast<std::uint8_t>(table.value().category) + table.value().kickers.front();
    });
    const auto warm_seconds = seconds_since(started);
    require(warm_digest != 0U, "warm lookup pass produces a nonzero digest");

    const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config());
    require(tree.has_value(), "CO40 tree builds for table-backed trainer comparison");
    gtosd::HuPreflopSolveOptions table_options;
    table_options.iterations = 16U;
    table_options.evaluation_deals = 32U;
    table_options.best_response_iterations = 8U;
    table_options.best_response_evaluation_deals = 32U;
    table_options.equity_samples_per_bucket = 0U;
    table_options.sampling_algorithm = gtosd::HuPreflopSamplingAlgorithm::ExternalSampling;
    table_options.postflop_representation = gtosd::HuPreflopPostflopRepresentation::ExactPhysical;
    table_options.seven_card_table_path = output.string();
    const auto table_solve = gtosd::solve_hu_preflop_sampled(tree.value(), table_options);
    auto oracle_options = table_options;
    oracle_options.seven_card_table_path.clear();
    const auto oracle_solve = gtosd::solve_hu_preflop_sampled(tree.value(), oracle_options);
    require(table_solve.has_value() && oracle_solve.has_value() &&
                table_solve.value().root_strategy == oracle_solve.value().root_strategy &&
                table_solve.value().root_ev_ante == oracle_solve.value().root_ev_ante,
            "table-backed trainer preserves the oracle policy and utility");
    require(table_solve.value().winner_cache_hits > 0U &&
                table_solve.value().winner_cache_misses > 0U &&
                table_solve.value().evaluator_table_payload_bytes ==
                    gtosd::seven_card_table_entry_count * sizeof(std::uint32_t),
            "trainer records winner-cache reuse and table payload");

    {
      std::fstream corrupt(output, std::ios::binary | std::ios::in | std::ios::out);
      require(static_cast<bool>(corrupt), "table resource opens for corruption test");
      corrupt.seekg(-1, std::ios::end);
      const auto original = corrupt.get();
      corrupt.seekp(-1, std::ios::end);
      corrupt.put(static_cast<char>(original ^ 0x5A));
    }
    require(!gtosd::SevenCardLookupTable::load(output),
            "payload corruption is rejected by checksum validation");
    if (argc > 1) {
      require(built.value().save(output).has_value(),
              "requested benchmark resource is restored after the corruption test");
    } else {
      std::error_code cleanup_error;
      std::filesystem::remove(output, cleanup_error);
    }

    std::cout << "R3_SEVEN_CARD_TABLE_TESTS=PASS\n"
              << "entries=" << compared << " payload_bytes=" << built.value().payload_bytes()
              << " checksum=" << built.value().checksum()
              << " ruleset=" << built.value().ruleset_fingerprint()
              << " build_seconds=" << build_seconds << " save_seconds=" << save_seconds
              << " load_seconds=" << load_seconds
              << " exhaustive_compare_seconds=" << exhaustive_seconds
              << " warm_lookup_seconds=" << warm_seconds << " assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "R3_SEVEN_CARD_TABLE_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  }
}
