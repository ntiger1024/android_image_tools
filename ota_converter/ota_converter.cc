#include <brotli/decode.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/loop.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

const int kBlockSize = 4096;
const char kZeroBlock[kBlockSize] = {0};

////////////////// LOG //////////////////
enum {
  LOG_FATAL,
  LOG_ERROR,
  LOG_WARN,
  LOG_INFO,
  LOG_DEBUG,
  LOG_VERBOSE,
  LOG_DEFAULT = LOG_ERROR,
};

#define pr_msg(...) fprintf(stderr, __VA_ARGS__)
#define pr_err(...)               \
  do {                            \
    if (LOG_ERROR <= gLogLevel) { \
      pr_msg(__VA_ARGS__);        \
    }                             \
  } while (0)

#define pr_dbg(...)               \
  do {                            \
    if (LOG_DEBUG <= gLogLevel) { \
      pr_msg(__VA_ARGS__);        \
    }                             \
  } while (0)

#ifdef LOG_LEVEL
static int gLogLevel = LOG_LEVEL;
#else
static int gLogLevel = LOG_DEFAULT;
#endif
//////////////// END LOG //////////////////

static int erase(int fd, vector<int> *ranges) {
  for (size_t i = 0; i < ranges->size(); i += 2) {
    size_t begin = (*ranges)[i];
    size_t end = (*ranges)[i + 1];
    size_t blocks[2];
    blocks[0] = begin * kBlockSize;
    blocks[1] = (end - begin) * kBlockSize;
    if (ioctl(fd, BLKDISCARD, &blocks) == -1) {
      pr_err("BLKDISCARD ioctl failed: %s\n", strerror(errno));
      return -1;
    }
  }
  return 0;
}

static int zeroize(int fd, vector<int> *ranges) {
  for (size_t i = 0; i < ranges->size(); i += 2) {
    size_t begin = (*ranges)[i];
    size_t end = (*ranges)[i + 1];
    size_t offset = begin * kBlockSize;
    if (lseek64(fd, offset, SEEK_SET) == -1) {
      pr_err("Failed to seek to 0x%lx\n", offset);
      return -1;
    }
    for (size_t j = begin; j < end; ++j) {
      if (write(fd, kZeroBlock, sizeof(kZeroBlock)) != sizeof(kZeroBlock)) {
        pr_err("Failed to write block %ld\n", j);
        return -1;
      }
    }
  }
  return 0;
}

shared_ptr<vector<int>> parse_args(string &args) {
  string::size_type pos = 0;
  string::size_type next;
  shared_ptr<vector<int>> ret;

  if ((next = args.find(',', pos)) == string::npos) {
    pr_err("Too few args: %s\n", args.c_str());
    return ret;
  }
  int num = stoi(args.substr(pos, next));
  if (num == 0 || num % 2 == 1) {
    pr_err("Invalid num: %d\n", num);
    return ret;
  }
  // pr_dbg("num: %d\n", num);

  ret.reset(new vector<int>);
  pos = next + 1;
  for (auto i = 0; i < num; ++i) {
    next = args.find(',', pos);
    if (next == string::npos) {
      if (i == num - 1) {
        ret->push_back(stoi(args.substr(pos)));
      } else {
        pr_err("Not enough args: %ld\n", ret->size());
        return shared_ptr<vector<int>>();
      }
    } else {
      ret->push_back(stoi(args.substr(pos, next)));
      pos = next + 1;
    }
  }
  return ret;
}

int get_max_block(ifstream &ifs) {
  int max_block = -1;
  // auto saved_pos = ifs.tellg();
  string line;
  while (getline(ifs, line)) {
    stringstream ss(line);
    string cmd, args;
    ss >> cmd >> args;
    if (!ss) {
      pr_err("Failed to parse line: %s\n", line.c_str());
      goto out;
    }
    // pr_dbg("%s %s\n", cmd.c_str(), args.c_str());

    shared_ptr<vector<int>> ranges = parse_args(args);
    for (auto it = ranges->begin(); it != ranges->end(); ++it) {
      if (*it > max_block) {
        max_block = *it;
      }
    }
  }

out:
  // if (! ifs.seekg(saved_pos)) {
  //     pr_err("Can't restore position\n");
  //     max_block = -1;
  // }
  return max_block;
}

