#pragma once

#include "gtosd/core/result.hpp"

#include <cstdint>
#include <string>

namespace gtosd {

enum class ArithmeticError : std::uint8_t { NegativeValue, PercentageOutOfRange, Overflow };

class RangeWeight {
public:
  constexpr RangeWeight() = default;
  [[nodiscard]] static Result<RangeWeight, ArithmeticError>
  from_basis_points(std::int64_t basis_points);
  [[nodiscard]] constexpr std::uint16_t basis_points() const noexcept { return basis_points_; }
  friend constexpr bool operator==(RangeWeight, RangeWeight) noexcept = default;

private:
  explicit constexpr RangeWeight(std::uint16_t basis_points) noexcept
      : basis_points_(basis_points) {}
  std::uint16_t basis_points_{0};
};

class PotPercentage {
public:
  constexpr PotPercentage() = default;
  [[nodiscard]] static Result<PotPercentage, ArithmeticError>
  from_basis_points(std::int64_t basis_points);
  [[nodiscard]] constexpr std::uint32_t basis_points() const noexcept { return basis_points_; }
  friend constexpr bool operator==(PotPercentage, PotPercentage) noexcept = default;

private:
  explicit constexpr PotPercentage(std::uint32_t basis_points) noexcept
      : basis_points_(basis_points) {}
  std::uint32_t basis_points_{0};
};

class Money {
public:
  static constexpr std::int64_t units_per_ante = 10'000;

  constexpr Money() = default;
  [[nodiscard]] static Result<Money, ArithmeticError> from_units(std::int64_t units);
  [[nodiscard]] static Result<Money, ArithmeticError> from_antes(std::int64_t antes);
  [[nodiscard]] constexpr std::int64_t units() const noexcept { return units_; }

  friend constexpr bool operator==(Money, Money) noexcept = default;
  friend constexpr auto operator<=>(Money, Money) noexcept = default;

private:
  struct Unchecked {};
  explicit constexpr Money(std::int64_t units, Unchecked) noexcept : units_(units) {}
  std::int64_t units_{0};
};

[[nodiscard]] Result<Money, ArithmeticError> add_checked(Money lhs, Money rhs);
[[nodiscard]] Result<Money, ArithmeticError> subtract_checked(Money lhs, Money rhs);
[[nodiscard]] Result<Money, ArithmeticError> percent_of(Money value, std::uint32_t basis_points,
                                                        std::uint32_t maximum_basis_points);
[[nodiscard]] std::string format_money(Money value);

} // namespace gtosd
