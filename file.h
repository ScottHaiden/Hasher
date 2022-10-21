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

#include <stdint.h>

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

enum class HashResult : int {
    OK = 0,
    Error,
};

class File {
 public:
  static std::unique_ptr<File> Create(
          std::string_view path, std::string_view hashname);
  static std::string hash_to_string(const std::vector<uint8_t>& hash);

  virtual ~File();
  virtual std::optional<std::vector<uint8_t>> GetHashMetadata() = 0;
  virtual std::optional<std::vector<uint8_t>> HashFileContents() = 0;
  virtual bool is_accessible(bool write) = 0;

  virtual HashResult UpdateHashMetadata(const std::vector<uint8_t>& value) = 0;
  virtual HashResult RemoveHashMetadata() = 0;
};
