#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Card abstraction for the Short Deck preflop blueprint solver.
//
// P0 scaffold: this header only fixes the library identity that every
// precomputed resource (canonical catalogs, exact features, bucket tables)
// will embed in its fingerprint. The canonicalization, feature and clustering
// APIs are added by phases P1-P3 of the roadmap.
namespace gtosd::card_abstraction {

inline constexpr std::string_view library_id = "gtosd.card_abstraction";
inline constexpr std::uint32_t format_major = 0U;
inline constexpr std::uint32_t format_minor = 1U;

// Deck identity. The abstraction depends only on the deck and the hand
// ranking, never on stack, sizes or the number of players.
inline constexpr std::string_view deck_id = "short_deck_36";
inline constexpr std::size_t deck_size = 36U;
inline constexpr std::size_t hole_card_combos = 630U;
inline constexpr std::size_t preflop_hand_classes = 81U;

// "gtosd.card_abstraction/0.1|short_deck_36|<ruleset fingerprint>". The
// ruleset fingerprint is the one exported by the exact seven-card table so a
// resource built for a different ranking can never be mistaken for ours.
[[nodiscard]] std::string library_identity();

} // namespace gtosd::card_abstraction
