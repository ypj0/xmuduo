#pragma once

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

inline void check(bool condition, const char* expression, int line) {
    if (!condition) {
        throw std::runtime_error("Line " + std::to_string(line) + ": " + expression);
    }
}
#define CHECK(...) check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __LINE__)

template<typename Exception, typename Function>
void expectThrows(Function&& function) {
    try {
        std::forward<Function>(function)();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error("Expected exception was not thrown");
}

template<typename Function>
int runTests(Function&& function) {
    try {
        std::forward<Function>(function)();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
