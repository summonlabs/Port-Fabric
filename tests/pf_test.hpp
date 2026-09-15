#pragma once

// Minimal, dependency free test harness for the Port Fabric suite.
//
// The harness deliberately has no timeouts and no watchdogs: a hanging test is a
// defect in the runtime under test and must be diagnosed rather than masked by
// terminating the process.

#include <cstdio>
#include <exception>
#include <string>
#include <type_traits>
#include <vector>

#include "portfabric/digest.hpp"

namespace pf_test {

struct Case {
  std::string name;
  void (*function)();
};

inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

inline int& failure_count() {
  static int failures = 0;
  return failures;
}

inline int& check_count() {
  static int checks = 0;
  return checks;
}

inline std::string& current_case() {
  static std::string name;
  return name;
}

struct Registrar {
  Registrar(const char* name, void (*function)()) { registry().push_back(Case{name, function}); }
};

inline bool check(bool condition, const char* expression, const char* file, int line) {
  ++check_count();
  if (!condition) {
    ++failure_count();
    std::printf("FAIL %s\n  %s:%d\n  expression: %s\n", current_case().c_str(), file, line,
                expression);
    std::fflush(stdout);
  }
  return condition;
}

inline void fail(const char* message, const char* file, int line) {
  ++check_count();
  ++failure_count();
  std::printf("FAIL %s\n  %s:%d\n  %s\n", current_case().c_str(), file, line, message);
  std::fflush(stdout);
}

struct Abort {};

inline void require(bool condition, const char* expression, const char* file, int line) {
  if (!check(condition, expression, file, line)) {
    throw Abort{};
  }
}

template <class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
inline std::string render_value(T value) {
  return std::to_string(value);
}

/// Renders an enumerator through its domain renderer, found by argument
/// dependent lookup.
template <class T, std::enable_if_t<std::is_enum_v<T>, int> = 0>
inline std::string render_value(T value) {
  return std::string(to_string(value));
}

template <class T,
          std::enable_if_t<!std::is_integral_v<T> && !std::is_enum_v<T>, int> = 0>
inline std::string render_value(const T& value) {
  return value.to_string();
}

inline std::string render_value(std::string_view value) { return std::string(value); }

inline std::string render_value(bool value) { return value ? "true" : "false"; }
inline std::string render_value(const std::string& value) { return value; }
inline std::string render_value(const char* value) { return value == nullptr ? "<null>" : value; }
inline std::string render_value(const portfabric::Digest& value) { return value.to_hex(); }

inline int run_all() {
  int executed = 0;
  for (const Case& test : registry()) {
    current_case() = test.name;
    const int before = failure_count();
    try {
      test.function();
    } catch (const Abort&) {
      // The failure was already reported by require().
    } catch (const std::exception& error) {
      fail(error.what(), "test case", 0);
    } catch (...) {
      fail("test case threw a non standard exception", "test case", 0);
    }
    ++executed;
    const int after = failure_count();
    std::printf("%-78s %s\n", test.name.c_str(), after == before ? "ok" : "FAILED");
    std::fflush(stdout);
  }
  std::printf("\n%d test cases executed, %d checks, %d failures\n", executed, check_count(),
              failure_count());
  std::fflush(stdout);
  return failure_count() == 0 ? 0 : 1;
}

}  // namespace pf_test

#define PF_TEST(name)                                                        \
  static void name();                                                        \
  static const ::pf_test::Registrar pf_registrar_##name(#name, &name);       \
  static void name()

#define PF_CHECK(condition) ::pf_test::check((condition), #condition, __FILE__, __LINE__)

#define PF_REQUIRE(condition) ::pf_test::require((condition), #condition, __FILE__, __LINE__)

#define PF_CHECK_EQ(lhs, rhs)                                                          \
  do {                                                                                 \
    const auto pf_lhs = (lhs);                                                         \
    const auto pf_rhs = (rhs);                                                         \
    if (!::pf_test::check(pf_lhs == pf_rhs, #lhs " == " #rhs, __FILE__, __LINE__)) {    \
      std::printf("  lhs: %s\n", ::pf_test::render_value(pf_lhs).c_str());             \
      std::printf("  rhs: %s\n", ::pf_test::render_value(pf_rhs).c_str());             \
      std::fflush(stdout);                                                             \
    }                                                                                  \
  } while (false)

#define PF_REQUIRE_EQ(lhs, rhs)                                                        \
  do {                                                                                 \
    const auto pf_lhs = (lhs);                                                         \
    const auto pf_rhs = (rhs);                                                         \
    if (!::pf_test::check(pf_lhs == pf_rhs, #lhs " == " #rhs, __FILE__, __LINE__)) {    \
      std::printf("  lhs: %s\n", ::pf_test::render_value(pf_lhs).c_str());             \
      std::printf("  rhs: %s\n", ::pf_test::render_value(pf_rhs).c_str());             \
      std::fflush(stdout);                                                             \
      throw ::pf_test::Abort{};                                                        \
    }                                                                                  \
  } while (false)

#define PF_CHECK_CODE(expression, expected)                                             \
  do {                                                                                  \
    const ::portfabric::Outcome pf_outcome = (expression);                              \
    if (!::pf_test::check(pf_outcome.code() == (expected),                              \
                          #expression " has code " #expected, __FILE__, __LINE__)) {    \
      std::printf("  actual: %s\n", pf_outcome.to_string().c_str());                   \
      std::fflush(stdout);                                                              \
    }                                                                                   \
  } while (false)
