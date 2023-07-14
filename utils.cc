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

#include <fts.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "common.h"
#include "utils.h"

namespace {
char** DefaultDirectories() {
    static char kDot[] = ".";
    static char* kDirectories[] = {kDot, nullptr};
    return kDirectories;
}

class AtomicFnameIterator final : public FnameIterator {
  public:
    ~AtomicFnameIterator() override;
    AtomicFnameIterator(char** first);

    std::string GetNext() override;
    void Start() override;
    
  private:
    std::atomic<char**> cur_;
};

class SocketFnameIterator final : public FnameIterator {
  public:
    ~SocketFnameIterator() override;
    explicit SocketFnameIterator(char** directories);
    std::string GetNext() override;
    void Start() override;

    bool CheckDirectories() const;

  private:
    int rfd() const;
    int wfd() const;

    char** const directories_;
    const std::array<int, 2> socket_fds_;

    std::thread thread_;
};

AtomicFnameIterator::~AtomicFnameIterator() = default;
AtomicFnameIterator::AtomicFnameIterator(char** first) : cur_(first) {}

std::string AtomicFnameIterator::GetNext() {
    char** ret = nullptr;
    while (true) {
        ret = cur_.load();
        if (!*ret) return "";
        if (cur_.compare_exchange_weak(ret, ret + 1)) break;
    }
    return std::string(*ret);
}

void AtomicFnameIterator::Start() {}

SocketFnameIterator::~SocketFnameIterator() {
    if (thread_.joinable()) {
        thread_.join();
    } else {
        if (close(wfd())) DIE("close wfd");
        if (close(rfd())) DIE("close rfd");
    }
}

SocketFnameIterator::SocketFnameIterator(char** directories)
    : directories_(*directories ? directories : DefaultDirectories()),
      socket_fds_([]() {
        std::array<int, 2> r;
        if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, r.data())) DIE("socketpair");
        return r;
      }()) {}

std::string SocketFnameIterator::GetNext() {
    std::string ret;
    ret.resize(4096);
    const int amount = read(rfd(), ret.data(), ret.size());
    if (amount < 0) DIE("read");
    if (amount == 0) return "";
    ret.resize(amount);
    ret.shrink_to_fit();
    return ret;
}

void SocketFnameIterator::Start() {
    thread_ = std::thread([this]() {
        auto* const fts = fts_open(directories_, FTS_NOCHDIR, nullptr);
        if (fts == nullptr) DIE("fts_open");
        const Cleanup closer([fts]() { fts_close(fts); });

        while (true) {
            auto* const cur = fts_read(fts);
            if (cur == nullptr) break;
            if (!S_ISREG(cur->fts_statp->st_mode)) continue;
            const size_t len = strlen(cur->fts_path);
            const ssize_t written = write(wfd(), cur->fts_path, len);
            if (written < 0) DIE("write");
            if (written != len) {
                QUIT("wrote wrong amount (%zu vs %zd)", written, len);
            }
        }

        if (close(wfd())) DIE("close");
    });
}

bool SocketFnameIterator::CheckDirectories() const {
    for (char** dir = directories_; *dir; ++dir) {
        struct stat sb;
        if (stat(*dir, &sb)) DIE("stat failed");
        if (S_ISDIR(sb.st_mode)) continue;
        WriteLocked(stderr, "%s is not a directory.\n", *dir);
        return false;
    }
    return true;
}

int SocketFnameIterator::rfd() const { return socket_fds_[0]; }
int SocketFnameIterator::wfd() const { return socket_fds_[1]; }

std::array<char, 2> ascii_byte(uint8_t byte) {
    static const char* const bytes[] = {
        "00", "01", "02", "03", "04", "05", "06", "07",
        "08", "09", "0a", "0b", "0c", "0d", "0e", "0f",
        "10", "11", "12", "13", "14", "15", "16", "17",
        "18", "19", "1a", "1b", "1c", "1d", "1e", "1f",
        "20", "21", "22", "23", "24", "25", "26", "27",
        "28", "29", "2a", "2b", "2c", "2d", "2e", "2f",
        "30", "31", "32", "33", "34", "35", "36", "37",
        "38", "39", "3a", "3b", "3c", "3d", "3e", "3f",
        "40", "41", "42", "43", "44", "45", "46", "47",
        "48", "49", "4a", "4b", "4c", "4d", "4e", "4f",
        "50", "51", "52", "53", "54", "55", "56", "57",
        "58", "59", "5a", "5b", "5c", "5d", "5e", "5f",
        "60", "61", "62", "63", "64", "65", "66", "67",
        "68", "69", "6a", "6b", "6c", "6d", "6e", "6f",
        "70", "71", "72", "73", "74", "75", "76", "77",
        "78", "79", "7a", "7b", "7c", "7d", "7e", "7f",
        "80", "81", "82", "83", "84", "85", "86", "87",
        "88", "89", "8a", "8b", "8c", "8d", "8e", "8f",
        "90", "91", "92", "93", "94", "95", "96", "97",
        "98", "99", "9a", "9b", "9c", "9d", "9e", "9f",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "a8", "a9", "aa", "ab", "ac", "ad", "ae", "af",
        "b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7",
        "b8", "b9", "ba", "bb", "bc", "bd", "be", "bf",
        "c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7",
        "c8", "c9", "ca", "cb", "cc", "cd", "ce", "cf",
        "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
        "d8", "d9", "da", "db", "dc", "dd", "de", "df",
        "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7",
        "e8", "e9", "ea", "eb", "ec", "ed", "ee", "ef",
        "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
        "f8", "f9", "fa", "fb", "fc", "fd", "fe", "ff",
    };
    const char* const ret = bytes[byte];
    return {ret[0], ret[1]};
}
}

FnameIterator::~FnameIterator() = default;

// static
std::unique_ptr<FnameIterator> FnameIterator::GetInstance(
        bool recurse, char** args) {
    if (recurse) {
        auto ret = std::make_unique<SocketFnameIterator>(args);
        if (!ret->CheckDirectories()) return nullptr;
        return ret;
    }
    return std::make_unique<AtomicFnameIterator>(args);
}

std::string HashToString(const std::vector<uint8_t>& bytes) {
    std::string ret(bytes.size() * 2, '\0');
    for (int i = 0; i < bytes.size(); ++i) {
        const std::array<char, 2> buf = ascii_byte(bytes[i]);
        std::copy(buf.begin(), buf.end(), &ret[i * 2]);
    }
    return ret;
}
