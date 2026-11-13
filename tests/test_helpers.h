#pragma once

#include <iostream>
#include <string>
#include <sstream>
#include <cstdlib>
#include <type_traits>

namespace axiom::testing {

inline int g_test_count = 0;
inline int g_fail_count = 0;

inline void log_failure(const char* file, int line, const std::string& msg) {
    std::cerr << "FAIL at " << file << ":" << line << " - " << msg << "\n";
    g_fail_count++;
}

template <typename T>
void print_val(std::ostream& os, const T& val) {
    if constexpr (requires { os << val; }) {
        os << val;
    } else if constexpr (std::is_enum_v<T>) {
        os << static_cast<int64_t>(val);
    } else {
        os << "<value>";
    }
}

#define ASSERT_TRUE(cond) \
    do { \
        axiom::testing::g_test_count++; \
        if (!(cond)) { \
            axiom::testing::log_failure(__FILE__, __LINE__, "Expected true: " #cond); \
        } \
    } while (0)

#define ASSERT_FALSE(cond) \
    do { \
        axiom::testing::g_test_count++; \
        if ((cond)) { \
            axiom::testing::log_failure(__FILE__, __LINE__, "Expected false: " #cond); \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        axiom::testing::g_test_count++; \
        if (!((a) == (b))) { \
            std::ostringstream _oss; \
            _oss << "Expected (" #a " == " #b ") but got ("; \
            axiom::testing::print_val(_oss, (a)); \
            _oss << " vs "; \
            axiom::testing::print_val(_oss, (b)); \
            _oss << ")"; \
            axiom::testing::log_failure(__FILE__, __LINE__, _oss.str()); \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        axiom::testing::g_test_count++; \
        if (((a) == (b))) { \
            axiom::testing::log_failure(__FILE__, __LINE__, "Expected (" #a " != " #b ")"); \
        } \
    } while (0)

#define ASSERT_THROWS(expr, ex_type) \
    do { \
        axiom::testing::g_test_count++; \
        bool _caught = false; \
        try { \
            (void)(expr); \
        } catch (const ex_type&) { \
            _caught = true; \
        } catch (...) { \
            axiom::testing::log_failure(__FILE__, __LINE__, "Expected exception " #ex_type " but caught other type for " #expr); \
            _caught = true; \
        } \
        if (!_caught) { \
            axiom::testing::log_failure(__FILE__, __LINE__, "Expected exception " #ex_type " was NOT thrown for " #expr); \
        } \
    } while (0)

} // namespace axiom::testing
