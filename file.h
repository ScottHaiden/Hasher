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

  virtual HashResult UpdateHashMetadata(const std::vector<uint8_t>& value) = 0;
  virtual HashResult RemoveHashMetadata() = 0;
};
