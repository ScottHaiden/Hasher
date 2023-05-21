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
#include <iostream>
#include <libgen.h>
#include <mutex>
#include <numeric>
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

enum class HashStatus : unsigned {
    OK = 0,
    MISMATCH = (1 << 0),
    ERROR = (1 << 1),
};

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
    auto contents = file->Load();
    for (auto hashname : hashnames) {
        if (file->GetHashMetadata(hashname)) {
            WriteLocked(stderr, "Skipping %s (already has %s hash)\n",
                                std::string(fname).c_str(),
                                std::string(hashname).c_str());
            ret = HashStatusMax(ret, HashStatus::ERROR);
            continue;
        }
        auto fresh = contents->HashContents(hashname);
        const auto result = file->SetHashMetadata(hashname, fresh);
        if (file->SetHashMetadata(hashname, fresh) != HashResult::OK) {
            WriteLocked(stderr, "Failed to write xattr to %s\n",
                                std::string(fname).c_str());
            ret = HashStatusMax(ret, HashStatus::ERROR);
        }
        if (print_name) {
            WriteLocked(stdout, "%s [%10s] %s\n",
                                HashToString(fresh).c_str(),
                                std::string(hashname).c_str(),
                                std::string(fname).c_str());
        } else {
            WriteLocked(stdout, "%s  %s\n",
                                HashToString(fresh).c_str(),
                                std::string(fname).c_str());
        }
    }
    return ret;
}

HashStatus HasHash(std::string_view fname, const HashList& hashnames) {
    const bool print_name = hashnames.size() > 1;
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
    const bool print_name = hashnames.size() > 1;
    auto file = File::Create(fname);

    if (!file->is_accessible(false)) {
        WriteLocked(stderr, "Skipping %s (insufficient permissions)\n",
                            std::string(fname).c_str());
        return HashStatus::ERROR;
    }

    std::optional<std::unique_ptr<MappedFile>> mapped;
    HashStatus ret = HashStatus::OK;
    for (const auto hashname : hashnames) {
        const auto from_attr = file->GetHashMetadata(hashname);
        if (!from_attr) {
            WriteLocked(stdout, "skipping %s (missing %s hash)\n",
                                std::string(fname).c_str(),
                                std::string(hashname).c_str());
            ret = HashStatusMax(ret, HashStatus::ERROR);
            continue;
        }
        if (!mapped.has_value()) mapped = file->Load();
        const auto from_contents = mapped->get()->HashContents(hashname);

        if (*from_attr != from_contents) {
            WriteLocked(stdout, "%s: %s FAILED\n",
                                std::string(fname).c_str(),
                                std::string(hashname).c_str());
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
    printf("%s [-c] [-h] [-r] [-s] [-p] [-t NUM] [-T] [-e] [-E] [-C hashname] "
           "[-R] filenames...\n", progname);
    printf("\n");
    printf("\t-C NAME: Set hashing function to NAME. (default=sha512)\n");
    printf("\t-E:      Only report error if a file has a bad hash\n");
    printf("\t-H:      Identify whether files have hashes\n");
    printf("\t-R:      Operate recursively over directories.\n");
    printf("\t-T:      Use one worker thread per CPU\n");
    printf("\t-c:      Check hashes\n");
    printf("\t-e:      Report all errors (even missing data errors)\n");
    printf("\t-h:      Show this help\n");
    printf("\t-p:      Print hash (create a checksum file)\n");
    printf("\t-r:      Reset hashes (remove hash from file's metadata)\n");
    printf("\t-s:      Set hash (Find file's hash and set it in files metadata)\n");
    printf("\t-t NUM:  Use NUM threads\n");
}

ArgResults ParseArgs(int argc, char* const* argv) {
    const std::vector<std::string_view> kDefaultHashes{"sha512", "blake2b512"};
    ArgResults ret = {
        .fn = nullptr,
        .num_threads = 1,
        .index = 0,
        .report_all_errors = false,
        .hash_fns = {},
        .recurse = false,
    };

    while (true) {
        switch (getopt(argc, argv, "chrspt:TeEC:RH")) {
            case 'T': ret.num_threads = 0;            continue;
            case 'c': ret.fn = &CheckHash;            continue;
            case 'p': ret.fn = &PrintHash;            continue;
            case 'r': ret.fn = &ResetHash;            continue;
            case 's': ret.fn = &ApplyHash;            continue;
            case 'H': ret.fn = &HasHash;              continue;
            case 'R': ret.recurse = true;             continue;
            case 'C': ret.hash_fns.push_back(optarg); continue;
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
    if (ret.hash_fns.empty()) ret.hash_fns = kDefaultHashes;
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
