/* Unit testing for outcomes
(C) 2013-2019 Niall Douglas <http://www.nedproductions.biz/> (1 commit)


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

#include <boost/outcome/std_result.hpp>
#include <boost/outcome/try.hpp>
#include "quickcpplib/boost/test/unit_test.hpp"

namespace outcome = BOOST_OUTCOME_V2_NAMESPACE;

enum class error
{
  test,
  abcde
};

class error_category_impl : public std::error_category
{
public:
  const char *name() const noexcept override { return "test"; }

  std::string message(int code) const noexcept override
  {
    switch(static_cast<error>(code))
    {
    case error::test:
      return "test";
    case error::abcde:
      return "abcde";
    }
    return "unknown";
  }
};
const std::error_category &error_category() noexcept
{
  static error_category_impl instance;
  return instance;
}

boost::system::error_code make_error_code(error error) noexcept
{
  return {static_cast<int>(error), error_category()};
}

namespace std
{
  template <> struct is_error_code_enum<error> : true_type
  {
  };
}  // namespace std

template <typename T> using enum_result = outcome::basic_result<T, error, outcome::policy::default_policy<T, error, void>>;

enum_result<int> test()
{
  return 5;
}

outcome::std_result<int> abc()
{
  static_assert(std::is_error_code_enum<error>::value, "custom enum is not marked convertible to error code");
  static_assert(std::is_constructible<boost::system::error_code, error>::value, "error code is not explicitly constructible from custom enum");
  static_assert(std::is_convertible<error, boost::system::error_code>::value, "error code is not implicitly constructible from custom enum");
  boost::system::error_code ec = error::test;  // custom enum is definitely convertible to error code
  BOOST_OUTCOME_TRY(test());               // hence this should compile, as implicit conversions work here
  (void) ec;

  // But explicit conversions are required between dissimilar basic_result, implicit conversions are disabled
  static_assert(std::is_constructible<outcome::std_result<int>, enum_result<int>>::value, "basic_result with error code is not explicitly constructible from basic_result with custom enum");
  static_assert(!std::is_convertible<enum_result<int>, outcome::std_result<int>>::value, "basic_result with error code is implicitly constructible from basic_result with custom enum");
  return 5;
}

BOOST_OUTCOME_AUTO_TEST_CASE(issues_203_test, "enum convertible to error code works as designed")
{
  BOOST_CHECK(abc().value() == 5);
}
