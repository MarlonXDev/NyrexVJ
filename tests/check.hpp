#pragma once

#include <cstdio>

namespace testing {

inline int& failures() {
    static int value = 0;
    return value;
}

inline int& checks() {
    static int value = 0;
    return value;
}

inline void check(bool condition, const char* expression, int line) {
    ++checks();
    if (!condition) {
        ++failures();
        std::fprintf(stderr, "FAILED line %d: %s\n", line, expression);
    }
}

inline int finish() {
    std::printf("%d checks, %d failed\n", checks(), failures());
    return failures() == 0 ? 0 : 1;
}

}

#define CHECK(expr) testing::check((expr), #expr, __LINE__)
