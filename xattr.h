#pragma once

#include <stddef.h>

// Get an extended file attribute from path. stores the size in size.
// Returns 0 if the attribute does not exist; <0 on error, >0 on success.
int get_attr(const char* path, const char* name, void* value, size_t* size);

// Set the attribute on the file. returns 0 if it lacks permissions. 1 if
// successful. <0 if error.
int set_attr(const char* path, const char* name, const void* value, size_t size);

int remove_attr(const char* path, const char* name);
