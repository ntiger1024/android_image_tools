all: ota_converter

ota_converter: ota_converter.cc lib/libbrotlidec-static.a lib/libbrotlicommon-static.a
	g++ -g -DLOG_LEVEL=4 -Iinclude ota_converter.cc -o ota_converter -static lib/libbrotlidec-static.a lib/libbrotlicommon-static.a

.PHONY: clean
clean:
	-rm ota_converter
