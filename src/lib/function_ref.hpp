/**
 * @file    function_ref.hpp
 * @brief   Non-owning reference to a callable, for callback parameters.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section function_ref_overview Overview
 *
 * Replaces the C pattern of a function pointer plus a void *ctx. Unlike
 * std::function it never allocates and never copies the callable: it only
 * stores its address. Use it for parameters only; it must not outlive the
 * callable it refers to.
 *
 * @code
 * bool for_each_chunk(crls::function_ref<bool(std::span<const unsigned char>)> sink);
 *
 * uint64_t total = 0;
 * for_each_chunk([&](std::span<const unsigned char> chunk){
 *   total += chunk.size();
 *   return true;
 * });
 * @endcode
 */

#ifndef LIB_FUNCTION_REF_HPP
#define LIB_FUNCTION_REF_HPP

#include <memory>
#include <type_traits>
#include <utility>

namespace crls{

template<class Signature> class function_ref;

/**
 * @brief Non-owning reference to a callable with signature R(Args...). A
 *        default-constructed function_ref is empty (false).
 */
template<class R, class... Args>
class function_ref<R(Args...)>{
public:
  function_ref() = default;

  /** @brief Refers to f, which must outlive this function_ref. */
  template<class F>
    requires (!std::is_same_v<std::remove_cvref_t<F>, function_ref> && std::is_invocable_r_v<R, F &, Args...>)
  function_ref(F &&f) noexcept
    : object_(const_cast<void *>(static_cast<const void *>(std::addressof(f)))),
      call_([](void *object, Args... args) -> R{
        return (*static_cast<std::remove_reference_t<F> *>(object))(std::forward<Args>(args)...);
      }) {}

  /** @brief Calls the callable. Must not be empty. */
  R operator()(Args... args) const{ return call_(object_, std::forward<Args>(args)...); }

  /** @brief True if it refers to a callable. */
  explicit operator bool() const noexcept{ return call_ != nullptr; }

private:
  void *object_ = nullptr;
  R (*call_)(void *, Args...) = nullptr;
};

}

#endif
