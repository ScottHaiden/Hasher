#pragma once

#include <stdio.h>
#include <stdlib.h>

#include <functional>
#include <memory>
#include <mutex>

#define DIE(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)
#define QUIT(...) do { printf(__VA_ARGS__); exit(EXIT_FAILURE); } while (0)
#define LOCAL_STRING(strname, ...) \
    char strname[snprintf(NULL, 0, __VA_ARGS__) + 1]; \
    snprintf(strname, sizeof(strname), __VA_ARGS__)

template <typename P, typename D>
std::unique_ptr<P, std::function<void(P*)>> MakeUnique(P* p, D deleter) {
    return std::unique_ptr<P, std::function<void(P*)>>(p, deleter);
}

std::mutex* GlobalWriteLock();

template <typename... T>
void WriteLocked(FILE* stream, T... args);

// Implementation details
template <typename... T>
void WriteLocked(FILE* stream, T... args) {
    auto* const mu = GlobalWriteLock();
    mu->lock();
    fprintf(stream, std::forward<T>(args)...);
    mu->unlock();
}
