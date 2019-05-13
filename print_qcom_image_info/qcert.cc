#include <fcntl.h>
#include <getopt.h>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>
#include "scoped_fd.h"
#include "sys/elf32.h"
#include "sys/elf64.h"

#ifdef DEBUG
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DBG
#endif

#define Elf

using namespace std;

const Elf64_Xword kElfPhdrTypeMask = 7 << 24;
const Elf64_Xword kElfPhdrTypeHash = 2 << 24;
const int kHashSegHeaderSz = 40;
const int kHashSegHeaderSzV6 = 48;
const int kMaxCerts = 3;
const int kKeyChainSize = 0x1800;
const int kMbnV6 = 6;
const int kSha1Sz = 20;

struct mi_boot_image_header_type {
  uint32_t res1;            /* Reserved for compatibility: was image_id */
  uint32_t version;         /* Reserved for compatibility: was header_vsn_num */
  uint32_t res3;            /* Reserved for compatibility: was image_src */
  uint32_t res4;            /* Reserved for compatibility: was image_dest_ptr */
  uint32_t image_size;      /* Size of complete hash segment in bytes */
  uint32_t code_size;       /* Size of hash table in bytes */
  uint32_t res5;            /* Reserved for compatibility: was signature_ptr */
  uint32_t signature_size;  /* Size of the attestation signature in bytes */
  uint32_t res6;            /* Reserved for compatibility: was cert_chain_ptr */
  uint32_t cert_chain_size; /* Size of the attestation chain in bytes */
};

struct mi_boot_image_header_type_v6 {
  struct mi_boot_image_header_type base;
  uint32_t qti_md_size;
  uint32_t md_size;
};

struct metadata_base {
  uint32_t major;
  uint32_t minor;
};

struct metadata_0_0 {
  metadata_base base;
  uint32_t sw_id;
  uint32_t hw_id;
  uint32_t oem_id;
  uint32_t model_id;
  uint32_t app_id;
  uint32_t rot_en : 1;
  uint32_t in_use_soc_hw_version : 1;
  uint32_t use_serial_number_in_signing : 1;
  uint32_t oem_id_independent : 1;
  uint32_t root_revoke_activate_enable : 2;
  uint32_t uie_key_switch_enable : 2;
  uint32_t debug : 2;
  uint32_t soc_vers[12];
  uint32_t multi_serial_numbers[8];
  uint32_t mrc_index;
  uint32_t anti_rollback_version;
};

static char byte2char(int b) {
  if (0 <= b && b <= 9) {
    return b + '0';
  } else {
    return b - 10 + 'A';
  }
}
static string hex_encode(const char *bin, int len) {
  string hex;
  int i;
  for (i=0; i<len; ++i) {
    int hi = (bin[i] >> 4) & 0xf;
    int lo = bin[i] & 0xf;
    hex.push_back(byte2char(hi));
    hex.push_back(byte2char(lo));
  }
  return hex;
}

static string parse_cert_sn(X509 *cert) {
  string sn;

  ASN1_INTEGER *serial = X509_get_serialNumber(cert);
  BIGNUM *bn = ASN1_INTEGER_to_BN(serial, NULL);
  if (!bn) {
    fprintf(stderr, "unable to convert ASN1INTEGER to BN\n");
    return sn;
  }

  char *tmp = BN_bn2dec(bn);
  if (!tmp) {
    fprintf(stderr, "unable to convert BN to decimal string.\n");
    BN_free(bn);
    return sn;
  }

  sn = tmp;
  BN_free(bn);
  OPENSSL_free(tmp);
  return sn;
}