shared_ptr<string> create_image_loop(const char *image_fn, int blocks) {
  // Create image file
  int fd =
      open(image_fn, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (fd == -1) {
    pr_err("Failed to open image file: %s\n", image_fn);
    return shared_ptr<string>();
  }
  unsigned long size_in_bytes = (unsigned long)blocks * kBlockSize;
  if (ftruncate(fd, size_in_bytes) == -1) {
    pr_err("Failed to truncate file %ld\n", size_in_bytes);
    close(fd);
    return shared_ptr<string>();
  }

#ifndef SET_LOOP_DEV
  close(fd);
  return make_shared<string>(image_fn);

#else  // SET_LOOP_DEV
  // Attach to a loop device
  int lcfd = open("/dev/loop-control", O_RDONLY);
  if (lcfd == -1) {
    pr_err("Failed to open loop control: %s\n", strerror(errno));
    close(fd);
    return shared_ptr<string>();
  }
  long devnr = ioctl(lcfd, LOOP_CTL_GET_FREE);
  if (devnr == -1) {
    pr_err("Can't find free loop device: %s\n", strerror(errno));
    close(fd);
    close(lcfd);
    return shared_ptr<string>();
  }
  char loop_name[256];
  snprintf(loop_name, sizeof(loop_name), "/dev/loop%ld", devnr);
  int lfd = open(loop_name, O_RDWR);
  if (lfd == -1) {
    pr_err("Failed to open loop device: %s %s\n", loop_name, strerror(errno));
    close(fd);
    close(lcfd);
    return shared_ptr<string>();
  }
  if (ioctl(lfd, LOOP_SET_FD, fd) == -1) {
    pr_err("Can't set loop device: %s %s\n", loop_name, strerror(errno));
    close(fd);
    close(lcfd);
    close(lfd);
    return shared_ptr<string>();
  }

  close(fd);
  close(lcfd);
  close(lfd);
  return make_shared<string>(loop_name);
#endif
}

int detech_image_loop(const char *loop_dev) {
#ifdef SET_LOOP_DEV
  int lfd = open(loop_dev, O_RDWR);
  if (lfd == -1) {
    pr_err("Can't open loop device %s: %s\n", loop_dev, strerror(errno));
    return -1;
  }

  if (ioctl(lfd, LOOP_CLR_FD) == -1) {
    pr_err("Failed to detech loop device %s %s\n", loop_dev, strerror(errno));
    return -1;
  }
#endif

  return 0;
}

struct cookie {
  int dfd;
  uint8_t in_buf[kBlockSize];
  size_t in_available;
  const uint8_t *next_in;

  uint8_t out_buf[kBlockSize];
  size_t out_available;
  uint8_t *next_out;
};

int copy_data(struct cookie *cookie, int tfd, vector<int> *ranges,
              BrotliDecoderState *state) {
  for (size_t i = 0; i < ranges->size(); i += 2) {
    size_t begin = (*ranges)[i];
    size_t end = (*ranges)[i + 1];
    size_t size = (end - begin) * kBlockSize;
    // pr_dbg("copy_data %ld %ld\n", begin, end);

    if (lseek64(tfd, begin * kBlockSize, SEEK_SET) == -1) {
      pr_err("Can't seek to %ld\n", begin * kBlockSize);
      return -1;
    }

    while (1) {
      // For brotli compressed data.
      if (state) {
        if (cookie->out_available == 0) {
          int count;
          if ((count = write(tfd, cookie->out_buf, sizeof(cookie->out_buf))) !=
              sizeof(cookie->out_buf)) {
            pr_err("Can't write data %ld %ld %d/%d: %s\n", begin,
                   begin * kBlockSize, count, kBlockSize, strerror(errno));
            return -1;
          }
          cookie->next_out = cookie->out_buf;
          cookie->out_available = sizeof(cookie->out_buf);

          size -= sizeof(cookie->out_buf);
          if (size == 0) {
            break;
          }
        }
        if (cookie->in_available == 0) {
          cookie->in_available =
              read(cookie->dfd, cookie->in_buf, sizeof(cookie->in_buf));
          if (cookie->in_available < 0) {
            if (!BrotliDecoderHasMoreOutput(state)) {
              pr_err("Can't read data\n");
              return -1;
            } else {
              cookie->in_available = 0;
            }
          }
          cookie->next_in = cookie->in_buf;
        }
        BrotliDecoderResult result = BrotliDecoderDecompressStream(
            state, &cookie->in_available, &cookie->next_in,
            &cookie->out_available, &cookie->next_out, nullptr);
        if (result == BROTLI_DECODER_RESULT_ERROR) {
          pr_err("Decompression failed with %s\n",
                 BrotliDecoderErrorString(BrotliDecoderGetErrorCode(state)));
          return -1;
        }
      } else {
        // For uncompressed data.
        int count;
        if ((count = read(cookie->dfd, cookie->in_buf,
                          sizeof(cookie->in_buf))) < 0) {
          pr_err("Can't read data\n");
          return -1;
        }
        if (write(tfd, cookie->in_buf, count) != count) {
          pr_err("Can't write data %ld %ld %d/%d: %s\n", begin,
                 begin * kBlockSize, count, kBlockSize, strerror(errno));
          return -1;
        }
        size -= count;
        if (size == 0) {
          break;
        }
      }
    }
  }
  return 0;
}

int transfer(ifstream &ifs, const char *data_file, const char *target_dev) {
  int ret = -1;
  int fd = open(target_dev, O_WRONLY);
  if (fd == -1) {
    pr_err("Can't open %s for write\n", target_dev);
    return -1;
  }

  string filename(data_file);
  bool br_compressed = true;
  if (filename.find(".br", filename.size() - 3) == string::npos) {
    br_compressed = false;
  }
  int dfd = open(data_file, O_RDONLY);
  if (dfd == -1) {
    pr_err("Can't open %s for read\n", data_file);
    close(fd);
    return -1;
  }

  BrotliDecoderState *state = nullptr;
  struct cookie cookie;
  if (br_compressed) {
    state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    if (!state) {
      pr_err("Can't create brotli decoder\n");
      close(fd);
      close(dfd);
      return -1;
    }
  }
  cookie.dfd = dfd;
  cookie.in_available = 0;
  cookie.next_in = cookie.in_buf;
  cookie.out_available = sizeof(cookie.out_buf);
  cookie.next_out = cookie.out_buf;

  string line;
  string cmd, args;
  while (getline(ifs, line)) {
    stringstream ss(line);
    ss >> cmd >> args;
    if (!ss) {
      pr_err("Invalid line: %s\n", line.c_str());
      goto out;
    }
    auto ranges = parse_args(args);
    if (!ranges) {
      pr_err("Failed to parse args: %s\n", args.c_str());
      goto out;
    }

    if (strcmp(cmd.c_str(), "erase") == 0) {
      pr_dbg("erase %s\n", args.c_str());
    } else if (strcmp(cmd.c_str(), "zero") == 0) {
      pr_dbg("zero %s\n", args.c_str());
      if (zeroize(fd, ranges.get())) {
        pr_err("failed to zeroize\n");
        goto out;
      }
    } else if (strcmp(cmd.c_str(), "new") == 0) {
      pr_dbg("new %s\n", args.c_str());
      if (copy_data(&cookie, fd, ranges.get(), state)) {
        pr_err("failed to copy data\n");
        goto out;
      }
    } else {
      pr_err("Unsupported command: %s\n", cmd.c_str());
      goto out;
    }
  }

  ret = 0;

out:
  BrotliDecoderDestroyInstance(state);
  close(dfd);
  close(fd);
  return ret;
}

int main(int argc, char **argv) {
  int ret = 0;

  if (argc != 4) {
    pr_err("usage: %s transfer.list new.dat[.br] image_file\n", argv[0]);
    return 1;
  }

  ifstream ifs(argv[1]);
  if (!ifs) {
    pr_err("Failed to open %s\n", argv[1]);
    return 1;
  }

  // first line
  string version_str;
  if (!getline(ifs, version_str)) {
    pr_err("Failed to read version line\n");
    return 1;
  }
  int version = stoi(version_str);
  if (version != 3 && version != 4) {
    pr_err("Unsupported version: %d\n", version);
    return 1;
  }
  printf("Version: %d\n", version);

  // second line
  string blocks_str;
  if (!getline(ifs, blocks_str)) {
    pr_err("Failed to read block line\n");
    return 1;
  }
  int blocks = stoi(blocks_str);
  if (blocks <= 0) {
    pr_err("Invalid blocks: %d\n", blocks);
    return 1;
  }

  // ignore 3rd and 4th lines
  string unused;
  if (!getline(ifs, unused) || !getline(ifs, unused)) {
    pr_err("Failed to read 3rd and 4th lines\n");
    return 1;
  }

  int max_block = get_max_block(ifs);
  if (max_block < blocks) {
    pr_err("Invalid max block: %d\n", max_block);
    return 1;
  }
  printf("Max block: %d\n", max_block);
  // Restore state
  ifs.close();
  ifs.open(argv[1]);
  for (int i = 0; i < 4; ++i) {
    getline(ifs, unused);
  }
  if (!ifs) {
    pr_err("Can't restore file state\n");
    return 1;
  }

  // Create file with max block.
  shared_ptr<string> image_loop_dev = create_image_loop(argv[3], max_block);
  if (!image_loop_dev) {
    pr_err("Failed to create image loop device\n");
    return 1;
  }
  printf("Create image loop device %s\n", image_loop_dev->c_str());

  // Transfer data.
  if (transfer(ifs, argv[2], image_loop_dev->c_str()) == -1) {
    pr_err("Failed to transfer data\n");
    ret = 1;
    goto out;
  }

out:
  // Detech loop device
  if (detech_image_loop(image_loop_dev->c_str()) == -1) {
    pr_err("Failed to detech loop device: %s\n", image_loop_dev->c_str());
    ret = 1;
  } else {
    printf("Deteched image loop device %s\n", image_loop_dev->c_str());
  }

  return ret;
}
