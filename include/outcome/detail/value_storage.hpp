/* Essentially an internal optional implementation :)
(C) 2017-2019 Niall Douglas <http://www.nedproductions.biz/> (24 commits)
File Created: June 2017


Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License in the accompanying file
Licence.txt or at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.


Distributed under the Boost Software License, Version 1.0.
    (See accompanying file Licence.txt or copy at
          http://www.boost.org/LICENSE_1_0.txt)
*/

#ifndef OUTCOME_VALUE_STORAGE_HPP
#define OUTCOME_VALUE_STORAGE_HPP

#include "../config.hpp"

#include <cassert>

OUTCOME_V2_NAMESPACE_BEGIN

namespace detail
{
  template <class T, bool nothrow> struct strong_swap_impl
  {
    constexpr strong_swap_impl(bool &allgood, T &a, T &b)
    {
      allgood = true;
      using std::swap;
      swap(a, b);
    }
  };
#ifdef __cpp_exceptions
  template <class T> struct strong_swap_impl<T, false>
  {
    strong_swap_impl(bool &allgood, T &a, T &b)
    {
      allgood = true;
      T v(static_cast<T &&>(a));
      try
      {
        a = static_cast<T &&>(b);
      }
      catch(...)
      {
        // Try to put back a
        try
        {
          a = static_cast<T &&>(v);
          // fall through as all good
        }
        catch(...)
        {
          // failed to completely restore
          allgood = false;
          // throw away second exception
        }
        throw;  // rethrow original exception
      }
      // b has been moved to a, try to move v to b
      try
      {
        b = static_cast<T &&>(v);
      }
      catch(...)
      {
        // Try to restore a to b, and v to a
        try
        {
          b = static_cast<T &&>(a);
          a = static_cast<T &&>(v);
          // fall through as all good
        }
        catch(...)
        {
          // failed to completely restore
          allgood = false;
          // throw away second exception
        }
        throw;  // rethrow original exception
      }
    }
  };
#endif
}  // namespace detail

/*!
 */
OUTCOME_TEMPLATE(class T)
OUTCOME_TREQUIRES(OUTCOME_TPRED(std::is_move_constructible<T>::value &&std::is_move_assignable<T>::value))
constexpr inline void strong_swap(bool &allgood, T &a, T &b) noexcept(detail::is_nothrow_swappable<T>::value)
{
  detail::strong_swap_impl<T, detail::is_nothrow_swappable<T>::value>(allgood, a, b);
}

namespace detail
{
  template <class T>
  constexpr
#ifdef _MSC_VER
  __declspec(noreturn)
#elif defined(__GNUC__) || defined(__clang__)
        __attribute__((noreturn))
#endif
  void make_ub(T && /*unused*/)
  {
    assert(false);  // NOLINT
#if defined(__GNUC__) || defined(__clang__)
    __builtin_unreachable();
#elif defined(_MSC_VER)
    __assume(0);
#endif
  }

  /* Outcome v1 used a C bitfield whose values were tracked by compiler optimisers nicely,
  but that produces ICEs when used in constexpr.

  Outcome v2.0-v2.1 used a 32 bit integer and manually set and cleared bits. Unfortunately
  only GCC's optimiser tracks bit values during constant folding, and only per byte, and
  even then unreliably. https://wg21.link/P1886 "Error speed benchmarking" showed just how
  poorly clang and MSVC fails to optimise outcome-using code, if you manually set bits.

  Outcome v2.2 therefore uses an enum with fixed values, and constexpr manipulation functions
  to change the value to one of the enum's values. This is stupid to look at in source code,
  but it make clang's optimiser do the right thing, so it's worth it.
  */
#define OUTCOME_USE_CONSTEXPR_ENUM_STATUS 0
  enum class status : uint16_t
  {
    // WARNING: These bits are not tracked by abi-dumper, but changing them will break ABI!
    none = 0,

    have_value = (1U << 0U),
    have_error = (1U << 1U),
    have_exception = (2U << 1U),
    have_error_exception = (3U << 1U),

    // failed to complete a strong swap
    have_lost_consistency = (1U << 3U),
    have_value_lost_consistency = (1U << 0U) | (1U << 3U),
    have_error_lost_consistency = (1U << 1U) | (1U << 3U),
    have_exception_lost_consistency = (2U << 1U) | (1U << 3U),
    have_error_exception_lost_consistency = (3U << 1U) | (1U << 3U),

    // can errno be set from this error?
    have_error_is_errno = (1U << 4U),
    have_error_error_is_errno = (1U << 1U) | (1U << 4U),
    have_error_exception_error_is_errno = (3U << 1U) | (1U << 4U),

    have_error_lost_consistency_error_is_errno = (1U << 1U) | (1U << 3U) | (1U << 4U),
    have_error_exception_lost_consistency_error_is_errno = (3U << 1U) | (1U << 3U) | (1U << 4U),

