#pragma once

#include <iostream>
#include <stdexcept>

#include <source_location>

#include <stacktrace>
#include <map>
#include <string>

#if __has_include(<stacktrace>)
#  include <stacktrace>
   constexpr bool stacktrace_available = true;
#else
#  pragma message("warning: <stacktrace> not available — stack dumps will be disabled")
   constexpr bool stacktrace_available = false;
#endif


thread_local std::map<uint64_t, std::string> __assert_context;
thread_local uint64_t __next_ctx_id = 0;

struct AssertContext {
    uint64_t id;
    std::string message;
    std::source_location location;

    AssertContext(uint64_t id, const std::string& message, const std::source_location& location)
        : id(id), message(message), location(location) {
            
            if (__assert_context.find(id) != __assert_context.end()) {
                throw std::runtime_error("Assertion context with id " + std::to_string(id) + " already exists: " + __assert_context[id]);
            } else {
                __assert_context[id] = message;
            }
    }

    static AssertContext create(const std::string& message, const std::source_location& location = std::source_location::current()) {
        uint64_t id = __next_ctx_id++;
        return AssertContext(id, message, location);
    }

    ~AssertContext() {
        if (__assert_context.find(id) != __assert_context.end()) {
            __assert_context.erase(id);
        }
    }
};

struct Assert {
    inline void operator()(bool condition, const std::string& message, const std::source_location& location) const {
        if (!condition) {
            std::cerr << "\n[Assertion failed] " << message  << "\n";
            std::cerr << "\t In function: " << location.function_name() << "\n";
            std::cerr << "\t at " << location.file_name() << ":" << location.line() << "\n" << std::endl;

            if constexpr (stacktrace_available) {
                std::cerr << "Stack trace:\n";
                std::cout << std::stacktrace::current() << '\n' << std::endl;
            }

            if (__assert_context.size() > 0) {
                std::cerr << "Context:\n";
                for (const auto& [id, ctx_message] : __assert_context) {
                    std::cerr << "\tID: " << id << " - " << ctx_message << "\n";
                }
            }

            throw std::runtime_error(message);
        }
    }
};

#define __CONCATENATE_IMPL(s1, s2) s1##s2
#define __CONCATENATE(s1, s2) __CONCATENATE_IMPL(s1, s2)
#define __UNIQUE_VAR_NAME(prefix) __CONCATENATE(prefix, __LINE__)

auto __cond_assert = Assert();

#ifndef NDEBUG

#define dassert(condition, message) \
    __cond_assert(condition, message, std::source_location::current())

#define dcontext(message) \
    auto __UNIQUE_VAR_NAME(assert_context) = AssertContext::create(message, std::source_location::current());

#else

#define dassert(condition, message) ((void)0)
#define dcontext(message) ((void)0)

#endif

#define fassert(condition, message) \
    __cond_assert(condition, message, std::source_location::current())

#define fcontext(message) \
    auto __UNIQUE_VAR_NAME(assert_context) = AssertContext::create(message, std::source_location::current()); 


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
