UNAME := $(shell uname)
src := qcert.cc
flags := -Iinclude -lcrypto -DDEBUG
ifeq ($(UNAME), Darwin)
	flags += -L/usr/local/opt/openssl/lib -I/usr/local/opt/openssl/include
endif

qcert : $(src)
	g++ -o qcert $(src) -std=c++11 $(flags)

run: qcert
	./qcert xbl.img

.PHONY: clean
clean:
	rm qcert
