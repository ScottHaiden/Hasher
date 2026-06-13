// This file is part of Hasher.
//
// Hasher is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// Hasher is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// Hasher. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <stdio.h>
#include <stdlib.h>

#include <format>
#include <functional>
#include <memory>
#include <mutex>

#define DIE(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)
#define QUIT(...) do { WriteLocked(stderr, __VA_ARGS__); exit(EXIT_FAILURE); } while (0)

std::mutex* GlobalWriteLock();

template <typename... T>
void WriteLocked(FILE* stream, const std::format_string<T...> fmt, T... args);

// Implementation details
template <typename... T>
void WriteLocked(FILE* stream, const std::format_string<T...> fmt, T... args) {
    const std::string line(std::vformat(fmt.get(), std::make_format_args(args...)));

    auto* const mu = GlobalWriteLock();
    const std::lock_guard<std::mutex> l(*mu);
    fprintf(stream, "%s\n", line.c_str());
}

template <typename Fn>
class Cleanup {
 public:
  Cleanup(const Fn& fn) : fn_(fn) {}
  ~Cleanup() { fn_(); }

 private:
  const Fn fn_;
};