    // value has been moved from
    have_moved_from = (1U << 5U)
  };
  struct status_bitfield_type
  {
    status status_value{status::none};
    uint16_t spare_storage_value{0};  // hooks::spare_storage()

    constexpr status_bitfield_type() = default;
    constexpr status_bitfield_type(status v) noexcept
        : status_value(v)
    {
    }  // NOLINT
    constexpr status_bitfield_type(status v, uint16_t s) noexcept
        : status_value(v)
        , spare_storage_value(s)
    {
    }
    constexpr status_bitfield_type(const status_bitfield_type &) = default;
    constexpr status_bitfield_type(status_bitfield_type &&) = default;
    constexpr status_bitfield_type &operator=(const status_bitfield_type &) = default;
    constexpr status_bitfield_type &operator=(status_bitfield_type &&) = default;
    //~status_bitfield_type() = default;  // Do NOT uncomment this, it breaks older clangs!

    constexpr bool have_value() const noexcept
    {
#if OUTCOME_USE_CONSTEXPR_ENUM_STATUS
      return (status_value == status::have_value)                      //
             || (status_value == status::have_value_lost_consistency)  //
      ;
#else
      return (static_cast<uint16_t>(status_value) & static_cast<uint16_t>(status::have_value)) != 0;
#endif
    }
    constexpr bool have_error() const noexcept
    {
#if OUTCOME_USE_CONSTEXPR_ENUM_STATUS
      return (status_value == status::have_error)                                               //
             || (status_value == status::have_error_exception)                                  //
             || (status_value == status::have_error_lost_consistency)                           //
             || (status_value == status::have_error_exception_lost_consistency)                 //
             || (status_value == status::have_error_error_is_errno)                             //
             || (status_value == status::have_error_exception_error_is_errno)                   //
             || (status_value == status::have_error_lost_consistency_error_is_errno)            //
             || (status_value == status::have_error_exception_lost_consistency_error_is_errno)  //
      ;
#else
      return (static_cast<uint16_t>(status_value) & static_cast<uint16_t>(status::have_error)) != 0;
#endif
    }
    constexpr bool have_exception() const noexcept
    {
#if OUTCOME_USE_CONSTEXPR_ENUM_STATUS
      return (status_value == status::have_exception)                                           //
             || (status_value == status::have_error_exception)                                  //
             || (status_value == status::have_exception_lost_consistency)                       //
             || (status_value == status::have_error_exception_lost_consistency)                 //
             || (status_value == status::have_error_exception_error_is_errno)                   //
             || (status_value == status::have_error_exception_lost_consistency_error_is_errno)  //
      ;
#else
      return (static_cast<uint16_t>(status_value) & static_cast<uint16_t>(status::have_exception)) != 0;
#endif
    }
    constexpr bool have_lost_consistency() const noexcept
    {
#if OUTCOME_USE_CONSTEXPR_ENUM_STATUS
      return (status_value == status::have_value_lost_consistency)                              //
             || (status_value == status::have_error_lost_consistency)                           //
             || (status_value == status::have_exception_lost_consistency)                       //
             || (status_value == status::have_error_lost_consistency_error_is_errno)            //
             || (status_value == status::have_error_exception_lost_consistency_error_is_errno)  //
      ;
#else
      return (static_cast<uint16_t>(status_value) & static_cast<uint16_t>(status::have_lost_consistency)) != 0;
#endif
    }
    constexpr bool have_error_is_errno() const noexcept
    {
#if OUTCOME_USE_CONSTEXPR_ENUM_STATUS
      return (status_value == status::have_error_error_is_errno)                                //
             || (status_value == status::have_error_exception_error_is_errno)                   //
             || (status_value == status::have_error_lost_consistency_error_is_errno)            //
             || (status_value == status::have_error_exception_lost_consistency_error_is_errno)  //
      ;
#else
      return (static_cast<uint16_t>(status_value) & static_cast<uint16_t>(status::have_error_is_errno)) != 0;
#endif
    }
    constexpr bool have_moved_from() const noexcept
    {
#if OUTCOME_USE_CONSTEXPR_ENUM_STATUS
#error Fixme
#else
      return (static_cast<uint16_t>(status_value) & static_cast<uint16_t>(status::have_moved_from)) != 0;
#endif
    }

