// This file is part of Hasher.
//
// Haser is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// Haser is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// Haser. If not, see <https://www.gnu.org/licenses/>.

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

std::mutex* GlobalWriteLock();

template <typename... T>
void WriteLocked(FILE* stream, T... args);

// Implementation details
template <typename... T>
void WriteLocked(FILE* stream, T... args) {
    auto* const mu = GlobalWriteLock();
    const std::lock_guard<std::mutex> l(*mu);
    fprintf(stream, std::forward<T>(args)...);
}

template <typename Fn>
class Cleanup {
 public:
  Cleanup(const Fn& fn) : fn_(fn) {}
  ~Cleanup() { fn_(); }

 private:
  const Fn fn_;
};