static int print_certs(shared_ptr<char> data, uint32_t sz, const char *fn=NULL) {
  const unsigned char *p = reinterpret_cast<const unsigned char *>(data.get());
  const unsigned char *end = p + sz;
  int i = 0;
  int nr_certs;

  if (sz == kKeyChainSize) {
    nr_certs = kMaxCerts;
  } else if (sz == kKeyChainSize * 2) {
    nr_certs = kMaxCerts * 2;
  } else {
    fprintf(stderr, "Invalid cert size %d\n", sz);
    return -1;
  }
  while (p < end && i < nr_certs) {
    ++i;
    if (i == kMaxCerts + 1) {
      p = end - kKeyChainSize;
      sz = kKeyChainSize;
      // TODO: Skip signature. How to know size of signature.
      p += 256;
    }
    const unsigned char *s = p;
    unique_ptr<X509, void (*)(X509 *)> cert(d2i_X509(NULL, &p, sz), X509_free);
    if (!cert) {
      fprintf(stderr, "Unable to parse cert\n");
      return -1;
    }
    sz = end - p;

    if (fn) {
      string fni = fn + to_string(i) + ".cert";
      DBG("write cert to %s\n", fni.c_str());
      int fd = open(fni.c_str(), O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
      if (fd == -1) {
        perror("open");
        return -1;
      }
      if (write(fd, s, p - s) != p - s) {
        perror("write");
        close(fd);
        return -1;
      }
      close(fd);
      continue;
    }

    char *subj = X509_NAME_oneline(X509_get_subject_name(cert.get()), NULL, 0);
    if (!subj) {
      fprintf(stderr, "Unable to get subject\n");
      return -1;
    }
    char *issuer = X509_NAME_oneline(X509_get_issuer_name(cert.get()), NULL, 0);
    if (!issuer) {
      fprintf(stderr, "Unable to get subject\n");
      OPENSSL_free(subj);
      return -1;
    }
    const EVP_MD *digest = EVP_sha1();
    unsigned len;
    char buf[kSha1Sz];
    int rc = X509_digest(cert.get(), digest, (unsigned char*) buf, &len);
    if (rc == 0 || len != kSha1Sz) {
      fprintf(stderr, "Unable to get sha1 of cert\n");
      return -1;
    }
    string sha1 = hex_encode(buf, len);
    string sn = parse_cert_sn(cert.get());

    printf("[*] Cert %d:\n", i);
    printf("  Subject: %s\n", subj);
    printf("  Issuer : %s\n", issuer);
    printf("  Sha1   : %s\n", sha1.c_str());
    // printf("  SN     : %s\n", sn.c_str());

    vector<char *> ous;
    char *token;
    while ((token = strsep(&subj, "/")) != NULL) {
      if (token[0] == '\0') {
        continue;
      }
      if (strncmp(token, "OU", 2) == 0) {
        ous.push_back(token);
      }
    }
    sort(ous.begin(), ous.end(),
         [](char *a, char *b) { return strcmp(a, b) < 0; });
    for (auto it = ous.cbegin(); it != ous.cend(); ++it) {
      printf("  %s\n", *it);
    }
    OPENSSL_free(subj);
    OPENSSL_free(issuer);
  }

  return 0;
}

static int read_file(int fd, uint32_t off, uint32_t len, void *buf) {
  if (lseek(fd, off, SEEK_SET) == -1) {
    perror("lseek");
    return -1;
  }
  if (read(fd, buf, len) != len) {
    perror("read");
    return -1;
  }
  return 0;
}

static int elf32_find_hashseg(int fd, uint32_t &offset, uint32_t &size) {
  Elf32_Ehdr ehdr;
  if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
    perror("read");
    return -1;
  }

  Elf32_Phdr phdr;
  for (int i = 0; i < ehdr.e_phnum; ++i) {
    uint32_t poff = ehdr.e_phoff + i * ehdr.e_phentsize;
    if (read_file(fd, poff, sizeof(phdr), &phdr) == -1) {
      return -1;
    }

    if ((phdr.p_flags & kElfPhdrTypeMask) == kElfPhdrTypeHash) {
      offset = phdr.p_offset;
      size = phdr.p_filesz;
      return 0;
    }
  }

  return -1;
}

static int elf64_find_hashseg(int fd, uint32_t &offset, uint32_t &size) {
  Elf64_Ehdr ehdr;
  if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
    perror("read");
    return -1;
  }

  Elf64_Phdr phdr;
  for (int i = 0; i < ehdr.e_phnum; ++i) {
    uint32_t poff = ehdr.e_phoff + i * ehdr.e_phentsize;
    if (read_file(fd, poff, sizeof(phdr), &phdr) == -1) {
      return -1;
    }

    if ((phdr.p_flags & kElfPhdrTypeMask) == kElfPhdrTypeHash) {
      offset = phdr.p_offset;
      size = phdr.p_filesz;
      return 0;
    }
  }

  return -1;
}

