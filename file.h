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

#include <stdint.h>

#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>
#include <variant>

enum class HashResult : int {
    OK = 0,
    Error,
};

class MappedFile {
 public: 
  static std::unique_ptr<MappedFile> Create(std::string_view path);

  virtual ~MappedFile();
  virtual std::vector<uint8_t> HashContents(std::string_view hash_name) = 0;
};

class File {
 public:
  static std::unique_ptr<File> Create(std::string_view path);
  virtual ~File();

  virtual bool is_accessible(bool write) = 0;

  virtual std::optional<std::vector<uint8_t>> GetHashMetadata(
      std::string_view hash_name) = 0;
  virtual HashResult SetHashMetadata(
      std::string_view hash_name, const std::vector<uint8_t>& value) = 0;
  virtual HashResult RemoveHashMetadata(std::string_view hash_name) = 0;

  virtual std::unique_ptr<MappedFile> Load() = 0;
};
