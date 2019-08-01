#ifndef EMUI_EXTRACTOR_IMAGEHDR_H_
#define EMUI_EXTRACTOR_IMAGEHDR_H_

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdint>
#include <vector>
#include <memory>

#ifdef DEBUG
  #define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
  #define DBG
#endif

class RoImageFile;

class Image {
  friend RoImageFile;
 public:
  static const uint32_t kMagic = 0xA55AAA55;

  struct ImageHdr {
    uint8_t magic_[4];
    uint32_t hdr_len_;
    uint32_t unused1_;
    uint8_t hw_id_[8];
    uint32_t sequence_;
    uint32_t data_len_;
    uint8_t date_[16];
    uint8_t time_[16];
    uint8_t type_[32];
    uint16_t hdr_checksum_;
    uint16_t unused3_;
    uint16_t unused4_;
    uint8_t data_checksum_[0];
  };

  // Image() : hdr_(nullptr), data_off_(0) {}
  Image(std::shared_ptr<ImageHdr> hdr, RoImageFile *ifp, off_t hdr_off)
      : hdr_(hdr), image_file_(ifp), data_off_(hdr->hdr_len_ + hdr_off) {}

  const std::shared_ptr<ImageHdr> GetImageHdr() const {
    return hdr_;
  }

  bool Dump(const std::string &out_fn) const;
  std::shared_ptr<ImageHdr> GetHdr() const {
    return hdr_;
  }

 private:
  Image(const Image &) = delete;
  Image &operator=(const Image &) = delete;

  std::shared_ptr<ImageHdr> hdr_;
  RoImageFile *image_file_;
  off_t data_off_;
};

class RoImageFile {
  friend class Image;
 public:
  static RoImageFile *Create(const char *file_name) {
    DBG("Create RoImageFile\n");
    static RoImageFile instance(file_name);
    return &instance;
  }

  const std::vector<std::shared_ptr<Image>> &GetAllImages() const {
    return images_;
  }

  bool Load();

  ~RoImageFile() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

 private:
  const unsigned kGoodBit = 0;
  const unsigned kBadBit = 1;
  const unsigned kEofBit = 2;

  RoImageFile(const char *file_name);

  // Do not allow copy
  RoImageFile(const RoImageFile &) = delete;
  RoImageFile &operator=(const RoImageFile &) = delete;

  explicit operator bool() const {
    return Good();
  }

  RoImageFile &Read(uint8_t *buf, size_t buf_sz);
  RoImageFile &Skip(off_t sz);
  off_t GetPosition() const { return position_; }
  RoImageFile &SetPosition(off_t new_pos) {
    position_ = new_pos <= size_ ? new_pos : size_;
    if (lseek(fd_, position_, SEEK_SET) == -1) {
      state_ |= kBadBit;
    }
    if ((state_ & kEofBit) == kEofBit && position_ < size_) {
      state_ &= ~kEofBit;
    }
    return *this;
  }

  RoImageFile &SkipAlign() {
    off_t pos = GetPosition();
    off_t pad = (pos + 3) / 4 * 4 - pos;
    return Skip(pad);
  }

  bool Good() const { return state_ == kGoodBit; }
  bool Bad() const { return (state_ & kBadBit) != 0; }
  bool Eof() const { return (state_ & kEofBit) != 0; }


  int fd_ = -1;
  off_t position_ = 0;
  off_t size_ = 0;
  unsigned state_ = kBadBit;
  const char *file_name_;  // For debug
  std::vector<std::shared_ptr<Image>> images_;
};
#endif  // EMUI_EXTRACTOR_IMAGEHDR_H_
