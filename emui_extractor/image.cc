#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include "image.h"

using namespace std;

bool Image::Dump(const string &out_fn) const {
  int fd = open(out_fn.c_str(), O_WRONLY | O_CREAT,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
  if (fd == -1) {
    fprintf(stderr, "error open %s: %s\n", out_fn.c_str(), strerror(errno));
    return false;
  }

  DBG("dumping %s size: %d off: 0x%lx\n", hdr_->type_, hdr_->data_len_,
      data_off_);

  if (image_file_->SetPosition(data_off_)) {
    char buf[4096];
    uint32_t total = hdr_->data_len_;
    while (total) {
      // DBG("0x%x\n", total);
      uint32_t to_read = total <= sizeof(buf) ? total : sizeof(buf);
      if (image_file_->Read(reinterpret_cast<uint8_t*>(buf), to_read)) {
        if (write(fd, buf, to_read) != to_read) {
          fprintf(stderr, "error write %s: %s\n", out_fn.c_str(), strerror(errno));
          close(fd);
          return false;
        }
        total -= to_read;
      } else {
        close(fd);
        return false;
      }
    }
    close(fd);
    return true;
  }
  close(fd);
  return false;
}

RoImageFile::RoImageFile(const char *file_name) : file_name_(file_name) {
  fd_ = open(file_name, O_RDONLY);
  if (fd_ == -1) {
    fprintf(stderr, "error open %s: %s\n", file_name, strerror(errno));
    return;
  }
  struct stat buf;
  if (fstat(fd_, &buf) == -1) {
    fprintf(stderr, "error stat %s: %s\n", file_name, strerror(errno));
    return;
  }
  state_ = kGoodBit;
  size_ = buf.st_size;
}

bool RoImageFile::Load() {
  DBG("Begin load images...\n");
  uint32_t magic, hdr_len;
  while (Read(reinterpret_cast<uint8_t *>(&magic), sizeof(magic))) {
    DBG("Now position: 0x%lx\n", GetPosition());
    if (magic == Image::kMagic) {
      if (Read(reinterpret_cast<uint8_t *>(&hdr_len), sizeof(hdr_len))) {
        Skip(-(sizeof(magic) + sizeof(hdr_len)));
        off_t pos = GetPosition();
        // shared_ptr<Image::ImageHdr> hdr(
        //     reinterpret_cast<Image::ImageHdr *>(new uint8_t[hdr_len]),
        //     [](Image::ImageHdr *p) { delete[] reinterpret_cast<uint8_t*>(p);
        //     });
        shared_ptr<Image::ImageHdr> hdr(
            reinterpret_cast<Image::ImageHdr *>(malloc(hdr_len)), free);
        if (Read(reinterpret_cast<uint8_t *>(hdr.get()), hdr_len)) {
          shared_ptr<Image> image = make_shared<Image>(hdr, this, pos);
          images_.push_back(image);

          Skip(hdr->data_len_);
          SkipAlign();
          DBG("Got new header %s\n", hdr->type_);
          continue;
        }
      }
    }
  }
  DBG("End load images\n");
  return Eof();
}

RoImageFile &RoImageFile::Read(uint8_t *buf, size_t buf_sz) {
  if (Good()) {
    ssize_t count = read(fd_, buf, buf_sz);
    if (count == -1) {
      fprintf(stderr, "error read %s: %s\n", file_name_, strerror(errno));
      state_ |= kBadBit;
    } else if (count < static_cast<ssize_t>(buf_sz)) {
      // fprintf(stderr, "error read %s: EOF\n", file_name_);
      position_ += count;
      state_ |= kEofBit;
    } else {
      position_ += count;
    }
  } else {
    DBG("Bad state, not read\n");
  }
  return *this;
}

RoImageFile &RoImageFile::Skip(off_t sz) {
  position_ += sz;
  if (position_ > size_) {
    position_ = size_;
  } else if (position_ < 0) {
    position_ = 0;
  }
  if (lseek(fd_, position_, SEEK_SET) == -1) {
    state_ |= kBadBit;
  }
  if ((state_ | kEofBit) != 0 && position_ < size_) {
    state_ &= ~kEofBit;
  }
  return *this;
}
