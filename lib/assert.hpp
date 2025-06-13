#pragma once

#include <iostream>
#include <stdexcept>
#include <source_location>
#include <stacktrace>


// check if stacktrace is available
#if __has_include(<stacktrace>)
#include <stacktrace>
constexpr bool stacktrace_available = true;
#else
#warn "Stacktrace support is not available. Please ensure you have a compatible compiler and standard library."
constexpr bool stacktrace_available = false;
#endif

void aassert(bool condition, std::string message = "Assertion failed",
    const std::source_location& location = std::source_location::current()) {
    if (!condition) {
        std::cerr << "[Assertion failed] " << message 
                << " at " << location.file_name() 
                << ":" << location.line() 
                << " in function " << location.function_name() << std::endl;


        if constexpr (stacktrace_available) {
            std::cerr << "Stack trace:\n";
            std::cout << std::stacktrace::current() << '\n';    
        }

        throw std::runtime_error(message);
    }
}

#ifdef NDEBUG
#define dassert(condition, message) ((void)0)
#else
#define dassert(condition, message) aassert(condition, message, std::source_location::current())
#endif

#define fassert aassert


class fake_ostream {
    public:

    template<typename T>
    fake_ostream& operator<<(const T&) {
        return *this;
    }
    
    fake_ostream& operator<<(std::ostream& (*)(std::ostream&)) {
        return *this;
    }
};

#define _cout fake_ostream()