    constexpr status_bitfield_type &set_have_value(bool v) noexcept
    {
#if OUTCOME_USE_CONSTEXPR_ENUM_STATUS
      switch(status_value)
      {
      case status::none:
        if(v)
        {
          status_value = status::have_value;
        }
        break;
      case status::have_value:
        if(!v)
        {
          status_value = status::none;
        }
        break;
      case status::have_error:
        if(v)
        {
          make_ub(*this);
        }
        break;
      case status::have_exception:
        if(v)
        {
          make_ub(*this);
        }
        break;
      case status::have_error_exception:
        if(v)
        {
          make_ub(*this);
        }
        break;
      case status::have_value_lost_consistency:
        if(!v)
        {
          status_value = status::none;
        }
        break;
      case status::have_error_lost_consistency:
        if(v)
        {
          make_ub(*this);
        }
        break;
      case status::have_exception_lost_consistency:
        if(v)
        {
          make_ub(*this);
        }
        break;
      case status::have_error_exception_lost_consistency:
        if(v)
        {
          make_ub(*this);
        }
        break;
      case status::have_error_error_is_errno:
        if(v)
        {
          make_ub(*this);
        }
        break;
      case status::have_error_exception_error_is_errno:
        if(v)
        {
          make_ub(*this);
        }
        break;
      case status::have_error_lost_consistency_error_is_errno:
        if(v)
        {
          make_ub(*this);
        }
        break;
      case status::have_error_exception_lost_consistency_error_is_errno:
        if(v)
        {
          make_ub(*this);
        }
        break;
      }
#else
      status_value = static_cast<status>(v ? (static_cast<uint16_t>(status_value) | static_cast<uint16_t>(status::have_value)) :
                                             (static_cast<uint16_t>(status_value) & ~static_cast<uint16_t>(status::have_value)));
#endif
      return *this;
    }
    constexpr status_bitfield_type &set_have_error(bool v) noexcept
    {
#if OUTCOME_USE_CONSTEXPR_ENUM_STATUS
      switch(status_value)
      {
      case status::none:
        if(v)
        {
          status_value = status::have_error;
        }
        break;
      case status::have_value:
        if(v)
        {
          make_ub(*this);
        }
        break;
      case status::have_error:
        if(!v)
        {
          status_value = status::none;
        }
        break;
      case status::have_exception:
        if(v)
        {
          status_value = status::have_error_exception;
        }
        break;
      case status::have_error_exception:
        if(!v)
        {
          status_value = status::have_exception;
        }
        break;
      case status::have_value_lost_consistency:
        if(v)
        {
          make_ub(*this);
        }
        break;
      case status::have_error_lost_consistency:
        if(!v)
        {
          status_value = status::none;
        }
        break;
      case status::have_exception_lost_consistency:
        if(v)
        {
          status_value = status::have_error_exception_lost_consistency;
        }
        break;
      case status::have_error_exception_lost_consistency:
        if(!v)
        {
          status_value = status::have_exception_lost_consistency;
        }
        break;
      case status::have_error_error_is_errno:
        if(!v)
        {
          status_value = status::none;
        }
        break;
      case status::have_error_exception_error_is_errno:
        if(!v)
        {
          status_value = status::have_exception;
        }
        break;
      case status::have_error_lost_consistency_error_is_errno:
        if(!v)
        {
          status_value = status::none;
        }
        break;
      case status::have_error_exception_lost_consistency_error_is_errno:
        if(!v)
        {
          status_value = status::have_exception_lost_consistency;
        }
        break;
      }
#else
      status_value = static_cast<status>(v ? (static_cast<uint16_t>(status_value) | static_cast<uint16_t>(status::have_error)) :
                                             (static_cast<uint16_t>(status_value) & ~static_cast<uint16_t>(status::have_error)));
#endif
      return *this;
    }
    constexpr status_bitfield_type &set_have_exception(bool v) noexcept
    {
#if OUTCOME_USE_CONSTEXPR_ENUM_STATUS
      switch(status_value)
      {
      case status::none:
        if(v)
        {
          status_value = status::have_exception;
        }
        break;
      case status::have_value:
        if(v)
        {
          make_ub(*this);
        }
        break;
      case status::have_error:
        if(v)
        {
          status_value = status::have_error_exception;
        }
        break;
      case status::have_exception:
        if(!v)
        {
          status_value = status::none;
        }
        break;
      case status::have_error_exception:
        if(!v)
        {
          status_value = status::have_error;
        }
        break;
      case status::have_value_lost_consistency:
        if(v)
        {
          make_ub(*this);
        }
        break;
      case status::have_error_lost_consistency:
        if(v)
        {
          status_value = status::have_error_exception_lost_consistency;
        }
        break;
      case status::have_exception_lost_consistency:
        if(!v)
        {
          status_value = status::none;
        }
        break;
      case status::have_error_exception_lost_consistency:
        if(!v)
        {
          status_value = status::have_error_lost_consistency;
        }
        break;
      case status::have_error_error_is_errno:
        if(v)
        {
          status_value = status::have_error_exception_error_is_errno;
        }
        break;
      case status::have_error_exception_error_is_errno:
        if(!v)
        {
          status_value = status::have_error_error_is_errno;
        }
        break;
      case status::have_error_lost_consistency_error_is_errno:
        if(v)
        {
          status_value = status::have_error_exception_lost_consistency_error_is_errno;
        }
        break;
      case status::have_error_exception_lost_consistency_error_is_errno:
        if(!v)
        {
          status_value = status::have_error_lost_consistency_error_is_errno;
        }
        break;
      }
#else
      status_value = static_cast<status>(v ? (static_cast<uint16_t>(status_value) | static_cast<uint16_t>(status::have_exception)) :
                                             (static_cast<uint16_t>(status_value) & ~static_cast<uint16_t>(status::have_exception)));
#endif
      return *this;
    }
    constexpr status_bitfield_type &set_have_error_is_errno(bool v) noexcept
    {
#if OUTCOME_USE_CONSTEXPR_ENUM_STATUS
      switch(status_value)
      {
      case status::none:
        make_ub(*this);
        break;
      case status::have_value:
        make_ub(*this);
        break;
      case status::have_error:
        if(v)
        {
          status_value = status::have_error_error_is_errno;
        }
        break;
      case status::have_exception:
        make_ub(*this);
        break;
      case status::have_error_exception:
        if(v)
        {
          status_value = status::have_error_exception_error_is_errno;
        }
        break;
      case status::have_value_lost_consistency:
        make_ub(*this);
        break;
      case status::have_error_lost_consistency:
        if(v)
        {
          status_value = status::have_error_lost_consistency_error_is_errno;
        }
        break;
      case status::have_exception_lost_consistency:
        make_ub(*this);
        break;
      case status::have_error_exception_lost_consistency:
        if(v)
        {
          status_value = status::have_error_exception_lost_consistency_error_is_errno;
        }
        break;
      case status::have_error_error_is_errno:
        if(!v)
        {
          status_value = status::have_error;
        }
        break;
      case status::have_error_exception_error_is_errno:
        if(!v)
        {
          status_value = status::have_error_exception;
        }
        break;
      case status::have_error_lost_consistency_error_is_errno:
        if(!v)
        {
          status_value = status::have_error_lost_consistency;
        }
        break;
      case status::have_error_exception_lost_consistency_error_is_errno:
        if(!v)
        {
          status_value = status::have_error_exception_lost_consistency;
        }
        break;
      }
#else
      status_value = static_cast<status>(v ? (static_cast<uint16_t>(status_value) | static_cast<uint16_t>(status::have_error_is_errno)) :
                                             (static_cast<uint16_t>(status_value) & ~static_cast<uint16_t>(status::have_error_is_errno)));
#endif
      return *this;
    }
    constexpr status_bitfield_type &set_have_lost_consistency(bool v) noexcept
    {
#if OUTCOME_USE_CONSTEXPR_ENUM_STATUS
      switch(status_value)
      {
      case status::none:
        if(v)
        {
          make_ub(*this);
        }
        break;
      case status::have_value:
        if(v)
        {
          status_value = status::have_value_lost_consistency;
        }
        break;
      case status::have_error:
        if(v)
        {
          status_value = status::have_error_lost_consistency;
        }
        break;
      case status::have_exception:
        if(v)
        {
          status_value = status::have_exception_lost_consistency;
        }
        break;
      case status::have_error_exception:
        if(v)
        {
          status_value = status::have_error_exception_lost_consistency;
        }
        break;
      case status::have_value_lost_consistency:
        if(!v)
        {
          status_value = status::have_value;
        }
        break;
      case status::have_error_lost_consistency:
        if(!v)
        {
          status_value = status::have_error;
        }
        break;
      case status::have_exception_lost_consistency:
        if(!v)
        {
          status_value = status::have_exception;
        }
        break;
      case status::have_error_exception_lost_consistency:
        if(!v)
        {
          status_value = status::have_error_exception;
        }
        break;
      case status::have_error_error_is_errno:
        if(v)
        {
          status_value = status::have_error_lost_consistency_error_is_errno;
        }
        break;
      case status::have_error_exception_error_is_errno:
        if(v)
        {
          status_value = status::have_error_exception_lost_consistency_error_is_errno;
        }
        break;
      case status::have_error_lost_consistency_error_is_errno:
        if(!v)
        {
          status_value = status::have_error_exception_error_is_errno;
        }
        break;
      case status::have_error_exception_lost_consistency_error_is_errno:
        if(!v)
        {
          status_value = status::have_error_exception_error_is_errno;
        }
        break;
      }
#else
      status_value = static_cast<status>(v ? (static_cast<uint16_t>(status_value) | static_cast<uint16_t>(status::have_lost_consistency)) :
                                             (static_cast<uint16_t>(status_value) & ~static_cast<uint16_t>(status::have_lost_consistency)));
#endif
      return *this;
    }
    constexpr status_bitfield_type &set_have_moved_from(bool v) noexcept
    {
#if OUTCOME_USE_CONSTEXPR_ENUM_STATUS
#error Fixme
#else
      status_value = static_cast<status>(v ? (static_cast<uint16_t>(status_value) | static_cast<uint16_t>(status::have_moved_from)) :
                                             (static_cast<uint16_t>(status_value) & ~static_cast<uint16_t>(status::have_moved_from)));
#endif
      return *this;
    }
  };
#if !defined(NDEBUG)
  // Check is trivial in all ways except default constructibility
  static_assert(sizeof(status_bitfield_type) == 4, "status_bitfield_type is not sized 4 bytes!");
  static_assert(std::is_trivially_copyable<status_bitfield_type>::value, "status_bitfield_type is not trivially copyable!");
  static_assert(std::is_trivially_assignable<status_bitfield_type, status_bitfield_type>::value, "status_bitfield_type is not trivially assignable!");
  static_assert(std::is_trivially_destructible<status_bitfield_type>::value, "status_bitfield_type is not trivially destructible!");
  static_assert(std::is_trivially_copy_constructible<status_bitfield_type>::value, "status_bitfield_type is not trivially copy constructible!");
  static_assert(std::is_trivially_move_constructible<status_bitfield_type>::value, "status_bitfield_type is not trivially move constructible!");
  static_assert(std::is_trivially_copy_assignable<status_bitfield_type>::value, "status_bitfield_type is not trivially copy assignable!");
  static_assert(std::is_trivially_move_assignable<status_bitfield_type>::value, "status_bitfield_type is not trivially move assignable!");
  // Also check is standard layout
  static_assert(std::is_standard_layout<status_bitfield_type>::value, "status_bitfield_type is not a standard layout type!");
#endif

