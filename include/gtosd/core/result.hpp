#pragma once

#include <utility>
#include <variant>

namespace gtosd {

template <typename T, typename E> class [[nodiscard]] Result {
public:
  static Result success(T value) { return Result(std::move(value)); }
  static Result failure(E error) { return Result(error); }

  [[nodiscard]] bool has_value() const noexcept { return std::holds_alternative<T>(data_); }
  [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }
  [[nodiscard]] const T &value() const { return std::get<T>(data_); }
  [[nodiscard]] T &value() { return std::get<T>(data_); }
  [[nodiscard]] E error() const { return std::get<E>(data_); }

private:
  explicit Result(T value) : data_(std::move(value)) {}
  explicit Result(E error) : data_(error) {}
  std::variant<T, E> data_;
};

} // namespace gtosd
