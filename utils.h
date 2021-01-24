#pragma once

#include <stdio.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class FnameIterator {
  public:
    virtual ~FnameIterator() = default;
    virtual std::string GetNext() = 0;
};

class AtomicFnameIterator : public FnameIterator {
  public:
    ~AtomicFnameIterator() override;
    AtomicFnameIterator(char** first);

    std::string GetNext() override;
    
  private:
    std::atomic<char**> cur_;
};

class SocketFnameIterator : public FnameIterator {
  public:
    ~SocketFnameIterator() override;
    SocketFnameIterator();
    std::string GetNext() override;
    void Recurse();

  private:
    int rfd_;
    int wfd_;
    std::thread thread_;
};
