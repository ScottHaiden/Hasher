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

#pragma once

#include <string>
#include <memory>

std::string HashToString(const std::vector<uint8_t>& bytes);

class FnameIterator {
  public:
    static std::unique_ptr<FnameIterator> GetInstance(bool recurse,
            char** args);
    virtual ~FnameIterator();
    virtual std::string GetNext() = 0;
    virtual void Start() = 0;
};