static void usage(const char *cmd) {
  fprintf(stderr, "%s [-d] img\n", cmd);
  exit(-1);
}

static int find_hash_segment(int fd, uint32_t &hash_off, uint32_t &hash_sz) {
  unsigned char ident[EI_NIDENT];
  if (read(fd, ident, sizeof(ident)) != sizeof(ident)) {
    perror("read");
    return -1;
  }
  if (ident[0] != ELFMAG0 || ident[1] != ELFMAG1 || ident[2] != ELFMAG2 ||
      ident[3] != ELFMAG3) {
    fprintf(stderr, "Not elf format\n");
    return -1;
  }

  if (lseek(fd, 0, SEEK_SET) == -1) {
    perror("lseek");
    return -1;
  }

  int ret;
  if (ident[EI_CLASS] == ELFCLASS32) {
    ret = elf32_find_hashseg(fd, hash_off, hash_sz);
  } else if (ident[EI_CLASS] == ELFCLASS64) {
    ret = elf64_find_hashseg(fd, hash_off, hash_sz);
  } else {
    fprintf(stderr, "Invalid elf format\n");
    return -1;
  }

  return ret;
}

static int print_metadata(int fd, uint32_t off, uint32_t sz, const char *type) {
  if (sz == 0) {
    return 0;
  } else if (sz <= sizeof(metadata_base)) {
    fprintf(stderr, "invalid metadata\n");
    return -1;
  }

  shared_ptr<metadata_base> meta_base(new metadata_base);
  if (!meta_base) {
    fprintf(stderr, "OOM\n");
    return -1;
  }

  if (read_file(fd, off, sizeof(metadata_base), meta_base.get()) == -1) {
    return -1;
  }

  if (meta_base->major != 0 || meta_base->minor != 0) {
    fprintf(stderr, "unsupported version: %d.%d\n", meta_base->major,
            meta_base->minor);
    return -1;
  }

  // Only support 0.0 now.
  if (sz != sizeof(metadata_0_0)) {
    fprintf(stderr, "invalid metadata version 0.0\n");
    return -1;
  }

  shared_ptr<metadata_0_0> meta(new metadata_0_0);
  if (!meta) {
    fprintf(stderr, "OOM\n");
    return -1;
  }

  if (read_file(fd, off, sizeof(metadata_0_0), meta.get()) == -1) {
    return -1;
  }

  printf("[*] %s METADATA:\n", type);
  printf("    SW_ID: %x\n", meta->sw_id);
  printf("    HW_ID(JTAG): %x\n", meta->hw_id);
  printf("    OEM_ID: %x\n", meta->oem_id);
  printf("    MODEL_ID: %x\n", meta->model_id);
  printf("    APP_ID: %x\n", meta->app_id);
  printf("    SOC_VERS: ");
  int i;
  for (i=0; i<12; ++i) {
    printf("%x, ", meta->soc_vers[i]);
  }
  printf("\n");
  printf("    MULTI_SNS: ");
  for (i=0; i<8; ++i) {
    printf("%x, ", meta->multi_serial_numbers[i]);
  }
  printf("\n");
  printf("    ROLLBACK: %x\n", meta->anti_rollback_version);
  printf("    ROT_EN: %x\n", meta->rot_en);
  printf("    IN_USE_SOC_HW_VER: %x\n", meta->in_use_soc_hw_version);
  printf("    USE_SERIAL_NUMBER_IN_SIGNING: %x\n", meta->use_serial_number_in_signing);
  printf("    OEM_ID_INDEPENDENT: %x\n", meta->oem_id_independent);
  printf("    ROOT_REVOKE_ACTIVATE_ENABLE: %x\n", meta->root_revoke_activate_enable);
  printf("    UIE_KEY_SWITCH_ENABLE: %x\n", meta->uie_key_switch_enable);
  printf("    DEBUG: %x\n", meta->debug);
  return 0;
}

