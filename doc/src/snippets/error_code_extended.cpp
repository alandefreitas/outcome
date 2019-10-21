/* Example of how to extend result's error code with extra information
(C) 2017-2019 Niall Douglas <http://www.nedproductions.biz/> (7 commits) and Andrzej Krzemieński <akrzemi1@gmail.com> (1 commit)


Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include "../../../include/boost/outcome.hpp"

#include <array>
#include <iostream>

#ifdef _WIN32
#error This example can only compile on POSIX
#else
#include <execinfo.h>
#endif

#ifdef _MSC_VER
#pragma warning(disable : 4702)  // unreachable code
#endif

namespace error_code_extended
{
  using BOOST_OUTCOME_V2_NAMESPACE::in_place_type;
  template <class T> using in_place_type_t = BOOST_OUTCOME_V2_NAMESPACE::in_place_type_t<T>;
}

//! [error_code_extended1]
/* Outcome's hook mechanism works vis ADL, so we will need a custom namespace
to ensure the hooks apply only to the types declared in this namespace only
*/
namespace error_code_extended
{
  // The extra error information we will keep
  struct extended_error_info
  {
    std::array<void *, 16> backtrace;  // The backtrace
    size_t items;                      // Items in backtrace array which are valid
  };
  struct mythreadlocaldata_t
  {
    // Keep 16 slots of extended error info as a ringbuffer
    extended_error_info slots[16];
    // The current oldest slot
    uint16_t current{0};

    // Return the oldest slot
    extended_error_info &next() { return slots[(current++) % 16]; }

    // Retrieve a previously stored slot, detecting if it is stale
    extended_error_info *get(uint16_t idx)
    {
      // If the idx is stale, return not found
      if(idx - current >= 16)
      {
        return nullptr;
      }
      return slots + (idx % 16);
    }
  };

  // Meyers' singleton returning a thread local data structure for this thread
  inline mythreadlocaldata_t &mythreadlocaldata()
  {
    static thread_local mythreadlocaldata_t v;
    return v;
  }
}
//! [error_code_extended1]

//! [error_code_extended2]
namespace error_code_extended
{
  // Use the error_code type as the ADL bridge for the hooks by creating a type here
  // It can be any type that your localised result uses, including the value type but
  // by localising the error code type here you prevent nasty surprises later when the
  // value type you use doesn't trigger the ADL bridge.
  struct error_code : public std::error_code
  {
    // literally passthrough
    using std::error_code::error_code;
    error_code() = default;
    error_code(std::error_code ec)
        : std::error_code(ec)
    {
    }
  };

  // Localise result and outcome to using the local error_code so this namespace gets looked up for the hooks
  template <class R> using result = BOOST_OUTCOME_V2_NAMESPACE::result<R, error_code>;
  template <class R> using outcome = BOOST_OUTCOME_V2_NAMESPACE::outcome<R, error_code /*, std::exception_ptr */>;
}
//! [error_code_extended2]

//! [error_code_extended3]
namespace error_code_extended
{
  // Specialise the result construction hook for our localised result
  // We hook any non-copy, non-move, non-inplace construction, capturing a stack backtrace
  // if the result is errored.
  template <class T, class U> inline void hook_result_construction(result<T> *res, U && /*unused*/) noexcept
  {
    if(res->has_error())
    {
      // Grab the next extended info slot in the TLS
      extended_error_info &eei = mythreadlocaldata().next();

      // Write the index just grabbed into the spare uint16_t
      BOOST_OUTCOME_V2_NAMESPACE::hooks::set_spare_storage(res, mythreadlocaldata().current - 1);

      // Capture a backtrace into my claimed extended info slot in the TLS
      eei.items = ::backtrace(eei.backtrace.data(), eei.backtrace.size());
    }
  }
}
//! [error_code_extended3]

//! [error_code_extended4]
namespace error_code_extended
{
  // Synthesise a custom exception_ptr from the TLS slot and write it into the outcome
  template <class R> inline void poke_exception(outcome<R> *o)
  {
    if(o->has_error())
    {
      extended_error_info *eei = mythreadlocaldata().get(BOOST_OUTCOME_V2_NAMESPACE::hooks::spare_storage(o));
      if(eei != nullptr)
      {
        // Make a custom string for the exception
        std::string str(o->error().message());
        str.append(" [");
        struct unsymbols  // RAII cleaner for symbols
        {
          char **_{nullptr};
          ~unsymbols() { ::free(_); }
        } symbols{::backtrace_symbols(eei->backtrace.data(), eei->items)};
        if(symbols._ != nullptr)
        {
          for(size_t n = 0; n < eei->items; n++)
          {
            if(n > 0)
            {
              str.append("; ");
            }
            str.append(symbols._[n]);
          }
        }
        str.append("]");

        // Override the payload/exception member in the outcome with our synthesised exception ptr
        BOOST_OUTCOME_V2_NAMESPACE::hooks::override_outcome_exception(o, std::make_exception_ptr(std::runtime_error(str)));
      }
    }
  }
}
//! [error_code_extended4]

//! [error_code_extended5]
namespace error_code_extended
{
  // Specialise the outcome copy and move conversion hook for when our localised result
  // is used as the source for copy construction our localised outcome
  template <class T, class U> inline void hook_outcome_copy_construction(outcome<T> *res, const result<U> & /*unused*/) noexcept
  {
    try
    {
      // when copy constructing from a result<T>, poke in an exception
      poke_exception(res);
    }
    catch(...)
    {
      // Do nothing
    }
  }
  template <class T, class U> inline void hook_outcome_move_construction(outcome<T> *res, result<U> && /*unused*/) noexcept
  {
    try
    {
      // when move constructing from a result<T>, poke in an exception
      poke_exception(res);
    }
    catch(...)
    {
      // Do nothing
    }
  }
}
//! [error_code_extended5]

extern error_code_extended::result<int> func2()
{
  using namespace error_code_extended;
  // At here the stack backtrace is collected and custom message stored in TLS
  return make_error_code(std::errc::operation_not_permitted);
}

extern error_code_extended::outcome<int> func1()
{
  using namespace error_code_extended;
  // At here the custom message and backtrace is assembled into a custom exception_ptr
  return outcome<int>(func2());
}
//! [error_code_extended]

int main()
{
  try
  {
    using namespace error_code_extended;
    outcome<int> r = func1();
    r.value();
    std::cerr << "Unfortunately the extension of the local result<> type to track stack backtraces and keep custom messages did not work\n";
    std::cerr << "No exception was ever thrown!" << std::endl;
    return 1;
  }
  catch(const std::system_error &e)
  {
    std::cerr << "Unfortunately the extension of the local result<> type to track stack backtraces and keep custom messages did not work\n";
    std::cerr << "The exception thrown instead says:\n\n";
    std::cerr << e.what() << std::endl;
    return 1;
  }
  catch(const std::runtime_error &e)
  {
    std::cout << "The extension of the local result<> type to track stack backtraces and keep custom messages worked!\n";
    std::cout << "Here is the extended message which should include also a backtrace:\n\n";
    std::cout << e.what() << std::endl;
    return 0;
  }
  return 1;
}
