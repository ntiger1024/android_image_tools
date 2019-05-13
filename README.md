Android Image Tools
===================

一些平时自己开发使用的，操作Android分区镜像的小工具。运行环境为Ubuntu 14.04及以后版本。

1. `boot_image_tool`

用于解压，重打包boot.img，也可以打印boot.img的header和签名信息。适用于AVB1.0。

更多内容请看[`boot_imge_tool`](./boot_image_tool/README.md)。

2. mnt-droid

转换并挂载ext4 稀疏文件系统镜像。一些手机厂商提供ROM的线刷包（工厂包）供用户下载，如Google的Pixel
等。其中包含system.img, vendor.img等分区的稀疏文件系统镜像。如果想要直接查看文件系统中的内容，则
需要挂载到本地。此工具用于挂载这些文件系统镜像。需要先安装simg2img工具：

`sudo apt-get install simg2img`

用法：

```
# 挂载system.img到本地system目录
mnt-droid system

# 挂载vendor.img到本地vendor目录
mnt-droid vendor
```

3. `ota_converter`

将卡刷包中的system，vendor等分区数据，转换成文件系统镜像。一些手机厂商不提供ROM的线刷包（工厂包），
而只提供卡刷包。卡刷包中包含system等分区的完整数据，但是并非文件系统镜像格式，不能直接挂载，需要先
转换成文件系统镜像。此工具用于将卡刷包中的文件系统数据转换成文件系统镜像格式(**非稀疏文件系统镜像，
无需进一步转换即可直接挂载**）。

更多内容请看[`ota_converter`](./ota_converter/README)。

编译：

`make`

用法：

```
# 转换system.ext4.img
./ota_converter system.transfer.list system.new.dat.br system.ext4.img
```

4. avbtool

基于Android原生系统代码中的avbtool，加入一个打印vbmeta中公钥的功能。

用法：

`avbtool dump_public_key --image vbmeta.img`

5. `print_qcom_image_info`

打印高通平台特定image的签名相关信息，如打印bootloader abl的签名信息。

编译：

`make`

用法：

```
# 打印abl信息
./qcert abl.elf
```
