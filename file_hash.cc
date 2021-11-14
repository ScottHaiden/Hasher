#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <vector>
#include <algorithm>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>

#include "common.h"
#include "file_hash.h"

FileHash::~FileHash() {
    if (fd_ < 0) return;
    if (close(fd_) < 0) DIE("close");
}
FileHash::FileHash(FileHash&& other) { *this = std::move(other); }
FileHash& FileHash::operator=(FileHash&& other) {
    file_name_ = std::move(other.file_name_);
    hash_name_ = std::move(other.hash_name_);
    fd_ = other.fd_;
    hash_ = std::move(other.hash_);

    other.fd_ = -1;
    return *this;
}
FileHash::FileHash(std::string_view file_name, std::string_view hash_name)
    : file_name_(file_name),
      hash_name_(hash_name),
      fd_(open(file_name_.data(), O_RDONLY)) {
  if (fd_ < 0) {
      LOCAL_STRING(error, "failed to open %s", file_name.data());
      DIE(error);
  }
}

FileHash&& FileHash::HashFileContents() && {
    return std::move(HashFileContents());
}
FileHash& FileHash::HashFileContents() & {
    const auto [file_data, file_len] = Mmap(fd_);

    auto* const md = EVP_get_digestbyname(hash_name_.data());
    if (md == nullptr) QUIT("Invalid digest name '%s'\n", hash_name_.data());
    auto mdctx = MakeUnique(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    EVP_DigestInit_ex(mdctx.get(), md, nullptr);
    EVP_DigestUpdate(mdctx.get(), file_data.get(), file_len);

    unsigned char mdbuf[EVP_MAX_MD_SIZE];
    unsigned md_len;
    EVP_DigestFinal_ex(mdctx.get(), mdbuf, &md_len);

    hash_ = std::vector<unsigned char>(&mdbuf[0], &mdbuf[md_len]);
    return *this;
}

FileHash&& FileHash::ReadFileHashXattr() && {
    return std::move(ReadFileHashXattr());
}
FileHash& FileHash::ReadFileHashXattr() & {
    LOCAL_STRING(attrname, "user.hash.%s", hash_name_.data());

    const ssize_t expected_size = fgetxattr(fd_, attrname, nullptr, 0);
    if (expected_size < 0) {
        if (errno == ENODATA) return *this;
        DIE("getxattr");
    }

    hash_.resize(expected_size);
    const size_t actual_size =
        fgetxattr(fd_, attrname, &hash_[0], expected_size);
    if (actual_size < 0) DIE("getxattr");
    if (actual_size != expected_size) {
        QUIT("attribute size seemed to change (was %d, then %d)\n",
                expected_size, actual_size);
    }
    return *this;
}

const std::vector<unsigned char>& FileHash::HashRaw() const { return hash_; }
std::string FileHash::HashString() const {
    std::string ret;
    ret.resize(hash_.size() * 2);
    for (int i = 0; i < hash_.size(); ++i) {
        char buf[3];
        snprintf(buf, 3, "%02x", hash_[i]);
        std::copy(&buf[0], &buf[sizeof(buf)], &ret[i * 2]);
    }
    return ret;
}

bool FileHash::operator==(const FileHash& other) const {
    if (file_name_ != other.file_name_) return false;
    if (hash_name_ != other.hash_name_) return false;
    if (hash_ != other.hash_) return false;
    return true;
}

int FileHash::SetHashXattr() const {
    LOCAL_STRING(attrname, "user.hash.%s", hash_name_.data());

    if (fsetxattr(fd_, attrname, hash_.data(), hash_.size(), 0) < 0) {
        if (errno != EACCES) DIE("fsetxattr");
        return 1;
    }

    return 0;
}

int FileHash::ClearHashXattr() const {
    LOCAL_STRING(attrname, "user.hash.%s", hash_name_.data());
    if (fremovexattr(fd_, attrname)) {
        if (errno == ENODATA) return 1;
        if (errno == EACCES) return 1;
        DIE("fremovexattr");
    }
    return 0;
}

std::pair<std::unique_ptr<char, std::function<void(char*)>>, size_t>
Mmap(int fd) {
    struct stat sb;
    if (fstat(fd, &sb) < 0) DIE("fstat");
    const size_t file_len = sb.st_size;
    if (!file_len) return {nullptr, 0};

    void* const mapped = mmap(nullptr, file_len, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) DIE("mmap");

    auto cleanup = [file_len](char* mapped) {
        if (munmap(mapped, file_len) < 0) DIE("munmap");
    };

    auto ptr = MakeUnique(static_cast<char*>(mapped), std::move(cleanup));

    return {std::move(ptr), file_len};
}

