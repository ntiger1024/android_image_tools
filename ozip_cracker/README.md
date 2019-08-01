# Ozip Cracker

Ozip cracker is a brute force tool to find ozip aes keys of oppo devices.

## What is it?

Oppo devices' ROM packages is not normal zip file format but a customed file
format called 'ozip'. The ozip file is created like this:

```
                              xx.ozip
                          +--------------+
  xx.zip                  | 0x1050 header|
+-------------+  encrypt  +--------------+
| 0x10 B      |  ===>     | 0x10 B       |
+-------------+           +--------------+
|             |  copy     |              |
| 0x4000 B    |  ===>     | 0x4000 B     |
|             |           |              |
+-------------+  encrypt  +--------------|
| 0x10 B      |  ===>     | 0x10 B       |
+-------------+           +--------------+
|             |  copy     |              |
| 0x4000 B    |  ===>     | 0x4000 B     |
|             |           |              |
+-------------+           +--------------+
   ...           ===>        ...
```

Sometimes I want to unpack ozip file to check something. So I must find the way
to decrypt the ozip file. There are already some tools to decrypt them. But they
don't support newer devices like R17, Reno, etc. After google I get this
[artical](https://bkerler.github.io/reversing/2019/04/24/the-game-begins/).
From it I know that:

1. The encrytion algorithm is AES-128-ECB
2. The key is stored in `/vendor/lib64/libapplypatch_jni.so` as plaintext.

So, brute force may be a better way to get the key than reverse engineering...


## Usage

To find the key of an ozip file, you have to get the `libapplypatch_jni.so` from
its corresponding device. For example, if you have an Oppo FindX device and its
ozip file `PAFM00_11_OTA_0300_all_rqjUlPT7h9J9.ozip`. Now you want to find its
ozip key. You can do this:

```
$ adb pull /vendor/lib64/libapplypatch_jni.so ./
$ ./ozip_cracker PAFM00_11_OTA_0300_all_rqjUlPT7h9J9.ozip libapplypatch_jni.so
d4d2cd61d4afdce13b5e01221bd14d20
```

When you get the key, you can decrypt ozip files using which tools you like.
Here I use this tool: [`oppo_ozip_decrypt`](https://github.com/ntiger1024/oppo_ozip_decrypt).

```
$ python ozipdecrypt.py PBDM00_11_A.17_OTA_0170_all_201901072337.ozip d4d2cd61d4afdce13b5e01221bd14d20
ozipdecrypt 0.5 (c) B.Kerler 2017-2019
using your key: d4d2cd61d4afdce13b5e01221bd14d20
Decrypting...
DONE!!
```
