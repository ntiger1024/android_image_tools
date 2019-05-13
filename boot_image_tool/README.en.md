# bootimgtool.sh

Pack, unpack or print information of boot.img. AVB1.0 is supported.

Usage:

```
bootimgtool.sh boot.img cmd args
  printinfo           : print header information
  printcert [-p]      : print certificate in text format. Or in PEM format if -p option specific
  unpack dir          : unpack <boot.img> into kernel, ramdisk fs, etc., into <dir>
  build dir [key_path]: build <boot.img> from <dir>. Sign using key_path.{pk8,x509.pem}
                        if has <key_path> parameter
```
