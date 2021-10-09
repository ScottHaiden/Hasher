#pragma once

#include <string>

class FnameIterator {
  public:
    static std::unique_ptr<FnameIterator> GetInstance(bool recurse,
            char** args);
    virtual ~FnameIterator() = default;
    virtual std::string GetNext() = 0;
    virtual void Start() = 0;
};

