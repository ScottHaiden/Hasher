#include <stdlib.h>
#include <iostream>

#include "common.h"
#include "file_hash.h"

#include <atomic>
#include <thread>
#include <vector>

enum class HashStatus : unsigned {
    OK = 0,
    MISMATCH = (1 << 0),
    ERROR = (1 << 1),
};

class FnameIterator {
  public:
    virtual ~FnameIterator() = default;
    virtual std::string GetNext() = 0;
};

class AtomicFnameIterator : public FnameIterator {
  public:
    ~AtomicFnameIterator() override = default;
    AtomicFnameIterator(char** first) : cur_(first) {}

    std::string GetNext() override {
        char** ret = nullptr;
        while (true) {
            ret = cur_.load();
            if (!*ret) return "";
            if (cur_.compare_exchange_weak(ret, ret + 1)) break;
        }
        return std::string(*ret);
    }
    
  private:
    std::atomic<char**> cur_;
};

void Worker(
        FnameIterator* iterator,
        std::string_view hashname,
        const std::function<HashStatus(std::string_view,
                                       std::string_view)>& task,
        unsigned* ret) {
    while (true) {
        const std::string cur = iterator->GetNext();
        if (cur.empty()) break;
        *ret |= static_cast<unsigned>(task(cur, hashname));
    }
}

HashStatus ApplyHash(std::string_view fname, std::string_view hashname) {
    auto file = FileHash(fname, hashname).ReadFileHashXattr();
    if (!file.HashRaw().empty()) {
        LOCAL_STRING(line, "skipping %s (already has hash)", fname.data());
        puts(line);
        return HashStatus::ERROR;
    }

    auto fresh = std::move(file).HashFileContents();
    fresh.SetHashXattr();
    LOCAL_STRING(hashline, "%s  %s", fresh.HashString().data(), fname.data());
    puts(hashline);
    return HashStatus::OK;
}

HashStatus CheckHash(std::string_view fname, std::string_view hashname) {
    auto xattr_file = FileHash(fname, hashname).ReadFileHashXattr();
    if (xattr_file.HashRaw().empty()) {
        LOCAL_STRING(line, "skipping %s (missing hash)", fname.data());
        puts(line);
        return HashStatus::ERROR;
    }
    const auto rawhash = xattr_file.HashRaw();

    auto actual = std::move(xattr_file).HashFileContents();
    if (actual.HashRaw() == rawhash) {
        LOCAL_STRING(line, "%s: ok", fname.data());
        puts(line);
        return HashStatus::OK;
    }

    LOCAL_STRING(line, "%s: FAILED", fname.data());
    puts(line);
    return HashStatus::MISMATCH;
}

int main(int argc, char* argv[]) {
    AtomicFnameIterator fn(&argv[1]);
    unsigned result = 0;
    Worker(&fn, "sha512", &CheckHash, &result);

    return result;
}