  // Used if T is trivial
  template <class T> struct value_storage_trivial
  {
    using value_type = T;
    union {
      empty_type _empty;
      devoid<T> _value;
    };
    status_bitfield_type _status;
    constexpr value_storage_trivial() noexcept
        : _empty{}
    {
    }
    // Special from-void catchall constructor, always constructs default T irrespective of whether void is valued or not (can do no better if T cannot be
    // copied)
    struct disable_void_catchall
    {
    };
    using void_value_storage_trivial = std::conditional_t<std::is_void<T>::value, disable_void_catchall, value_storage_trivial<void>>;
    explicit constexpr value_storage_trivial(const void_value_storage_trivial &o) noexcept(std::is_nothrow_default_constructible<value_type>::value)
        : _value()
        , _status(o._status)
    {
    }
    value_storage_trivial(const value_storage_trivial &) = default;             // NOLINT
    value_storage_trivial(value_storage_trivial &&) = default;                  // NOLINT
    value_storage_trivial &operator=(const value_storage_trivial &) = default;  // NOLINT
    value_storage_trivial &operator=(value_storage_trivial &&) = default;       // NOLINT
    ~value_storage_trivial() = default;
    constexpr explicit value_storage_trivial(status_bitfield_type status)
        : _empty()
        , _status(status)
    {
    }
    template <class... Args>
    constexpr explicit value_storage_trivial(in_place_type_t<value_type> /*unused*/,
                                             Args &&... args) noexcept(detail::is_nothrow_constructible<value_type, Args...>)
        : _value(static_cast<Args &&>(args)...)
        , _status(status::have_value)
    {
    }
    template <class U, class... Args>
    constexpr value_storage_trivial(in_place_type_t<value_type> /*unused*/, std::initializer_list<U> il,
                                    Args &&... args) noexcept(detail::is_nothrow_constructible<value_type, std::initializer_list<U>, Args...>)
        : _value(il, static_cast<Args &&>(args)...)
        , _status(status::have_value)
    {
    }
    template <class U>
    static constexpr bool enable_converting_constructor = !std::is_same<std::decay_t<U>, value_type>::value && std::is_constructible<value_type, U>::value;
    OUTCOME_TEMPLATE(class U)
    OUTCOME_TREQUIRES(OUTCOME_TPRED(enable_converting_constructor<U>))
    constexpr explicit value_storage_trivial(const value_storage_trivial<U> &o) noexcept(detail::is_nothrow_constructible<value_type, U>)
        : value_storage_trivial(o._status.have_value() ? value_storage_trivial(in_place_type<value_type>, o._value) : value_storage_trivial())  // NOLINT
    {
      _status = o._status;
    }
    OUTCOME_TEMPLATE(class U)
    OUTCOME_TREQUIRES(OUTCOME_TPRED(enable_converting_constructor<U>))
    constexpr explicit value_storage_trivial(value_storage_trivial<U> &&o) noexcept(detail::is_nothrow_constructible<value_type, U>)
        : value_storage_trivial(o._status.have_value() ? value_storage_trivial(in_place_type<value_type>, static_cast<U &&>(o._value)) :
                                                         value_storage_trivial())  // NOLINT
    {
      _status = o._status;
    }
    constexpr void swap(value_storage_trivial &o) noexcept
    {
      // storage is trivial, so just use assignment
      auto temp = static_cast<value_storage_trivial &&>(*this);
      *this = static_cast<value_storage_trivial &&>(o);
      o = static_cast<value_storage_trivial &&>(temp);
    }
  };
  // Used if T is non-trivial
  template <class T> struct value_storage_nontrivial
  {
    using value_type = T;
    union {
      empty_type _empty;
      value_type _value;
    };
    status_bitfield_type _status;
    value_storage_nontrivial() noexcept
        : _empty{}
    {
    }
    value_storage_nontrivial &operator=(const value_storage_nontrivial &) = default;  // if reaches here, copy assignment is trivial
    value_storage_nontrivial &operator=(value_storage_nontrivial &&) = default;       // NOLINT if reaches here, move assignment is trivial
    value_storage_nontrivial(value_storage_nontrivial &&o) noexcept(std::is_nothrow_move_constructible<value_type>::value)  // NOLINT
        : _status(o._status)
    {
      if(this->_status.have_value())
      {
        this->_status.set_have_value(false);
        new(&_value) value_type(static_cast<value_type &&>(o._value));  // NOLINT
        _status = o._status;
      }
    }
    value_storage_nontrivial(const value_storage_nontrivial &o) noexcept(std::is_nothrow_copy_constructible<value_type>::value)
        : _status(o._status)
    {
      if(this->_status.have_value())
      {
        this->_status.set_have_value(false);
        new(&_value) value_type(o._value);  // NOLINT
        _status = o._status;
      }
    }
    // Special from-void constructor, constructs default T if void valued
    explicit value_storage_nontrivial(const value_storage_trivial<void> &o) noexcept(std::is_nothrow_default_constructible<value_type>::value)
        : _status(o._status)
    {
      if(this->_status.have_value())
      {
        this->_status.set_have_value(false);
        new(&_value) value_type;  // NOLINT
        _status = o._status;
      }
    }
    explicit value_storage_nontrivial(status_bitfield_type status)
        : _empty()
        , _status(status)
    {
    }
    template <class... Args>
    explicit value_storage_nontrivial(in_place_type_t<value_type> /*unused*/,
                                      Args &&... args) noexcept(detail::is_nothrow_constructible<value_type, Args...>)
        : _value(static_cast<Args &&>(args)...)  // NOLINT
        , _status(status::have_value)
    {
    }
    template <class U, class... Args>
    value_storage_nontrivial(in_place_type_t<value_type> /*unused*/, std::initializer_list<U> il,
                             Args &&... args) noexcept(detail::is_nothrow_constructible<value_type, std::initializer_list<U>, Args...>)
        : _value(il, static_cast<Args &&>(args)...)
        , _status(status::have_value)
    {
    }
    template <class U>
    static constexpr bool enable_converting_constructor = !std::is_same<std::decay_t<U>, value_type>::value && std::is_constructible<value_type, U>::value;
    OUTCOME_TEMPLATE(class U)
    OUTCOME_TREQUIRES(OUTCOME_TPRED(enable_converting_constructor<U>))
    constexpr explicit value_storage_nontrivial(const value_storage_nontrivial<U> &o) noexcept(detail::is_nothrow_constructible<value_type, U>)
        : value_storage_nontrivial(o._status.have_value() ? value_storage_nontrivial(in_place_type<value_type>, o._value) : value_storage_nontrivial())
    {
      _status = o._status;
    }
    OUTCOME_TEMPLATE(class U)
    OUTCOME_TREQUIRES(OUTCOME_TPRED(enable_converting_constructor<U>))
    constexpr explicit value_storage_nontrivial(const value_storage_trivial<U> &o) noexcept(detail::is_nothrow_constructible<value_type, U>)
        : value_storage_nontrivial(o._status.have_value() ? value_storage_nontrivial(in_place_type<value_type>, o._value) : value_storage_nontrivial())
    {
      _status = o._status;
    }
    OUTCOME_TEMPLATE(class U)
    OUTCOME_TREQUIRES(OUTCOME_TPRED(enable_converting_constructor<U>))
    constexpr explicit value_storage_nontrivial(value_storage_nontrivial<U> &&o) noexcept(detail::is_nothrow_constructible<value_type, U>)
        : value_storage_nontrivial(o._status.have_value() ? value_storage_nontrivial(in_place_type<value_type>, static_cast<U &&>(o._value)) :
                                                            value_storage_nontrivial())
    {
      _status = o._status;
    }
    OUTCOME_TEMPLATE(class U)
    OUTCOME_TREQUIRES(OUTCOME_TPRED(enable_converting_constructor<U>))
    constexpr explicit value_storage_nontrivial(value_storage_trivial<U> &&o) noexcept(detail::is_nothrow_constructible<value_type, U>)
        : value_storage_nontrivial(o._status.have_value() ? value_storage_nontrivial(in_place_type<value_type>, static_cast<U &&>(o._value)) :
                                                            value_storage_nontrivial())
    {
      _status = o._status;
    }
    ~value_storage_nontrivial() noexcept(std::is_nothrow_destructible<T>::value)
    {
      if(this->_status.have_value())
      {
        this->_value.~value_type();  // NOLINT
        this->_status.set_have_value(false);
      }
    }
    constexpr void swap(value_storage_nontrivial &o) noexcept(detail::is_nothrow_swappable<value_type>::value)
    {
      using std::swap;
      if(!_status.have_value() && !o._status.have_value())
      {
        swap(_status, o._status);
        return;
      }
      if(_status.have_value() && o._status.have_value())
      {
        struct _
        {
          status_bitfield_type &a, &b;
          bool all_good{false};
          ~_()
          {
            if(!all_good)
            {
              // We lost one of the values
              a.set_have_lost_consistency(true);
              b.set_have_lost_consistency(true);
            }
          }
        } _{_status, o._status};
        strong_swap(_.all_good, _value, o._value);
        swap(_status, o._status);
        return;
      }
      // One must be empty and the other non-empty, so use move construction
      if(_status.have_value())
      {
        // Move construct me into other
        new(&o._value) value_type(static_cast<value_type &&>(_value));  // NOLINT
        this->_value.~value_type();                                     // NOLINT
        swap(_status, o._status);
      }
      else
      {
        // Move construct other into me
        new(&_value) value_type(static_cast<value_type &&>(o._value));  // NOLINT
        o._value.~value_type();                                         // NOLINT
        swap(_status, o._status);
      }
    }
  };
  template <class Base> struct value_storage_delete_copy_constructor : Base  // NOLINT
  {
    using Base::Base;
    using value_type = typename Base::value_type;
    value_storage_delete_copy_constructor() = default;
    value_storage_delete_copy_constructor(const value_storage_delete_copy_constructor &) = delete;
    value_storage_delete_copy_constructor(value_storage_delete_copy_constructor &&) = default;  // NOLINT
  };
  template <class Base> struct value_storage_delete_copy_assignment : Base  // NOLINT
  {
    using Base::Base;
    using value_type = typename Base::value_type;
    value_storage_delete_copy_assignment() = default;
    value_storage_delete_copy_assignment(const value_storage_delete_copy_assignment &) = default;
    value_storage_delete_copy_assignment(value_storage_delete_copy_assignment &&) = default;  // NOLINT
    value_storage_delete_copy_assignment &operator=(const value_storage_delete_copy_assignment &o) = delete;
    value_storage_delete_copy_assignment &operator=(value_storage_delete_copy_assignment &&o) = default;  // NOLINT
  };
  template <class Base> struct value_storage_delete_move_assignment : Base  // NOLINT
  {
    using Base::Base;
    using value_type = typename Base::value_type;
    value_storage_delete_move_assignment() = default;
    value_storage_delete_move_assignment(const value_storage_delete_move_assignment &) = default;
    value_storage_delete_move_assignment(value_storage_delete_move_assignment &&) = default;  // NOLINT
    value_storage_delete_move_assignment &operator=(const value_storage_delete_move_assignment &o) = default;
    value_storage_delete_move_assignment &operator=(value_storage_delete_move_assignment &&o) = delete;
  };
  template <class Base> struct value_storage_delete_move_constructor : Base  // NOLINT
  {
    using Base::Base;
    using value_type = typename Base::value_type;
    value_storage_delete_move_constructor() = default;
    value_storage_delete_move_constructor(const value_storage_delete_move_constructor &) = default;
    value_storage_delete_move_constructor(value_storage_delete_move_constructor &&) = delete;
  };
  template <class Base> struct value_storage_nontrivial_move_assignment : Base  // NOLINT
  {
    using Base::Base;
    using value_type = typename Base::value_type;
    value_storage_nontrivial_move_assignment() = default;
    value_storage_nontrivial_move_assignment(const value_storage_nontrivial_move_assignment &) = default;
    value_storage_nontrivial_move_assignment(value_storage_nontrivial_move_assignment &&) = default;  // NOLINT
    value_storage_nontrivial_move_assignment &operator=(const value_storage_nontrivial_move_assignment &o) = default;
    value_storage_nontrivial_move_assignment &
    operator=(value_storage_nontrivial_move_assignment &&o) noexcept(std::is_nothrow_move_assignable<value_type>::value)  // NOLINT
    {
      if(this->_status.have_value() && o._status.have_value())
      {
        this->_value = static_cast<value_type &&>(o._value);  // NOLINT
      }
      else if(this->_status.have_value() && !o._status.have_value())
      {
        this->_value.~value_type();  // NOLINT
      }
      else if(!this->_status.have_value() && o._status.have_value())
      {
        new(&this->_value) value_type(static_cast<value_type &&>(o._value));  // NOLINT
      }
      this->_status = o._status;
      return *this;
    }
  };
  template <class Base> struct value_storage_nontrivial_copy_assignment : Base  // NOLINT
  {
    using Base::Base;
    using value_type = typename Base::value_type;
    value_storage_nontrivial_copy_assignment() = default;
    value_storage_nontrivial_copy_assignment(const value_storage_nontrivial_copy_assignment &) = default;
    value_storage_nontrivial_copy_assignment(value_storage_nontrivial_copy_assignment &&) = default;              // NOLINT
    value_storage_nontrivial_copy_assignment &operator=(value_storage_nontrivial_copy_assignment &&o) = default;  // NOLINT
    value_storage_nontrivial_copy_assignment &
    operator=(const value_storage_nontrivial_copy_assignment &o) noexcept(std::is_nothrow_copy_assignable<value_type>::value)
    {
      if(this->_status.have_value() && o._status.have_value())
      {
        this->_value = o._value;  // NOLINT
      }
      else if(this->_status.have_value() && !o._status.have_value())
      {
        this->_value.~value_type();  // NOLINT
      }
      else if(!this->_status.have_value() && o._status.have_value())
      {
        new(&this->_value) value_type(o._value);  // NOLINT
      }
      this->_status = o._status;
      return *this;
    }
  };

