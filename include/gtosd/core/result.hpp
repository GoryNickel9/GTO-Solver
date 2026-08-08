#pragma once

#include <type_traits>
#include <utility>
#include <variant>

namespace gtosd {

template <typename T, typename E> class [[nodiscard]] Result {
public:
  // In-place construction: emplace directly into the variant storage so the
  // value is not copied twice (by-value parameter + variant move). For
  // large value types (e.g. the traversal's 5 KB combo vectors) this halves
  // the return-copy traffic on the hot path. The by-value overload exists
  // for braced-init-list arguments (which a forwarding reference cannot
  // deduce); overload resolution prefers the forwarding version for plain
  // lvalues/rvalues.
  template <typename... Args> static Result success(Args &&...args) {
    Result result;
    result.data_.template emplace<T>(std::forward<Args>(args)...);
    return result;
  }
  static Result success(T value) {
    Result result;
    result.data_.template emplace<T>(std::move(value));
    return result;
  }
  static Result failure(E error) { return Result(error); }

  [[nodiscard]] bool has_value() const noexcept { return std::holds_alternative<T>(data_); }
  [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }
  [[nodiscard]] const T &value() const { return std::get<T>(data_); }
  [[nodiscard]] T &value() { return std::get<T>(data_); }
  [[nodiscard]] E error() const { return std::get<E>(data_); }

private:
  Result() = default;
  explicit Result(E error) : data_(error) {}
  std::variant<T, E> data_;
};

} // namespace gtosd
