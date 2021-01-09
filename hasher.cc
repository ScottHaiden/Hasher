#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <vector>
#include <algorithm>
#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#define DIE(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)
#define QUIT(...) do { printf(__VA_ARGS__); exit(EXIT_FAILURE); } while (0)
#define LOCAL_STRING(strname, ...) \
    char strname[snprintf(NULL, 0, __VA_ARGS__) + 1]; \
    snprintf(strname, sizeof(strname), __VA_ARGS__)

template <typename P, typename D>
std::unique_ptr<P, std::function<void(P*)>> MakeUnique(P* p, D deleter) {
    return std::unique_ptr<P, std::function<void(P*)>>(p, deleter);
}

std::pair<std::unique_ptr<char, std::function<void(char*)>>, size_t> Mmap(int fd) {
    struct stat sb;
    if (fstat(fd, &sb) < 0) DIE("fstat");
    const size_t file_len = sb.st_size;

    void* const mapped = mmap(nullptr, file_len, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) DIE("mmap");

    auto cleanup = [file_len](char* mapped) {
        if (munmap(mapped, file_len) < 0) DIE("munmap");
    };

    auto ptr = MakeUnique(static_cast<char*>(mapped), std::move(cleanup));

    return {std::move(ptr), file_len};
}

class FileHash {
  public:
    ~FileHash();
    FileHash(FileHash&& other);
    FileHash& operator=(FileHash&& other);

    FileHash(std::string_view file_name, std::string_view hash_name);
    FileHash& HashFileContents() &;
    FileHash& ReadFileHashXattr() &;
    FileHash&& HashFileContents() &&;
    FileHash&& ReadFileHashXattr() &&;

    std::string HashString() const;
    std::vector<unsigned char> HashRaw() const;
    void SetHashXattr() const;
    void ClearHashXattr() const;

    bool operator==(const FileHash& other) const;

  private:
    std::string file_name_;
    std::string hash_name_;
    int fd_;
    std::vector<unsigned char> hash_;
};

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
    const auto& [file_data, file_len] = Mmap(fd_);

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

std::vector<unsigned char> FileHash::HashRaw() const { return hash_; }
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

void FileHash::SetHashXattr() const {
    LOCAL_STRING(attrname, "user.hash.%s", hash_name_.data());

    if (fsetxattr(fd_, attrname, hash_.data(), hash_.size(), 0)) {
        DIE("fsetxattr");
    }
}

int main(int argc, char* argv[]) {
    auto read_self = FileHash(argv[0], "sha512").ReadFileHashXattr();
    auto hashed_self = FileHash(argv[0], "sha512").HashFileContents();
    std::cout << read_self.HashString() << std::endl;
    std::cout << hashed_self.HashString() << std::endl;
    hashed_self.SetHashXattr();
    return EXIT_SUCCESS;
}
