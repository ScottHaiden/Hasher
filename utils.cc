// This file is part of Hasher.
//
// Haser is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// Haser is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// Haser. If not, see <https://www.gnu.org/licenses/>.

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
