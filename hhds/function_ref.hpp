// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace hhds {

// A small, non-owning reference to a callable. The callable must outlive the
// function_ref. Rvalue construction is intentionally disabled so a view cannot
// accidentally retain a reference to a temporary lambda.
template <typename Signature>
class function_ref;

template <typename Result, typename... Args>
class function_ref<Result(Args...)> {
public:
  constexpr function_ref() noexcept = default;
  constexpr function_ref(std::nullptr_t) noexcept {}
  constexpr function_ref(const function_ref&) noexcept            = default;
  constexpr function_ref& operator=(const function_ref&) noexcept = default;

  template <typename Callable>
    requires(!std::same_as<std::remove_cvref_t<Callable>, function_ref> && std::is_invocable_r_v<Result, Callable&, Args...>)
  constexpr function_ref(Callable& callable) noexcept
      : object_(static_cast<void*>(std::addressof(callable))), invoke_([](void* object, Args... args) -> Result {
        return std::invoke(*static_cast<Callable*>(object), std::forward<Args>(args)...);
      }) {}

  template <typename Callable>
    requires(!std::same_as<std::remove_cvref_t<Callable>, function_ref>)
  function_ref(Callable&&) = delete;

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return invoke_ != nullptr; }

  constexpr Result operator()(Args... args) const {
    assert(invoke_ != nullptr && "calling an empty function_ref");
    return invoke_(object_, std::forward<Args>(args)...);
  }

private:
  void* object_                     = nullptr;
  Result (*invoke_)(void*, Args...) = nullptr;
};

}  // namespace hhds
