#!/usr/bin/env python3
'''Brute force ozip aes key.
'''

import binascii
import os
import sys
from Crypto.Cipher import AES


def main():
    '''Main function.
    '''
    if len(sys.argv) != 3:
        print('Usage: {} update.ozip libapplypatch_jni.so'.format(sys.argv[0]))
        sys.exit(1)

    ozip_file = open(sys.argv[1], 'rb')
    key_file = open(sys.argv[2], 'rb')

    ozip_file.seek(0x1050, 0)
    test_data = ozip_file.read(16)

    key_file_size = os.path.getsize(sys.argv[2])
    pos = 0
    while pos < key_file_size - 16:
        key_file.seek(pos, 0)
        key = key_file.read(16)
        pos = pos + 4

        aes = AES.new(key, AES.MODE_ECB)
        plain = aes.decrypt(test_data)
        if plain[0:4] == b'\x50\x4B\x03\x04':
            print(binascii.hexlify(key).decode())


if __name__ == '__main__':
    main()