static int print_ous_in_header(
    int fd, uint32_t hash_off, uint32_t hash_sz,
    shared_ptr<mi_boot_image_header_type_v6> header) {
  uint32_t qti_md_sz = header->qti_md_size;
  uint32_t qti_md_off = hash_off + sizeof(mi_boot_image_header_type_v6);
  int ret = print_metadata(fd, qti_md_off, qti_md_sz, "QTI");

  uint32_t md_sz = header->md_size;
  uint32_t md_off = qti_md_off + qti_md_sz;
  ret = ret || print_metadata(fd, md_off, md_sz, "OEM");
  return ret;
}

static int find_certs(int fd, uint32_t hash_off, uint32_t hash_sz,
                      uint32_t cert_off, uint32_t cert_sz, const char *dump_file) {
  uint32_t off = hash_off + cert_off;
  shared_ptr<char> buf(new char[cert_sz]);
  if (!buf) {
    fprintf(stderr, "OOM\n");
    return -1;
  }

  if (read_file(fd, off, cert_sz, buf.get()) == -1) {
    return -1;
  }

  if (dump_file) {
    return print_certs(buf, cert_sz, dump_file);
  } else {
    return print_certs(buf, cert_sz);
  }
}

int main(int argc, char **argv) {
  int dump = 0;
  char *dump_file = NULL;
  int c;
  const char *cmd = argv[0];
  struct option options[] = {
      {"dump", no_argument, NULL, 'd'},
      {NULL, 0, NULL, 0},
  };

  while ((c = getopt_long(argc, argv, "d", options, NULL)) != -1) {
    switch (c) {
      case 'd':
        dump = 1;
        break;
      default:
        usage(cmd);
        break;
    }
  }
  argc -= optind;
  argv += optind;

  if (argc != 1) {
    usage(cmd);
  }
  if (dump) {
    dump_file = argv[0];
  }

  ScopedFd fd(open(argv[0], O_RDONLY));
  if (!fd) {
    perror("open");
    return -1;
  }

  uint32_t hash_off, hash_sz;
  if (find_hash_segment(fd.get(), hash_off, hash_sz) == -1) {
    //fprintf(stderr, "Can't find hash segment\n");
    return -1;
  }
  DBG("hash segment: %x %x\n", hash_off, hash_sz);

  shared_ptr<mi_boot_image_header_type> header(new mi_boot_image_header_type);
  if (!header) {
    fprintf(stderr, "OOM\n");
    return -1;
  }

  if (read_file(fd.get(), hash_off, kHashSegHeaderSz, header.get()) == -1) {
    return -1;
  }

  // TODO: support -d(--dump)

  if (header->version > kMbnV6) {
    fprintf(stderr, "Unsupported mbn version %d\n", header->version);
    return -1;
  } else if (header->version == kMbnV6) {
    DBG("V6 header\n");
    shared_ptr<mi_boot_image_header_type_v6> header_v6(
        new mi_boot_image_header_type_v6);
    if (read_file(fd.get(), hash_off, kHashSegHeaderSzV6, header_v6.get()) ==
        -1) {
      return -1;
    }
    uint32_t cert_off = sizeof(mi_boot_image_header_type_v6) +
                        header_v6->qti_md_size +
                        header_v6->md_size +
                        header_v6->base.code_size +
                        header_v6->base.signature_size;
    DBG("cert: %x %x\n", cert_off, header_v6->base.cert_chain_size);
    if (dump_file) {
      return find_certs(fd.get(), hash_off, hash_sz, cert_off,
                   header_v6->base.cert_chain_size, dump_file);
    } else {
      if (print_ous_in_header(fd.get(), hash_off, hash_sz, header_v6) ||
          find_certs(fd.get(), hash_off, hash_sz, cert_off,
                     header_v6->base.cert_chain_size, dump_file)) {
        return -1;
      }
    }
  } else {
    uint32_t cert_off = sizeof(mi_boot_image_header_type) + header->code_size +
                        header->signature_size;
    return find_certs(fd.get(), hash_off, hash_sz, cert_off,
                      header->cert_chain_size, dump_file);
  }
}
