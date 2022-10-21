// This file is part of Hasher.
//
// Foobar is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// Foobar is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// Foobar. If not, see <https://www.gnu.org/licenses/>.

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
