#include "common.h"

#include <mutex>

std::mutex* GlobalWriteLock() {
    static std::mutex mu;
    return &mu;
}
