// This file is part of Hasher.
//
// Foobar is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// Foobar is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// Foobar. If not, see <https://www.gnu.org/licenses/>.

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
    const int fd = open(str.c_str(), open_flags());

    const Cleanup closer([fd]() { close(fd); });

    if (fd < 0) return std::nullopt;

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
  FileImpl(std::string_view path, std::string_view hashname);
  ~FileImpl() override;

  std::optional<std::vector<uint8_t>> GetHashMetadata() override;
  std::optional<std::vector<uint8_t>> HashFileContents() override;
  bool is_accessible(bool write) override;
  HashResult UpdateHashMetadata(const std::vector<uint8_t>& value) override;
  HashResult RemoveHashMetadata() override;

 private:
  const std::string path_;
  const std::string hashname_;
};

FileImpl::FileImpl(std::string_view path, std::string_view hashname)
    : path_(path), hashname_(hashname) {}

FileImpl::~FileImpl() = default;

std::optional<std::vector<uint8_t>> FileImpl::HashFileContents() {
    auto* const md = EVP_get_digestbyname(hashname_.c_str());
    if (!md) QUIT("not found");

    auto maybe_contents(load_file(path_));
    if (!maybe_contents) return std::nullopt;
    const auto contents = std::move(maybe_contents).value();
    const Cleanup cleanup([contents]() { unload_buffer(contents); });

    auto* const ctx = EVP_MD_CTX_new();
    const Cleanup ctx_freer([ctx]() { EVP_MD_CTX_free(ctx); });
    EVP_DigestInit_ex(ctx, md, nullptr);
    EVP_DigestUpdate(ctx, contents.data(), contents.size());

    std::vector<uint8_t> buf(EVP_MAX_MD_SIZE);
    unsigned md_len;
    EVP_DigestFinal_ex(ctx, buf.data(), &md_len);

    buf.resize(md_len);
    return buf;
}

bool FileImpl::is_accessible(bool write) {
    const int amode = R_OK | (write ? W_OK : 0);
    return access(path_.c_str(), amode) == 0;
}

std::optional<std::vector<uint8_t>> FileImpl::GetHashMetadata() {
    LOCAL_STRING(attrname, "hash.%s", hashname_.c_str());

    std::vector<uint8_t> buf(EVP_MAX_MD_SIZE);
    size_t size = buf.size();
    const int attr_result = get_attr(path_.c_str(), attrname, buf.data(), &size);
    if (attr_result < 0) DIE("getxattr");
    if (attr_result > 0) return std::nullopt;
    buf.resize(size);
    return buf;
}

HashResult FileImpl::UpdateHashMetadata(const std::vector<uint8_t>& value) {
    LOCAL_STRING(attrname, "hash.%s", hashname_.c_str());
    const auto* const converted = reinterpret_cast<const char*>(value.data());
    const int result =
        set_attr(path_.c_str(), attrname, converted, value.size());
    if (result == 0) return HashResult::OK;
    if (result < 0) DIE("set_attr");
    return HashResult::Error;
}

HashResult FileImpl::RemoveHashMetadata() {
    LOCAL_STRING(attrname, "hash.%s", hashname_.c_str());
    const int result = remove_attr(path_.c_str(), attrname);
    if (result == 0) return HashResult::OK;
    if (result > 0) return HashResult::Error;
    DIE("remove_attr");
}
}

File::~File() = default;

// static
std::unique_ptr<File> File::Create(std::string_view path,
                                   std::string_view hashname) {
    return std::make_unique<FileImpl>(path, hashname);
}

// static
std::string File::hash_to_string(const std::vector<uint8_t>& bytes) {
    std::string ret(bytes.size() * 2, '\0');
    for (int i = 0; i < bytes.size(); ++i) {
        char buf[3];
        snprintf(buf, 3, "%02x", bytes[i]);
        std::copy(&buf[0], &buf[sizeof(buf)], &ret[i * 2]);
    }
    return ret;
}

