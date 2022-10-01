#include "xattr.h"

#if defined(__FreeBSD__)

#include <sys/types.h>
#include <sys/extattr.h>
#include <errno.h>

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

#elif defined(__linux__)

#include <sys/xattr.h>
#include <errno.h>

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

#else
#  error "Not compiling on a known OS."
#endif
