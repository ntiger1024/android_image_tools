#ifndef SCOPED_FD_
#define SCOPED_FD_

#include <unistd.h>

class ScopedFd {
 public:
  ScopedFd() : fd_(-1) {}
  ScopedFd(int fd) : fd_(fd) {}
  ~ScopedFd() { reset(); }

  int get() const { return fd_; }
  void reset(int fd = -1) {
    if (fd_ >= 0) {
      close(fd_);
    }
    fd_ = fd;
  }

  int release() {
    int local_fd = fd_;
    fd_ = -1;
    return local_fd;
  }

  operator bool() const {
    if (fd_ < 0) {
      return false;
    } else {
      return true;
    }
  }

 private:
  ScopedFd(const ScopedFd&) = delete;
  ScopedFd& operator=(const ScopedFd&) = delete;

 private:
  int fd_;
};
#endif  // SCOPED_FD_
