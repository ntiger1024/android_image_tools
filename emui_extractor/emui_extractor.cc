#include <stdio.h>
#include <string.h>
#include <memory>
#include <string>
#include <vector>
#include "image.h"

using namespace std;

static string convert_size_to_str(uint32_t size) {
  char buf[16];
  const char *unit = "B";
  double sd = size;
  if (size > 1024) {
    sd /= 1024;
    size /= 1024;
    unit = "KB";
  }
  if (size > 1024) {
    sd /= 1024;
    size /= 1024;
    unit = "MB";
  }
  if (size > 1024) {
    sd /= 1024;
    size /= 1024;
    unit = "GB";
  }
  snprintf(buf, sizeof(buf), "%12.2lf %2s", sd, unit);
  return string(buf);
}

static int list_images(const char *in_file) {
  RoImageFile *image_file = RoImageFile::Create(in_file);
  if (!image_file->Load()) {
    fprintf(stderr, "Failed to load\n");
    return -1;
  }

  auto images = image_file->GetAllImages();
  unsigned size_len = 0;
  unsigned type_len = 0;
  for (const auto &img : images) {
    string size_str = convert_size_to_str(img->GetHdr()->data_len_);
    if (size_str.size() > size_len) {
      size_len = size_str.size();
    }
    unsigned len = strlen(reinterpret_cast<const char *>(img->GetHdr()->type_));
    if (len > type_len) {
      type_len = len;
    }
  }
  string line(8 + 1 + type_len + 4 + 1 + size_len + 1 + type_len + 1 + 8, '=');
  fprintf(stdout, "%s\n", line.c_str());
  fprintf(stdout, "%8s %*s.img %*s %*s %8s\n", "Sequence", type_len, "File",
          size_len, "Size", type_len, "Type", "Device");
  fprintf(stdout, "%s\n", line.c_str());
  for (const auto &img : images) {
    string size_str = convert_size_to_str(img->GetHdr()->data_len_);
    int i = sizeof(img->GetHdr()->hw_id_) - 1;
    while (i >= 0 && img->GetHdr()->hw_id_[i] == 0xff) {
      --i;
    }
    ++i;
    string device(reinterpret_cast<const char *>(&img->GetHdr()->hw_id_[0]), i);
    fprintf(stdout, "%08x %*s.img %*s %*s %8s\n", img->GetHdr()->sequence_,
            type_len, img->GetHdr()->type_, size_len, size_str.c_str(), type_len,
            img->GetHdr()->type_, device.c_str());
  }
  fprintf(stdout, "%s\n", line.c_str());
}

static int dump_image(const char *in_file, const char *img_name,
                      const char *out_file) {
  RoImageFile *image_file = RoImageFile::Create(in_file);
  if (!image_file->Load()) {
    fprintf(stderr, "Failed to load\n");
    return -1;
  }

  auto images = image_file->GetAllImages();
  for (const auto &img : images) {
    string type(reinterpret_cast<const char *>(img->GetHdr()->type_));
    type += ".img";

    if (out_file == string("all")) {
      img->Dump(type);
      continue;
    }

    if (type != img_name) {
      continue;
    }
    img->Dump(out_file);
    return 0;
  }
  if (out_file != string("all")) {
    fprintf(stderr, "err find %s\n", img_name);
    return -1;
  }
  return 0;
}

static const char *usage =
    "Usage: emui_extractor UPDATE.APP cmd\n"
    "  cmd:\n"
    "    list              - list all images in the UPDATE.APP\n"
    "    dump image output - dump one of image in the UPDATE.APP\n";

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[2], "list") == 0) {
    return list_images(argv[1]);
  } else if (argc == 5 && strcmp(argv[2], "dump") == 0) {
    return dump_image(argv[1], argv[3], argv[4]);
  } else {
    fprintf(stderr, "%s", usage);
    return -1;
  }
}
