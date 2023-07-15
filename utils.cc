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
    static const std::array<char, 2> bytes[] = {
        {'0', '0'}, {'0', '1'}, {'0', '2'}, {'0', '3'}, {'0', '4'}, {'0', '5'},
        {'0', '6'}, {'0', '7'}, {'0', '8'}, {'0', '9'}, {'0', 'a'}, {'0', 'b'},
        {'0', 'c'}, {'0', 'd'}, {'0', 'e'}, {'0', 'f'}, {'1', '0'}, {'1', '1'},
        {'1', '2'}, {'1', '3'}, {'1', '4'}, {'1', '5'}, {'1', '6'}, {'1', '7'},
        {'1', '8'}, {'1', '9'}, {'1', 'a'}, {'1', 'b'}, {'1', 'c'}, {'1', 'd'},
        {'1', 'e'}, {'1', 'f'}, {'2', '0'}, {'2', '1'}, {'2', '2'}, {'2', '3'},
        {'2', '4'}, {'2', '5'}, {'2', '6'}, {'2', '7'}, {'2', '8'}, {'2', '9'},
        {'2', 'a'}, {'2', 'b'}, {'2', 'c'}, {'2', 'd'}, {'2', 'e'}, {'2', 'f'},
        {'3', '0'}, {'3', '1'}, {'3', '2'}, {'3', '3'}, {'3', '4'}, {'3', '5'},
        {'3', '6'}, {'3', '7'}, {'3', '8'}, {'3', '9'}, {'3', 'a'}, {'3', 'b'},
        {'3', 'c'}, {'3', 'd'}, {'3', 'e'}, {'3', 'f'}, {'4', '0'}, {'4', '1'},
        {'4', '2'}, {'4', '3'}, {'4', '4'}, {'4', '5'}, {'4', '6'}, {'4', '7'},
        {'4', '8'}, {'4', '9'}, {'4', 'a'}, {'4', 'b'}, {'4', 'c'}, {'4', 'd'},
        {'4', 'e'}, {'4', 'f'}, {'5', '0'}, {'5', '1'}, {'5', '2'}, {'5', '3'},
        {'5', '4'}, {'5', '5'}, {'5', '6'}, {'5', '7'}, {'5', '8'}, {'5', '9'},
        {'5', 'a'}, {'5', 'b'}, {'5', 'c'}, {'5', 'd'}, {'5', 'e'}, {'5', 'f'},
        {'6', '0'}, {'6', '1'}, {'6', '2'}, {'6', '3'}, {'6', '4'}, {'6', '5'},
        {'6', '6'}, {'6', '7'}, {'6', '8'}, {'6', '9'}, {'6', 'a'}, {'6', 'b'},
        {'6', 'c'}, {'6', 'd'}, {'6', 'e'}, {'6', 'f'}, {'7', '0'}, {'7', '1'},
        {'7', '2'}, {'7', '3'}, {'7', '4'}, {'7', '5'}, {'7', '6'}, {'7', '7'},
        {'7', '8'}, {'7', '9'}, {'7', 'a'}, {'7', 'b'}, {'7', 'c'}, {'7', 'd'},
        {'7', 'e'}, {'7', 'f'}, {'8', '0'}, {'8', '1'}, {'8', '2'}, {'8', '3'},
        {'8', '4'}, {'8', '5'}, {'8', '6'}, {'8', '7'}, {'8', '8'}, {'8', '9'},
        {'8', 'a'}, {'8', 'b'}, {'8', 'c'}, {'8', 'd'}, {'8', 'e'}, {'8', 'f'},
        {'9', '0'}, {'9', '1'}, {'9', '2'}, {'9', '3'}, {'9', '4'}, {'9', '5'},
        {'9', '6'}, {'9', '7'}, {'9', '8'}, {'9', '9'}, {'9', 'a'}, {'9', 'b'},
        {'9', 'c'}, {'9', 'd'}, {'9', 'e'}, {'9', 'f'}, {'a', '0'}, {'a', '1'},
        {'a', '2'}, {'a', '3'}, {'a', '4'}, {'a', '5'}, {'a', '6'}, {'a', '7'},
        {'a', '8'}, {'a', '9'}, {'a', 'a'}, {'a', 'b'}, {'a', 'c'}, {'a', 'd'},
        {'a', 'e'}, {'a', 'f'}, {'b', '0'}, {'b', '1'}, {'b', '2'}, {'b', '3'},
        {'b', '4'}, {'b', '5'}, {'b', '6'}, {'b', '7'}, {'b', '8'}, {'b', '9'},
        {'b', 'a'}, {'b', 'b'}, {'b', 'c'}, {'b', 'd'}, {'b', 'e'}, {'b', 'f'},
        {'c', '0'}, {'c', '1'}, {'c', '2'}, {'c', '3'}, {'c', '4'}, {'c', '5'},
        {'c', '6'}, {'c', '7'}, {'c', '8'}, {'c', '9'}, {'c', 'a'}, {'c', 'b'},
        {'c', 'c'}, {'c', 'd'}, {'c', 'e'}, {'c', 'f'}, {'d', '0'}, {'d', '1'},
        {'d', '2'}, {'d', '3'}, {'d', '4'}, {'d', '5'}, {'d', '6'}, {'d', '7'},
        {'d', '8'}, {'d', '9'}, {'d', 'a'}, {'d', 'b'}, {'d', 'c'}, {'d', 'd'},
        {'d', 'e'}, {'d', 'f'}, {'e', '0'}, {'e', '1'}, {'e', '2'}, {'e', '3'},
        {'e', '4'}, {'e', '5'}, {'e', '6'}, {'e', '7'}, {'e', '8'}, {'e', '9'},
        {'e', 'a'}, {'e', 'b'}, {'e', 'c'}, {'e', 'd'}, {'e', 'e'}, {'e', 'f'},
        {'f', '0'}, {'f', '1'}, {'f', '2'}, {'f', '3'}, {'f', '4'}, {'f', '5'},
        {'f', '6'}, {'f', '7'}, {'f', '8'}, {'f', '9'}, {'f', 'a'}, {'f', 'b'},
        {'f', 'c'}, {'f', 'd'}, {'f', 'e'}, {'f', 'f'},
    };
    return bytes[byte];
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
