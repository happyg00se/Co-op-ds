#pragma once
// Zero-dependency test harness: TEST(name) {...}; CHECK(expr); CHECK_EQ(a,b); run with RUN_ALL_TESTS().
#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace minitest {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
struct Reg { Reg(const char* n, std::function<void()> f) { cases().push_back({n, std::move(f)}); } };
inline int runAll() {
    int failedCases = 0;
    for (auto& c : cases()) {
        const int before = failures();
        c.fn();
        const bool ok = failures() == before;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.name);
        if (!ok) ++failedCases;
    }
    std::printf("%zu tests, %d failed\n", cases().size(), failedCases);
    return failedCases == 0 ? 0 : 1;
}
}  // namespace minitest

#define MT_CAT2(a, b) a##b
#define MT_CAT(a, b) MT_CAT2(a, b)
#define TEST(name) \
    static void MT_CAT(test_fn_, name)(); \
    static ::minitest::Reg MT_CAT(test_reg_, name)(#name, MT_CAT(test_fn_, name)); \
    static void MT_CAT(test_fn_, name)()
#define CHECK(...) do { if (!(__VA_ARGS__)) { ++::minitest::failures(); std::printf("  CHECK failed: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); } } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { ++::minitest::failures(); std::ostringstream _s; _s << _a << " != " << _b; std::printf("  CHECK_EQ failed: %s == %s -> %s (%s:%d)\n", #a, #b, _s.str().c_str(), __FILE__, __LINE__); } } while (0)
#define CHECK_NEAR(a, b, eps) do { auto _a = (a); auto _b = (b); if (std::fabs(static_cast<double>(_a) - static_cast<double>(_b)) > (eps)) { ++::minitest::failures(); std::printf("  CHECK_NEAR failed: %s ~ %s -> %g vs %g (%s:%d)\n", #a, #b, (double)_a, (double)_b, __FILE__, __LINE__); } } while (0)
#define RUN_ALL_TESTS() ::minitest::runAll()
