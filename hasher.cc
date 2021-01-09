#include <cstddef>
#include <cstdlib>
#include <unistd.h>
#include <openssl/evp.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <vector>
#include <algorithm>
#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#define DIE(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)
#define QUIT(...) do { printf(__VA_ARGS__); exit(EXIT_FAILURE); } while (0)

template <typename P, typename D>
std::unique_ptr<P, std::function<void(P*)>> MakeUnique(P* p, D deleter) {
    return std::unique_ptr<P, std::function<void(P*)>>(p, deleter);
}

std::pair<std::unique_ptr<char, std::function<void(char*)>>, size_t> Mmap(int fd) {
    struct stat sb;
    if (fstat(fd, &sb) < 0) DIE("fstat");
    const size_t file_len = sb.st_size;

    void* const mapped = mmap(nullptr, file_len, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) DIE("mmap");

    auto cleanup = [file_len](char* mapped) {
        if (munmap(mapped, file_len) < 0) DIE("munmap");
    };

    auto ptr = MakeUnique(static_cast<char*>(mapped), std::move(cleanup));

    return {std::move(ptr), file_len};
}

std::vector<unsigned char> HashFile(int fd, std::string_view hashname) {
    // First, mmap fd.
    const auto [file_mem, file_len] = Mmap(fd);

    auto* const md = EVP_get_digestbyname(hashname.data());
    if (md == nullptr) QUIT("Invalid digest name '%s'\n", hashname.data());
    auto mdctx = MakeUnique(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    EVP_DigestInit_ex(mdctx.get(), md, nullptr);
    EVP_DigestUpdate(mdctx.get(), file_mem.get(), file_len);

    unsigned char mdbuf[EVP_MAX_MD_SIZE];
    unsigned md_len;
    EVP_DigestFinal_ex(mdctx.get(), mdbuf, &md_len);

    return std::vector<unsigned char>(&mdbuf[0], &mdbuf[md_len]);
}

std::ostream& operator<<(std::ostream& os,
                         const std::vector<unsigned char>& v) {
    for (const auto byte : v) {
        char buf[3];
        snprintf(buf, 3, "%02x", byte);
        os << buf;
    }
    return os;
}

int main(int argc, char* argv[]) {
    auto* const file = fopen(argv[0], "r");
    auto hash = HashFile(fileno(file), "sha512");
    fclose(file);
    std::cout << argv[0] << ": " << hash << std::endl;
    return EXIT_SUCCESS;
}
