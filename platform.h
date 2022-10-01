#pragma once

#include <stddef.h>

// Get an extended file attribute from path. stores the size in size.
// Returns 0 if the attribute does not exist; <0 on error, >0 on success.
//
// Returns 0 on succes.
// Returns <0 if unexpected system error.
// Returns >0 if expected error.
int get_attr(const char* path, const char* name, void* value, size_t* size);

// Set the attribute on the file
//
// Returns 0 on success, >0 on expected error, <0 on unexpected system error.
int set_attr(const char* path, const char* name, const void* value, size_t size);

// Remove the attribute from the file.
//
// Returns 0 on success, >0 on expected error, <0 on unexpected error.
int remove_attr(const char* path, const char* name);

// Returns the flags to be used to open files. This can differ by platform
// depending on what open flags are supported.
int open_flags();
