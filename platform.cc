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

#include "platform.h"

#if defined(__FreeBSD__)

#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/extattr.h>

int get_attr(const char* path, const char* name,
                 void* value, size_t* size) {
    const ssize_t ret =
        extattr_get_file(path, EXTATTR_NAMESPACE_USER, name, value, *size);
    if (ret >= 0) {
        *size = ret;
        return 0;
    }
    if (errno == ENOATTR) {
        *size = 0;
        return 1;
    }
    return -1;
}

int set_attr(const char* path, const char* name,
             const void* value, size_t size) {
    const int ret =
        extattr_set_file(path, EXTATTR_NAMESPACE_USER, name, value, size);
    if (ret >= 0) return 0;
    if (errno == EACCES) return 1;
    return -1;
}

int remove_attr(const char* path, const char* name) {
    const int ret = extattr_delete_file(path, EXTATTR_NAMESPACE_USER, name);
    if (ret == 0) return 0;
    if (errno == ENOATTR) return 0;
    if (errno == EACCES) return 0;
    return -1;
}

int open_flags(const char* path) { return O_RDONLY; }

#elif defined(__linux__)

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include "common.h"

int get_attr(const char* path, const char* name, void* value, size_t* size) {
    LOCAL_STRING(propname, "user.%s", name);
    const int ret = getxattr(path, propname, value, *size);
    if (ret >= 0) {
        *size = ret;
        return 0;
    }
    if (errno == ENODATA) {
        *size = 0;
        return 1;
    }
    return -1;
}

int set_attr(const char* path, const char* name, const void* value, size_t size) {
    LOCAL_STRING(propname, "user.%s", name);
    const int ret = setxattr(path, propname, value, size, 0);
    if (ret == 0) return 0;
    if (errno == EACCES) return 1;
    return -1;
}

int remove_attr(const char* path, const char* name) {
    LOCAL_STRING(propname, "user.%s", name);
    const int ret = removexattr(path, propname);
    if (ret == 0) return 0;
    if (errno == ENODATA) return 0;
    if (errno == EACCES) return 0;
    return -1;
}

int open_flags(const char* path) {
    const uid_t self = geteuid();
    struct stat buf;
    if (stat(path, &buf)) QUIT("Failed to stat %s\n", path);
    if (self == buf.st_uid) return O_RDONLY | O_NOATIME;
    return O_RDONLY;
}

#else
#  error "Not compiling on a known OS."
#endif