  // We don't actually need all of std::is_trivial<>, std::is_trivially_copyable<> is sufficient
  template <class T>
  using value_storage_select_trivality =
  std::conditional_t<std::is_trivially_copyable<devoid<T>>::value, value_storage_trivial<T>, value_storage_nontrivial<T>>;
  template <class T>
  using value_storage_select_move_constructor = std::conditional_t<std::is_move_constructible<devoid<T>>::value, value_storage_select_trivality<T>,
                                                                   value_storage_delete_move_constructor<value_storage_select_trivality<T>>>;
  template <class T>
  using value_storage_select_copy_constructor = std::conditional_t<std::is_copy_constructible<devoid<T>>::value, value_storage_select_move_constructor<T>,
                                                                   value_storage_delete_copy_constructor<value_storage_select_move_constructor<T>>>;
  template <class T>
  using value_storage_select_move_assignment = std::conditional_t<
  std::is_trivially_move_assignable<devoid<T>>::value, value_storage_select_copy_constructor<T>,
  std::conditional_t<std::is_move_assignable<devoid<T>>::value, value_storage_nontrivial_move_assignment<value_storage_select_copy_constructor<T>>,
                     value_storage_delete_copy_assignment<value_storage_select_copy_constructor<T>>>>;
  template <class T>
  using value_storage_select_copy_assignment = std::conditional_t<
  std::is_trivially_copy_assignable<devoid<T>>::value, value_storage_select_move_assignment<T>,
  std::conditional_t<std::is_copy_assignable<devoid<T>>::value, value_storage_nontrivial_copy_assignment<value_storage_select_move_assignment<T>>,
                     value_storage_delete_copy_assignment<value_storage_select_move_assignment<T>>>>;
  template <class T> using value_storage_select_impl = value_storage_select_copy_assignment<T>;
#ifndef NDEBUG
  // Check is trivial in all ways except default constructibility
  // static_assert(std::is_trivial<value_storage_select_impl<int>>::value, "value_storage_select_impl<int> is not trivial!");
  // static_assert(std::is_trivially_default_constructible<value_storage_select_impl<int>>::value, "value_storage_select_impl<int> is not trivially default
  // constructible!");
  static_assert(std::is_trivially_copyable<value_storage_select_impl<int>>::value, "value_storage_select_impl<int> is not trivially copyable!");
  static_assert(std::is_trivially_assignable<value_storage_select_impl<int>, value_storage_select_impl<int>>::value,
                "value_storage_select_impl<int> is not trivially assignable!");
  static_assert(std::is_trivially_destructible<value_storage_select_impl<int>>::value, "value_storage_select_impl<int> is not trivially destructible!");
  static_assert(std::is_trivially_copy_constructible<value_storage_select_impl<int>>::value,
                "value_storage_select_impl<int> is not trivially copy constructible!");
  static_assert(std::is_trivially_move_constructible<value_storage_select_impl<int>>::value,
                "value_storage_select_impl<int> is not trivially move constructible!");
  static_assert(std::is_trivially_copy_assignable<value_storage_select_impl<int>>::value, "value_storage_select_impl<int> is not trivially copy assignable!");
  static_assert(std::is_trivially_move_assignable<value_storage_select_impl<int>>::value, "value_storage_select_impl<int> is not trivially move assignable!");
  // Also check is standard layout
  static_assert(std::is_standard_layout<value_storage_select_impl<int>>::value, "value_storage_select_impl<int> is not a standard layout type!");
#endif
}  // namespace detail

OUTCOME_V2_NAMESPACE_END

#endif
