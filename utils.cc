#include <ftw.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>

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

SocketFnameIterator::~SocketFnameIterator() {
    if (!child_) return;
    int status = 0;
    const pid_t waited = waitpid(child_, &status, 0);
    if (waited < 0) DIE("waitpid");
    if (waited != child_) {
        QUIT("Waited wrong child %d vs %d\n", waited, child_);
    }
}

SocketFnameIterator::SocketFnameIterator() : child_(0) {
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
    const pid_t pid = fork();
    if (pid < 0) DIE("fork");
    if (pid > 0) {
        child_ = pid;
        if (close(wfd_)) DIE("close in parent");
        return;
    }

    const int fd = dup2(wfd_, fileno(stdout));
    if (fd < 0) DIE("dup2");
    if (fd != fileno(stdout)) {
        QUIT("Mismatched fds: %d vs %d\n", fileno(stdout), wfd_);
    }

    if (nftw("./", +[](const char* fpath, const struct stat* sb,
                              int typeflag, struct FTW*) {
        if (typeflag != FTW_F) return 0;
        const std::string_view path_view(fpath);
        const ssize_t amount =
            write(fileno(stdout), path_view.data(), path_view.size());
        if (amount < 0) DIE("write");
        if (amount != path_view.size()) {
            QUIT("write messed up: %zd vs %zd\n", amount, path_view.size());
        }
        return 0;
    }, 1 << 10, FTW_PHYS) < 0) DIE("nftw");

    if (close(wfd_)) DIE("close in child");
    exit(EXIT_SUCCESS);
}
