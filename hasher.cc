#include <atomic>
#include <mutex>
#include <iostream>
#include <numeric>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <sys/socket.h>
#include <sys/wait.h>

#include "common.h"
#include "utils.h"
#include "file.h"

enum class HashStatus : unsigned {
    OK = 0,
    MISMATCH = (1 << 0),
    ERROR = (1 << 1),
};

void Worker(
        FnameIterator* iterator,
        std::string_view hashname,
        const std::function<HashStatus(std::string_view,
                                       std::string_view)>& task,
        std::atomic<unsigned>* ret) {
    while (true) {
        const std::string cur = iterator->GetNext();
        if (cur.empty()) break;
        *ret |= static_cast<unsigned>(task(cur, hashname));
    }
}

HashStatus ApplyHash(std::string_view fname, std::string_view hashname) {
    auto file = File::Create(fname, hashname);
    if (file->GetHashMetadata()) {
        WriteLocked(stderr, "Skipping %s (already has hash)\n", fname.data());
        return HashStatus::ERROR;
    }

    auto fresh = file->HashFileContents();
    if (!fresh) {
        WriteLocked(stderr, "Failed to hash file contents for %s\n", fname.data());
        return HashStatus::ERROR;
    }
    if (file->UpdateHashMetadata(*fresh) != HashResult::OK) {
        WriteLocked(stderr, "Failed to write xattr to %s\n", fname.data());
        return HashStatus::ERROR;
    }
    WriteLocked(stdout, "%s  %s\n",
                File::hash_to_string(*fresh).c_str(), fname.data());
    return HashStatus::OK;
}

HashStatus CheckHash(std::string_view fname, std::string_view hashname) {
    auto xattr_file = File::Create(fname, hashname);
    const auto from_xattr = xattr_file->GetHashMetadata();
    if (!from_xattr) {
        WriteLocked(stdout, "skipping %s (missing hash)\n", fname.data());
        return HashStatus::ERROR;
    }
    const auto from_contents = xattr_file->HashFileContents();
    if (!from_contents) {
        WriteLocked(stderr, "Failed to hash %s\n", fname.data());
        return HashStatus::ERROR;
    }
    if (*from_xattr == *from_contents) {
        WriteLocked(stdout, "%s: OK\n", fname.data());
        return HashStatus::OK;
    }
    
    WriteLocked(stdout, "%s: FAILED\n", fname.data());
    return HashStatus::MISMATCH;
}

HashStatus PrintHash(std::string_view fname, std::string_view hashname) {
    auto hash = File::Create(fname, hashname)->GetHashMetadata();
    if (!hash) return HashStatus::ERROR;
    WriteLocked(stdout, "%s  %s\n",
                File::hash_to_string(*hash).c_str(), fname.data());
    return HashStatus::OK;
}

HashStatus ResetHash(std::string_view fname, std::string_view hashname) {
    auto file = File::Create(fname, hashname);
    if (file->RemoveHashMetadata() != HashResult::OK) {
        WriteLocked(stderr, "Failed to reset hash on %s\n", fname.data());
        return HashStatus::ERROR;
    }
    WriteLocked(stdout, "Resetting hash on %s\n", fname.data());
    return HashStatus::OK;
}

struct ArgResults {
    HashStatus (*fn)(std::string_view, std::string_view);
    int num_threads;
    int index;
    bool report_all_errors;
    const char* hash_fn;
    bool recurse;
};

void ShowHelp(char* progname) {
    printf("%s [-c] [-h] [-r] [-s] [-p] [-t NUM] [-T] [-e] [-E] [-C hashname] filenames..."
           "[-R] [filenames...]\n", progname);
    printf("\n");
    printf("\t-c:      Check hashes\n");
    printf("\t-h:      Show this help\n");
    printf("\t-r:      Reset hashes (remove hash from file's metadata)\n");
    printf("\t-s:      Set hash (Find file's hash and set it in files metadata)\n");
    printf("\t-p:      Print hash (create a checksum file)\n");
    printf("\t-t NUM:  Use NUM threads\n");
    printf("\t-T:      Use one worker thread per CPU\n");
    printf("\t-e:      Report all errors (even missing data errors)\n");
    printf("\t-E:      Only report error if a file has a bad hash\n");
    printf("\t-C NAME: Set hashing function to NAME. (default=sha512)\n");
    printf("\t-R:      Operate recursively over directories.\n");
}

ArgResults ParseArgs(int argc, char* const* argv) {
    static constexpr char kDefaultHash[] = "sha512";
    ArgResults ret = {
        .fn = nullptr,
        .num_threads = 1,
        .index = 0,
        .report_all_errors = false,
        .hash_fn = &kDefaultHash[0],
        .recurse = false,
    };

    while (true) {
        switch (getopt(argc, argv, "chrspt:TeEC:R")) {
            case 'T': ret.num_threads = 0;           continue;
            case 'c': ret.fn = &CheckHash;            continue;
            case 'p': ret.fn = &PrintHash;            continue;
            case 'r': ret.fn = &ResetHash;            continue;
            case 's': ret.fn = &ApplyHash;            continue;
            case 'R': ret.recurse = true;             continue;
            case 'C': ret.hash_fn = optarg;           continue;
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
    if (ret.num_threads <= 0) ret.num_threads = sysconf(_SC_NPROCESSORS_ONLN);
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
        FnameIterator::GetInstance(results.recurse, &argv[results.index]);
    if (!iterator) return 1;
    iterator->Start();

    std::vector<std::thread> workers;
    workers.reserve(results.num_threads);
    std::atomic<unsigned> result;

    for (unsigned i = 0; i < results.num_threads; ++i) {
        workers.emplace_back(&Worker,
                             iterator.get(),
                             results.hash_fn,
                             results.fn,
                             &result);
    }

    for (auto& thread : workers) thread.join();

    if (results.report_all_errors) return result.load();
    return result.load() & static_cast<unsigned>(HashStatus::MISMATCH);
}
