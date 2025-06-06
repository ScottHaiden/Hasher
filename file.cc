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

#include "file.h"

#include <fcntl.h>
#include <openssl/evp.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform.h"
#include "common.h"

namespace {
std::optional<std::string_view> load_file(std::string_view path) {
    const std::string str(path);
    const int fd = open(str.c_str(), open_flags(str.c_str()));

    if (fd < 0) return std::nullopt;

    const Cleanup closer([fd]() { close(fd); });

    struct stat buf;
    if (fstat(fd, &buf) < 0) return std::nullopt;
    const size_t len = buf.st_size;

    const void* const mem = mmap(nullptr, len, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mem == MAP_FAILED) return std::nullopt;

    return std::string_view(static_cast<const char*>(mem), len);
}

void unload_buffer(std::string_view buf) {
    auto* const data = const_cast<char*>(buf.data());
    munmap(data, buf.size());
}

class FileImpl final : public File {
  public:
    explicit FileImpl(std::string_view path);

    ~FileImpl() override;

    bool is_accessible(bool write) override;

    std::optional<std::vector<uint8_t>> GetHashMetadata(
        std::string_view hash_name) override;
    HashResult SetHashMetadata(std::string_view hash_name,
                               const std::vector<uint8_t>& value) override;
    HashResult RemoveHashMetadata(std::string_view hash_name) override;
    std::unique_ptr<OpenFile> Open() override;

  private:
    const std::string path_;
};

FileImpl::~FileImpl() = default;
FileImpl::FileImpl(std::string_view path) : path_(path) {}

bool FileImpl::is_accessible(bool write) {
    const int amode = R_OK | (write ? W_OK : 0);
    return access(path_.c_str(), amode) == 0;
}

std::optional<std::vector<uint8_t>> FileImpl::GetHashMetadata(
        std::string_view hash_name) {
    LOCAL_STRING(attrname, "hash.%s", std::string(hash_name).c_str());

    std::vector<uint8_t> buf(EVP_MAX_MD_SIZE);
    size_t size = buf.size();
    const int attr_result = get_attr(path_.c_str(), attrname, buf.data(), &size);
    if (attr_result < 0) DIE("getxattr");
    if (attr_result > 0) return std::nullopt;
    buf.resize(size);
    return buf;
}

HashResult FileImpl::SetHashMetadata(std::string_view hash_name,
                           const std::vector<uint8_t>& value) {
    LOCAL_STRING(attrname, "hash.%s", std::string(hash_name).c_str());
    const auto* const converted = reinterpret_cast<const char*>(value.data());
    const int result = set_attr(
            path_.c_str(), attrname, converted, value.size());
    if (result == 0) return HashResult::OK;
    if (result < 0) DIE("set_attr");
    return HashResult::Error;
}

HashResult FileImpl::RemoveHashMetadata(std::string_view hash_name) {
    LOCAL_STRING(attrname, "hash.%s", std::string(hash_name).c_str());
    const int result = remove_attr(path_.c_str(), attrname);
    if (result == 0) return HashResult::OK;
    if (result > 0) return HashResult::Error;
    DIE("remove_attr");
}

std::unique_ptr<OpenFile> FileImpl::Open() {
    if (!this->is_accessible(false)) return nullptr;
    return OpenFile::Create(path_);
}

class OpenFileImpl final : public OpenFile {
 public:
  OpenFileImpl(int fd);
  ~OpenFileImpl() override;
  std::unordered_map<std::string, std::vector<uint8_t>> HashContents(
      std::span<const std::string_view> hash_names) override;

 private:
  EVP_MD_CTX* get_hasher(std::string_view hashname);

  const int fd_;
};

OpenFileImpl::OpenFileImpl(int fd) : fd_(fd) {}
OpenFileImpl::~OpenFileImpl() { close(fd_); }

std::unordered_map<std::string, std::vector<uint8_t>>
OpenFileImpl::HashContents(std::span<const std::string_view> hash_names) {
  if (hash_names.empty()) return {};
  std::vector<std::pair<std::string, EVP_MD_CTX*>> hashers;
  const Cleanup ctx_freer([&hashers]() {
    for (auto& [unused_key, ptr] : hashers) EVP_MD_CTX_free(ptr);
  });
  for (const auto& hash_name : hash_names) {
    hashers.push_back({std::string(hash_name), get_hasher(hash_name)});
  }
  std::vector<char> buf(4 << 20, '\0');
  while (true) {
    const ssize_t amount = read(fd_, &buf[0], buf.size());
    if (amount < 0) DIE("read");
    if (amount == 0) break;

    for (auto& [hash_name, ctx] : hashers) {
      EVP_DigestUpdate(ctx, &buf[0], amount);
    }
  }

  std::unordered_map<std::string, std::vector<uint8_t>> ret;
  for (auto& [hash_name, ctx] : hashers) {
    std::vector<uint8_t> buf(EVP_MAX_MD_SIZE);
    unsigned md_len;
    EVP_DigestFinal_ex(ctx, buf.data(), &md_len);
    buf.resize(md_len);
    ret[hash_name] = std::move(buf);
  }
  return ret;
}

EVP_MD_CTX* OpenFileImpl::get_hasher(std::string_view hashname) {
  auto* const md = EVP_get_digestbyname(std::string(hashname).c_str());
  if (!md) QUIT("hash type not found");

  auto* const ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, md, nullptr);

  return ctx;
}
}

OpenFile::~OpenFile() = default;
File::~File() = default;

// static
std::unique_ptr<OpenFile> OpenFile::Create(std::string_view path) {
  const int oflags = open_flags(std::string(path).c_str());
  const int fd = open(std::string(path).c_str(), oflags);
  if (fd < 0) return nullptr;

  return std::make_unique<OpenFileImpl>(fd);
}

// static
std::unique_ptr<File> File::Create(std::string_view path) {
    return std::make_unique<FileImpl>(path);
}
