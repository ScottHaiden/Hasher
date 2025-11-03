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

#include <atomic>
#include <algorithm>
#include <iostream>
#include <libgen.h>
#include <mutex>
#include <numeric>
#include <span>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "common.h"
#include "utils.h"
#include "file.h"
#include "platform.h"

namespace {
const std::vector<std::string_view> DefaultHashes() {
    constexpr static char kSha512[] = "sha512";
    constexpr static char kBlake2b512[] = "blake2b512";
    constexpr static char kSha3_512[] = "sha3-512";
    return {kBlake2b512, kSha3_512, kSha512};
}

enum class HashStatus : unsigned {
    OK = 0,
    MISMATCH = (1 << 0),
    ERROR = (1 << 1),
};

int ParseInt(std::string_view arg) {
    const std::string str(arg);
    char* endptr = nullptr;
    const long ret = strtoll(str.c_str(), &endptr, 0);

    bool good = true;
    good &= endptr != str.c_str();
    good &= *endptr == '\0';
    good &= ret > 0;
    good &= ret <= std::numeric_limits<int>::max();
    if (!good) QUIT("Invalid argument: %s\n", str.c_str());

    return ret;
}

unsigned HashStatusToUnsigned(HashStatus a) {
    switch (a) {
        case HashStatus::OK: return 0;
        case HashStatus::MISMATCH: return 1;
        case HashStatus::ERROR: return 2;
    }
    QUIT("Unhandled case in %s (%u)\n", __func__, static_cast<unsigned>(a));
}

HashStatus HashStatusMax(HashStatus a, HashStatus b) {
    const unsigned a_int = static_cast<unsigned>(a);
    const unsigned b_int = static_cast<unsigned>(b);
    if (a_int >= b_int) return a;
    return b;
}

using HashList = std::vector<std::string_view>;

void Worker(
        FnameIterator* iterator,
        const HashList& hashnames,
        const std::function<HashStatus(std::string_view,
                                       const HashList&)>& task,
        std::atomic<unsigned>* ret) {
    while (true) {
        const std::string cur = iterator->GetNext();
        if (cur.empty()) break;
        *ret |= HashStatusToUnsigned(task(cur, hashnames));
    }
}

HashStatus ApplyHash(std::string_view fname, const HashList& hashnames) {
    const bool print_name = hashnames.size() > 1;
    auto file = File::Create(fname);
    if (!file->is_accessible(true)) {
        WriteLocked(stderr, "Skipping %s (insufficient permissions)\n",
                std::string(fname).c_str());
        return HashStatus::ERROR;
    }

    HashStatus ret = HashStatus::OK;
    std::unique_ptr<OpenFile> contents = file->Open();
    if (!contents) {
        WriteLocked(stderr, "Skipping %s (failed to open)\n",
                std::string(fname).c_str());
        return HashStatus::ERROR;
    }

    const HashList unknowns([&]() {
        HashList ret;
        for (const auto& name : hashnames) {
            if (file->GetHashMetadata(name)) {
                WriteLocked(stderr, "Skipping %s for %s (already has hash)\n",
                            std::string(fname).c_str(),
                            std::string(name).c_str());
                continue;
            }
            ret.push_back(name);
        }
        return ret;
    }());

    if (unknowns.empty()) return ret;

    const auto hashes = contents->HashContents(std::span{unknowns});
    for (auto& [hashname, value] : hashes) {
        if (file->SetHashMetadata(hashname, value) != HashResult::OK) {
            WriteLocked(stderr, "Failed to write xattr to %s\n",
                                std::string(fname).c_str());
            ret = HashStatusMax(ret, HashStatus::ERROR);
        }
        if (print_name) {
            WriteLocked(stdout, "%s [%10s] %s\n",
                                HashToString(value).c_str(),
                                std::string(hashname).c_str(),
                                std::string(fname).c_str());
        } else {
            WriteLocked(stdout, "%s  %s\n",
                                HashToString(value).c_str(),
                                std::string(fname).c_str());
        }
    }
    return ret;
}

HashStatus HasHash(std::string_view fname, const HashList& hashnames) {
    auto file = File::Create(fname);

    HashStatus ret = HashStatus::OK;
    for (const auto hashname : hashnames) {
        if (file->GetHashMetadata(hashname)) continue;
        ret = HashStatusMax(ret, HashStatus::MISMATCH);
    }

    if (ret == HashStatus::MISMATCH) {
        WriteLocked(stdout, "%s\n", std::string(fname).c_str());
    }
    return ret;
}

HashStatus CheckHash(std::string_view fname, const HashList& hashnames) {
    auto file = File::Create(fname);

    if (!file->is_accessible(false)) {
        WriteLocked(stderr, "Skipping %s (insufficient permissions)\n",
                            std::string(fname).c_str());
        return HashStatus::ERROR;
    }

    HashStatus ret = HashStatus::OK;
    std::unordered_map<std::string, std::vector<uint8_t>> extant_hashes;
    for (const auto& hashname : hashnames) {
        auto extant = file->GetHashMetadata(hashname);
        if (extant) {
            extant_hashes[std::string(hashname)] = std::move(extant).value();
            continue;
        }
        WriteLocked(stdout, "Skipping %s (missing %s hash)\n",
                    std::string(fname).c_str(),
                    std::string(hashname).c_str());
        ret = HashStatusMax(ret, HashStatus::ERROR);
    }
    if (extant_hashes.empty()) return ret;

    std::vector<std::string_view> extant_hashnames;
    for (const auto& [key, val] : extant_hashes) {
        extant_hashnames.push_back(key);
    }

    std::unique_ptr<OpenFile> opened = file->Open();
    if (!opened) {
        WriteLocked(stderr, "Failed to open %s when we thought we could.\n",
                            std::string(fname).c_str());
        return HashStatus::ERROR;
    }
    std::unordered_map<std::string, std::vector<uint8_t>> actual_hashes =
        opened->HashContents(std::span(extant_hashnames));
    for (const auto& hashname : extant_hashnames) {
        const std::string hashname_str(hashname);
        const auto& expected = extant_hashes[hashname_str];
        const auto& actual = actual_hashes[hashname_str];
        if (actual != expected) {
            WriteLocked(stdout, "%s: %s FAILED\n",
                                std::string(fname).c_str(),
                                hashname_str.c_str());
            ret = HashStatusMax(ret, HashStatus::MISMATCH);
            continue;
        }
        WriteLocked(stdout, "%s: %s OK\n", std::string(fname).c_str(),
                                           std::string(hashname).c_str());
    }

    return ret;
}

HashStatus PrintHash(std::string_view fname, const HashList& hashnames) {
    const bool print_name = hashnames.size() > 1;

    auto file = File::Create(fname);
    if (!file->is_accessible(false)) {
        WriteLocked(stderr, "Skipping %s (insufficient permissions)\n",
                            std::string(fname).c_str());
        return HashStatus::ERROR;
    }
    HashStatus ret = HashStatus::OK;
    for (const auto hashname : hashnames) {
        const auto hash = file->GetHashMetadata(hashname);
        if (!hash) {
            ret = HashStatusMax(ret, HashStatus::ERROR);
            continue;
        }
        if (print_name) {
            WriteLocked(stdout, "%s [%10s] %s\n",
                                HashToString(*hash).c_str(),
                                std::string(hashname).c_str(),
                                std::string(fname).c_str());
        } else {
            WriteLocked(stdout, "%s  %s\n",
                                HashToString(*hash).c_str(),
                                std::string(fname).c_str());
        }
    }
    return ret;
}

HashStatus ResetHash(std::string_view fname, const HashList& hashnames) {
    auto file = File::Create(fname);
    if (!file->is_accessible(true)) {
        WriteLocked(stderr, "Skipping %s (insufficient permissions)\n",
                            std::string(fname).c_str());
        return HashStatus::ERROR;
    }
    HashStatus ret = HashStatus::OK;
    for (const auto hashname :hashnames) {
        if (file->RemoveHashMetadata(hashname) != HashResult::OK) {
            WriteLocked(stderr, "Failed to reset %s hash on %s\n",
                    std::string(hashname).c_str(),
                    std::string(fname).c_str());
            ret = HashStatusMax(ret, HashStatus::ERROR);
        }
        WriteLocked(stdout, "Resetting %s hash on %s\n",
                            std::string(hashname).c_str(),
                            std::string(fname).c_str());
    }
    return ret;
}

struct ArgResults {
    HashStatus (*fn)(std::string_view, const HashList&);
    int num_threads;
    int index;
    bool report_all_errors;
    std::vector<std::string_view> hash_fns;
    bool recurse;
};

void ShowHelp(char* progname) {
    const std::string default_hashes([]() -> std::string {
        const auto hashes = DefaultHashes();
        if (hashes.empty()) return "";

        std::string ret(hashes[0]);
        for (int i = 1; i < hashes.size(); ++i) {
            ret += "," + std::string(hashes[i]);
        }
        return ret;
    }());

    printf("%s [-c] [-p] [-r] [-s] [-H] [-T] [-t NUM] [-C hashname] [-R] [-e] [-E] [-h] filenames...\n", progname);
    printf("\n");
    printf("  Task switches:\n");
    printf("    -c:      Check hashes\n");
    printf("    -p:      Print hash (create a checksum file)\n");
    printf("    -r:      Reset hashes (remove hash from file's metadata)\n");
    printf("    -s:      Set hash (Find file's hash and set it in files metadata)\n");
    printf("    -H:      Identify whether files have hashes\n");
    printf("    -T:      Use one worker thread per CPU\n");
    printf("    -t NUM:  Use NUM threads\n");
    printf("\n");
    printf("  Hash control switches:\n");
    printf("    -C NAME: Set hashing function to NAME. (default=%s)\n", default_hashes.c_str());
    printf("\n");
    printf("  File handling switches:\n");
    printf("    -R:      Operate recursively over directories.\n");
    printf("\n");
    printf("  Error reporting switches:\n");
    printf("    -E:      Only report error if a file has a bad hash\n");
    printf("    -e:      Report all errors (even missing data errors)\n");
    printf("\n");
    printf("  Misc. Switches:\n");
    printf("    -h:      Show this help\n");
}

ArgResults ParseArgs(int argc, char* const* argv) {
    ArgResults ret = {
        .fn = nullptr,
        .num_threads = 1,
        .index = 0,
        .report_all_errors = false,
        .hash_fns = {},
        .recurse = false,
    };

    while (true) {
        switch (getopt(argc, argv, "cprsHt:TRC:eEh")) {
            // Control the job.
            case 'c': ret.fn = &CheckHash; continue;
            case 'p': ret.fn = &PrintHash; continue;
            case 'r': ret.fn = &ResetHash; continue;
            case 's': ret.fn = &ApplyHash; continue;
            case 'H': ret.fn = &HasHash;   continue;

            // Control the threads.
            case 'T': ret.num_threads = -1;               continue;
            case 't': ret.num_threads = ParseInt(optarg); continue;

            // Control what hashes we work with.
            case 'C': ret.hash_fns.push_back(optarg); continue;

            // Recursion
            case 'R': ret.recurse = true; continue;

            // Error reporting.
            case 'e': ret.report_all_errors = true;  continue;
            case 'E': ret.report_all_errors = false; continue;

            // Request help.
            case 'h': ShowHelp(argv[0]); exit(0); break;

            case -1: break;
            default: exit(1);
        }
        break;
    }
    ret.index = optind;
    if (ret.hash_fns.empty()) ret.hash_fns = DefaultHashes();
    if (ret.num_threads <= 0) ret.num_threads = sysconf(_SC_NPROCESSORS_ONLN);
    if (ret.fn) return ret;

    char* const fname = basename(*argv);
    if (!strcmp(fname, "hasher")) ret.fn = &ApplyHash;
    if (!strcmp(fname, "checker")) ret.fn = &CheckHash;

    return ret;
}
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

    for (unsigned i = 1; i < results.num_threads; ++i) {
        workers.emplace_back(&Worker,
                             iterator.get(),
                             results.hash_fns,
                             results.fn,
                             &result);
    }
    Worker(iterator.get(), results.hash_fns, results.fn, &result);

    for (auto& thread : workers) thread.join();

    if (results.report_all_errors) return result.load();
    return result.load() & static_cast<unsigned>(HashStatus::MISMATCH);
}
