#include "gtosd/core/money.hpp"

#include <iomanip>
#include <limits>
#include <sstream>

namespace gtosd {

Result<RangeWeight, ArithmeticError>
RangeWeight::from_basis_points(const std::int64_t basis_points) {
  if (basis_points < 0) {
    return Result<RangeWeight, ArithmeticError>::failure(ArithmeticError::NegativeValue);
  }
  if (basis_points > 10'000) {
    return Result<RangeWeight, ArithmeticError>::failure(ArithmeticError::PercentageOutOfRange);
  }
  return Result<RangeWeight, ArithmeticError>::success(
      RangeWeight(static_cast<std::uint16_t>(basis_points)));
}

Result<PotPercentage, ArithmeticError>
PotPercentage::from_basis_points(const std::int64_t basis_points) {
  if (basis_points < 0) {
    return Result<PotPercentage, ArithmeticError>::failure(ArithmeticError::NegativeValue);
  }
  if (basis_points > 100'000) {
    return Result<PotPercentage, ArithmeticError>::failure(ArithmeticError::PercentageOutOfRange);
  }
  return Result<PotPercentage, ArithmeticError>::success(
      PotPercentage(static_cast<std::uint32_t>(basis_points)));
}

Result<Money, ArithmeticError> Money::from_units(const std::int64_t units) {
  if (units < 0) {
    return Result<Money, ArithmeticError>::failure(ArithmeticError::NegativeValue);
  }
  return Result<Money, ArithmeticError>::success(Money(units, Unchecked{}));
}

Result<Money, ArithmeticError> Money::from_antes(const std::int64_t antes) {
  if (antes < 0 || antes > std::numeric_limits<std::int64_t>::max() / units_per_ante) {
    return Result<Money, ArithmeticError>::failure(antes < 0 ? ArithmeticError::NegativeValue
                                                             : ArithmeticError::Overflow);
  }
  return from_units(antes * units_per_ante);
}

Result<Money, ArithmeticError> add_checked(const Money lhs, const Money rhs) {
  if (lhs.units() > std::numeric_limits<std::int64_t>::max() - rhs.units()) {
    return Result<Money, ArithmeticError>::failure(ArithmeticError::Overflow);
  }
  return Money::from_units(lhs.units() + rhs.units());
}

Result<Money, ArithmeticError> subtract_checked(const Money lhs, const Money rhs) {
  if (lhs.units() < rhs.units()) {
    return Result<Money, ArithmeticError>::failure(ArithmeticError::NegativeValue);
  }
  return Money::from_units(lhs.units() - rhs.units());
}

Result<Money, ArithmeticError> percent_of(const Money value, const std::uint32_t basis_points,
                                          const std::uint32_t maximum_basis_points) {
  constexpr std::int64_t divisor = 10'000;
  if (basis_points > maximum_basis_points) {
    return Result<Money, ArithmeticError>::failure(ArithmeticError::PercentageOutOfRange);
  }

  // Exact half-up mul/div without overflowing a 64-bit intermediate.
  const auto whole = value.units() / divisor;
  const auto remainder = value.units() % divisor;
  if (basis_points != 0U &&
      whole > std::numeric_limits<std::int64_t>::max() / static_cast<std::int64_t>(basis_points)) {
    return Result<Money, ArithmeticError>::failure(ArithmeticError::Overflow);
  }
  const auto whole_product = whole * static_cast<std::int64_t>(basis_points);
  const auto remainder_product = remainder * static_cast<std::int64_t>(basis_points) + divisor / 2;
  const auto rounded_remainder = remainder_product / divisor;
  if (whole_product > std::numeric_limits<std::int64_t>::max() - rounded_remainder) {
    return Result<Money, ArithmeticError>::failure(ArithmeticError::Overflow);
  }
  return Money::from_units(whole_product + rounded_remainder);
}

std::string format_money(const Money value) {
  std::ostringstream output;
  output << value.units() / Money::units_per_ante << '.' << std::setw(4) << std::setfill('0')
         << value.units() % Money::units_per_ante;
  return output.str();
}

} // namespace gtosd
