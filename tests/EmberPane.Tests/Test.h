#pragma once

// A deliberately tiny test harness: register with TEST, assert with CHECK*.
// A failed check throws, so the rest of that test is skipped and the next one
// runs. The exit code is the number of failures, for build scripts.

#include <cmath>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace Test
{
    struct Failure
    {
        std::string Message;
    };

    struct Case
    {
        const char* Name;
        std::function<void()> Body;
    };

    inline std::vector<Case>& Registry()
    {
        static std::vector<Case> cases;
        return cases;
    }

    struct Registrar
    {
        Registrar(const char* name, std::function<void()> body) { Registry().push_back({ name, std::move(body) }); }
    };

    template <typename T>
    std::string Show(T const& value)
    {
        if constexpr (std::is_same_v<T, std::wstring>)
        {
            std::string narrow;
            for (wchar_t c : value) narrow.push_back(c < 128 ? static_cast<char>(c) : '?');
            return "\"" + narrow + "\"";
        }
        else if constexpr (std::is_enum_v<T>)
        {
            return std::to_string(static_cast<int>(value));
        }
        else if constexpr (requires(std::ostream& o, T const& v) { o << v; })
        {
            std::ostringstream out;
            out.precision(10);
            out << value;
            return out.str();
        }
        else
        {
            return "<value>";
        }
    }

    [[noreturn]] inline void Fail(const char* file, int line, std::string const& message)
    {
        std::ostringstream out;
        out << file << "(" << line << "): " << message;
        throw Failure{ out.str() };
    }
}

#define TEST_CONCAT2(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT2(a, b)

#define TEST(name)                                                                 \
    static void name();                                                            \
    static ::Test::Registrar TEST_CONCAT(registrar_, name)(#name, name);           \
    static void name()

#define CHECK(condition)                                                           \
    do { if (!(condition)) ::Test::Fail(__FILE__, __LINE__, "CHECK(" #condition ") failed"); } while (0)

#define CHECK_EQ(expected, actual)                                                 \
    do {                                                                           \
        /* Copies, not references: (actual) is often a member of a       */      \
        /* temporary, e.g. Match(...)->DeviceKey, gone by the next line. */      \
        auto e_ = (expected);                                                    \
        auto a_ = (actual);                                                      \
        if (!(e_ == a_))                                                           \
            ::Test::Fail(__FILE__, __LINE__, "expected " + ::Test::Show(e_) +      \
                         ", got " + ::Test::Show(a_) + "  [" #actual "]");        \
    } while (0)

#define CHECK_NEAR(expected, actual, tolerance)                                    \
    do {                                                                           \
        double e_ = (expected), a_ = (actual);                                     \
        if (!(std::abs(e_ - a_) <= (tolerance)))                                   \
            ::Test::Fail(__FILE__, __LINE__, "expected " + ::Test::Show(e_) +      \
                         " +/- " #tolerance ", got " + ::Test::Show(a_));          \
    } while (0)

#define CHECK_RANGE(low, high, actual)                                             \
    do {                                                                           \
        double a_ = (actual);                                                      \
        if (!(a_ >= (low) && a_ <= (high)))                                        \
            ::Test::Fail(__FILE__, __LINE__, "expected " #actual " in [" #low ", " \
                         #high "], got " + ::Test::Show(a_));                      \
    } while (0)
