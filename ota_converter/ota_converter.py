#!/usr/bin/env python3

# Convert system/vendor data in ota package update.zip to image data
# in system/vendor.img
#
# Usage: python3 ota_converter.py xx.transfer.list xx.new.dat.br xx.img
#
# TODO: handle exceptions

import brotli
import logging
import sys

BLOCK_SIZE = 4096
BUF_SIZE = BLOCK_SIZE * 1024
ZERO_BLOCK = b'\x00' * BLOCK_SIZE


def cmd_erase(file, ranges):
    '''Erase command.

    This command is for ssd so we don't use it.
    '''

    pass


def cmd_zero(file, ranges):
    '''Zeroize those blocks in #ranges.'''

    for begin, end in ranges:
        file.seek(begin * BLOCK_SIZE, 0)
        while begin != end:
            file.write(ZERO_BLOCK)
            begin = begin + 1


def cmd_new(file, ranges, data_file, dec, cookie):
    '''Decompress and write data.'''

    for begin, end in ranges:
        #logging.debug('new: {}'.format(begin))
        begin_byte = begin * BLOCK_SIZE
        end_byte = end * BLOCK_SIZE
        file.seek(begin_byte, 0)
        while begin_byte != end_byte:
            # logging.debug('begin_byte: {}, end_byte: {}'.format(
            #    begin_byte, end_byte))
            space = end_byte - begin_byte
            if dec:
                if cookie[0]:
                    output = cookie[0]
                else:
                    data = data_file.read(BUF_SIZE)
                    output = dec.process(data)

                if space > BUF_SIZE:
                    space = BUF_SIZE
                if len(output) > space:
                    cookie[0] = output[space:]
                    output = output[:space]
                else:
                    cookie[0] = None
            else:
                if space > BUF_SIZE:
                    space = BUF_SIZE
                output = data_file.read(space)

            if output:
                file.write(output)
                begin_byte = begin_byte + len(output)


def parse_ranges(args):
    '''Parse args to a set of (begin, end) ranges.'''

    tokens = args.split(',')
    if len(tokens) % 2 != 1:
        logging.error('Invalid args: ' + args)
        return None

    try:
        num = int(tokens[0])
    except ValueError:
        logging.error('Invalid number {}'.format(tokens[0]))
        return None
    if num != len(tokens) - 1:
        logging.error('Invalid number {}'.format(tokens[0]))
        return None
    ret = []
    for i in range(1, len(tokens), 2):
        try:
            begin = int(tokens[i])
            end = int(tokens[i+1])
            ret.append((begin, end))
        except ValueError:
            logging.error('Invalid range number: {}, {}'.format(
                tokens[i], tokens[i+1]))
            return None
    return ret


def get_max_block(file):
    max_block = 0
    saved_pos = file.tell()
    for line in file.readlines():
        tokens = line.split()
        if len(tokens) != 2:
            logging.error('Invalid line: ' + line)
            return max_block
        args = tokens[1]
        ranges = parse_ranges(args)
        for range in ranges:
            if range[1] > max_block:
                max_block = range[1]
    file.seek(saved_pos)
    return max_block


def create_image(file_name, blocks):
    '''Create target image file.'''

    file = open(file_name, 'wb')
    file.truncate(blocks * BLOCK_SIZE)
    file.close()


def transfer(file, dat_fn, img_fn):
    '''Transfer data.'''

    br_compressed = True if dat_fn.endswith('.br') else False

    dec = brotli.Decompressor() if br_compressed else None
    dat_f = open(dat_fn, 'rb')
    img_f = open(img_fn, 'wb')
    cookie = [None]

    for line in file.readlines():
        logging.info(line)
        tokens = line.split()
        if len(tokens) != 2:
            logging.error('Invalid line: ' + line)
            return False
        cmd = tokens[0]
        ranges = parse_ranges(tokens[1])
        if cmd == 'erase':
            cmd_erase(img_f, ranges)
        elif cmd == 'zero':
            cmd_zero(img_f, ranges)
        elif cmd == 'new':
            cmd_new(img_f, ranges, dat_f, dec, cookie)
        else:
            logging.error('Invalid cmd: ' + cmd)
            return False
    return True


def main():
    if len(sys.argv) != 4:
        logging.error(
            'usage: {} xxx.transfer.list xxx.new.dat[.br] xxx.img'.format(sys.argv[0]))
        sys.exit(1)

    logging.basicConfig(level=logging.INFO)
    file = open(sys.argv[1], 'r')
    version = int(file.readline())
    print('[*] version: {}'.format(version))

    block = int(file.readline())
    file.readline()
    file.readline()
    max_block = get_max_block(file)
    if max_block < block:
        logging.error('Invalid max block: {}'.format(max_block))
        sys.exit(1)

    print('[*] blocks: {}'.format(max_block))
    create_image(sys.argv[3], max_block)

    print('[*] transfering...')
    if not transfer(file, sys.argv[2], sys.argv[3]):
        sys.exit(1)

    print('[*] done')
    sys.exit(0)


if __name__ == '__main__':
    main()
