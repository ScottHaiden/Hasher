#include <fts.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <mutex>
#include <atomic>

#include "common.h"
#include "utils.h"

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

SocketFnameIterator::~SocketFnameIterator() { thread_.join(); }

SocketFnameIterator::SocketFnameIterator() {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds) < 0) DIE("socketpair");
    rfd_ = fds[0];
    wfd_ = fds[1];
}

std::string SocketFnameIterator::GetNext() {
    std::string ret;
    ret.resize(4096);
    const int amount = read(rfd_, ret.data(), ret.size());
    if (amount < 0) DIE("read");
    if (amount == 0) return "";
    ret.resize(amount);
    ret.shrink_to_fit();
    return ret;
}

void SocketFnameIterator::Recurse() {
    constexpr char kCurDir[] = ".";
    thread_ = std::thread([this, kCurDir]() {
        char cur_dir[sizeof(kCurDir)];
        strcpy(cur_dir, kCurDir);
        char* dir_list[] = {&cur_dir[0], nullptr};

        auto fts = MakeUnique(fts_open(dir_list, FTS_NOCHDIR, nullptr),
                              &fts_close);
        if (fts == nullptr) DIE("fts_open");

        while (true) {
            auto* const cur = fts_read(fts.get());
            if (cur == nullptr) break;
            if (!S_ISREG(cur->fts_statp->st_mode)) continue;
            const size_t len = strlen(cur->fts_path);
            const ssize_t written = write(wfd_, cur->fts_path, len);
            if (written < 0) DIE("write");
            if (written != len) {
                QUIT("wrote wrong amount (%zu vs %zd)", written, len);
            }
        }

        if (close(wfd_)) DIE("close");
    });
}
