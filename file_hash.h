#pragma once

#include <sys/mman.h>

#include <string>
#include <string_view>
#include <vector>

class FileHash {
  public:
    ~FileHash();

    FileHash(std::string_view file_name, std::string_view hash_name);
    FileHash& HashFileContents();
    FileHash& ReadFileHashXattr();

    std::string HashString() const;
    const std::vector<unsigned char>& HashRaw() const;

    int SetHashXattr() const;
    int ClearHashXattr() const;

    bool operator==(const FileHash& other) const;

  private:
    const std::string file_name_;
    const std::string hash_name_;
    const int fd_;
    std::vector<unsigned char> hash_;
};

std::pair<std::unique_ptr<char, std::function<void(char*)>>, size_t> Mmap(int fd);
