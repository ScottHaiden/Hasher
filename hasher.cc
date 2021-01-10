#include <atomic>
#include <iostream>
#include <numeric>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "common.h"
#include "file_hash.h"

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

HashStatus PrintHash(std::string_view fname, std::string_view hashname) {
    auto xattr_file = FileHash(fname, hashname).ReadFileHashXattr();
    if (xattr_file.HashRaw().empty()) return HashStatus::ERROR;

    LOCAL_STRING(line, "%s  %s", xattr_file.HashString().data(), fname.data());
    puts(line);
    return HashStatus::OK;
}

HashStatus ResetHash(std::string_view fname, std::string_view hashname) {
    FileHash(fname, hashname).ClearHashXattr();
    LOCAL_STRING(line, "Resetting hash on %s", fname.data());
    puts(line);
    return HashStatus::OK;
}

struct ArgResults {
    HashStatus (*fn)(std::string_view, std::string_view);
    int num_threads;
    int index;
    bool report_all_errors;
};

void ShowHelp(char* progname) {
    printf("%s [-c] [-h] [-r] [-s] [-t NUM] [-T]\n", progname);
    printf("\n");
    printf("\t-h:     Show this help\n");
    printf("\t-c:     Check hashes\n");
    printf("\t-r:     Reset hashes (remove hash from file's metadata)\n");
    printf("\t-s:     Set hash (find hash and set it in file's metadata)\n");
    printf("\t-p:     Print hash (create a checksum file)\n");
    printf("\t-t NUM: Use NUM threads\n");
    printf("\t-T:     Use default number of threads (1 per CPU core)\n");
    printf("\t-e:     Report all errors (even missing data errors)\n");
    printf("\t-E:     Only report error if found a file with a bad hash.\n");
}

ArgResults ParseArgs(int argc, char* const* argv) {
    ArgResults ret = {
        .fn = nullptr,
        .num_threads = -1,
        .index = 0,
        .report_all_errors = false,
    };

    while (true) {
        switch (getopt(argc, argv, "chrspt:TeE")) {
            case 'T': ret.num_threads = -1;           continue;
            case 'c': ret.fn = &CheckHash;            continue;
            case 'p': ret.fn = &PrintHash;            continue;
            case 'r': ret.fn = &ResetHash;            continue;
            case 's': ret.fn = &ApplyHash;            continue;
            case 't': ret.num_threads = atoi(optarg); continue;
            case 'e': ret.report_all_errors = true;   continue;
            case 'E': ret.report_all_errors = false;  continue;
            case 'h': ShowHelp(argv[0]); exit(0);     break;
            case -1:                                  break;
            default: exit(1);
        }
        break;
    }
    ret.index = optind;
    if (ret.num_threads < 0) ret.num_threads = sysconf(_SC_NPROCESSORS_ONLN);
    if (ret.fn) return ret;

    char* const fname = basename(*argv);
    if (!strcmp(fname, "hasher")) ret.fn = &ApplyHash;
    if (!strcmp(fname, "checker")) ret.fn = &CheckHash;

    return ret;
}

int main(int argc, char* argv[]) {
    auto results = ParseArgs(argc, &argv[0]);
    if (!results.fn) return 1;

    auto iterator =
        std::make_unique<AtomicFnameIterator>(&argv[results.index]);

    std::vector<std::thread> workers;
    workers.reserve(results.num_threads);
    std::vector<unsigned> codes;
    codes.reserve(results.num_threads);

    for (unsigned i = 0; i < results.num_threads; ++i) {
        codes.emplace_back(0);
        workers.emplace_back(
                &Worker, iterator.get(), "sha512", results.fn, &codes.back());
    }

    for (auto& thread : workers) thread.join();

    const unsigned result =
        std::reduce(codes.begin(), codes.end(), 0,
                    [](unsigned memo, unsigned cur) { return memo | cur; });
    if (results.report_all_errors) return result;
    return result & static_cast<unsigned>(HashStatus::MISMATCH);
}
