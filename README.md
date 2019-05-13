# bootimagetool.sh

解压/打包boot.img，或者打印boot.img header和签名信息。适用于AVB1.0系统（大部分小于等于android 8.0的系统）

用法：

```
bootimgtool.sh boot.img cmd args
  printinfo           : 打印header信息(kernel加载地址，命令行参数等)
  printcert [-p]      : 打印签名证书信息。默认打印text格式，如果指定`-p`参数，则打印pem格式。
  unpack dir          : 解压boot.img 到指定目录
  build dir [your_key_path.pk8 you_key_path.x509.pem]: 重打包boot.img。如果制定了秘钥，则使用秘钥签名生成的boot.img。
```
