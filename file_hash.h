#pragma once

#include <sys/mman.h>

#include <string>
#include <string_view>
#include <vector>

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
    const std::vector<unsigned char>& HashRaw() const;
    void SetHashXattr() const;
    void ClearHashXattr() const;

    bool operator==(const FileHash& other) const;

  private:
    std::string file_name_;
    std::string hash_name_;
    int fd_;
    std::vector<unsigned char> hash_;
};

std::pair<std::unique_ptr<char, std::function<void(char*)>>, size_t> Mmap(int fd);
