#!/bin/bash

##
# bootimgtool.sh - tools to manipulate boot.img
##

#DEBUG="true"

set -e

usage()
{
    echo "bootimgtool.sh boot.img cmd args"
    echo "  printinfo           : print header information"
    echo "  printcert [-p]      : print certificate in text format or PEM format"
    echo "  unpack dir          : unpack boot.img into kernel, ramdisk fs, etc. into dir"
    echo "  build dir [key_path]: build boot.img from dir and sign using key_path.{pk8,x509.pem}"
}

# copy_from_file infile outfile offset size align
copy_from_file()
{
    infile=$1
    outfile=$2
    offset=$3
    size=$4
    align=$5

    count=$(( size / align ))
    remind=$(( size % align ))
    skip=$(( offset / align ))
    dd if=$infile of=$outfile bs=$align count=$count skip=$skip >/dev/null 2>&1
    skip=$(( offset + size - remind ))
    seek=$(( align * count ))
    dd if=$infile of=$outfile bs=1 count=$remind skip=$skip seek=$seek >/dev/null 2>&1
}

# append_file_and_update_header src dst padding update_off
append_file_and_update_header()
{
    local src=$1
    local dst=$2
    local padding=$3
    local update_off=$4

    if [ -f $src ]; then
        local size=`ls -l $src | cut -f 5 -d " "`
        # append file
        dd if=$src of=$dst conv=notrunc oflag=append 2>/dev/null
        dd if=/dev/zero of=$dst conv=notrunc oflag=append bs=1\
            count=$(( (padding - size % padding) % padding )) 2>/dev/null

        # update header
        printf "%02x" $(( size & 255 )) | xxd -r -ps | dd of=$dst bs=1\
            seek=$update_off conv=notrunc 2>/dev/null
        printf "%02x" $(( (size>>8) & 255 )) | xxd -r -ps | dd of=$dst bs=1\
            count=1 seek=$(( update_off+1 )) conv=notrunc 2>/dev/null
        printf "%02x" $(( (size>>16) & 255 )) | xxd -r -ps | dd of=$dst bs=1\
            count=1 seek=$(( update_off+2 )) conv=notrunc 2>/dev/null
        printf "%02x" $(( (size>>24) & 255 )) | xxd -r -ps | dd of=$dst bs=1\
            count=1 seek=$(( update_off+3 )) conv=notrunc 2>/dev/null
    fi
}

# get_info boot.img/header
get_info()
{
    ks_off=8    # kernel_size offset
    rs_off=16   # ramdisk_size offset
    ss_off=24   # second size offset
    dt_off=40   # dt size offset
    id_off=$(( 48 + 16 + 512 ))

    local header=$1

    kernel_size=`hexdump -s $ks_off -n 4 -e '/4 "%u"' $header`
    kernel_addr=`hexdump -s 12 -n 4 -e '/4 "%u"' $header`

    ramdisk_size=`hexdump -s $rs_off -n 4 -e '/4 "%u"' $header`
    ramdisk_addr=`hexdump -s 20 -n 4 -e '/4 "%u"' $header`

    second_size=`hexdump -s $ss_off -n 4 -e '/4 "%u"' $header`
    second_addr=`hexdump -s 28 -n 4 -e '/4 "%u"' $header`

    tags_addr=`hexdump -s 32 -n 4 -e '/4 "%u"' $header`
    page_size=`hexdump -s 36 -n 4 -e '/4 "%u"' $header`

    dt_size=`hexdump -s $dt_off -n 4 -e '/4 "%u"' $header`

    padded_kernel_size=$(( (kernel_size+page_size-1)/page_size*page_size ))
    padded_ramdisk_size=$(( (ramdisk_size+page_size-1)/page_size*page_size ))
    padded_second_size=$(( (second_size+page_size-1)/page_size*page_size ))
    padded_dt_size=$(( (dt_size+page_size-1)/page_size*page_size ))

    os_version=`hexdump -s 44 -n 4 -e '/4 "%u"' $header`
    name=`hexdump -s 48 -n 16 -e '/16 "%s"' $header`
    cmdline=`hexdump -s $(( 48 + 16 )) -n 512 -e '/512 "%s"' $header`
    extra_cmdline=`hexdump -s $(( 48 + 16 + 512 + 32)) -n 1024 -e '/1024 "%s"' $header`

    total_size=$(( padded_kernel_size+padded_ramdisk_size+padded_second_size\
        +padded_dt_size+page_size ))
}

print_info_internal()
{
    printf "%-20s: 0x%x\n" "kernel_size" $kernel_size
    printf "%-20s: 0x%x\n" "padded_kernel_size" $padded_kernel_size
    printf "%-20s: 0x%x\n" "kernel_addr" $kernel_addr
    printf "%-20s: 0x%x\n" "ramdisk_size" $ramdisk_size
    printf "%-20s: 0x%x\n" "padded_ramdisk_size" $padded_ramdisk_size
    printf "%-20s: 0x%x\n" "ramdisk_addr" $ramdisk_addr
    printf "%-20s: 0x%x\n" "second_size" $second_size
    printf "%-20s: 0x%x\n" "second_addr" $second_addr
    printf "%-20s: 0x%x\n" "tags_addr" $tags_addr
    printf "%-20s: 0x%x\n" "page_size" $page_size
    printf "%-20s: 0x%x\n" "dt_size" $dt_size
    printf "%-20s: 0x%x\n" "padded_dt_size" $padded_dt_size
    printf "%-20s: 0x%x\n" "total_size" $total_size
    if [ $os_version -ne 0 ]; then
        local A=$(( (os_version >> 25) & 0x7f ))
        local B=$(( (os_version >> 18) & 0x7f ))
        local C=$(( (os_version >> 11) & 0x7f ))
        local Y=$(( ((os_version >> 4) & 0x7f) + 2000 ))
        local M=$(( os_version & 0xf ))
        printf "%-20s: %s\n" "version" "$A.$B.$C"
        printf "%-20s: %s\n" "patch_level" "$Y-$M"
    fi
    printf "%-20s: %s\n" "name" ${name:-"(null)"}
    echo "cmdline             : "${cmdline:-"(null)"}
    echo "extra_cmdline       : "${extra_cmdline:-"(null)"}
}

print_info()
{
    if [ $# -ne 0 ];then
        usage && exit 1
    fi

    get_info $bootimg
    print_info_internal
}


print_cert()
{
    while getopts "p" opt; do
        case $opt in
            p)
                pem="true";;
            *)
                usage
                exit 1
        esac
    done

    shift $(( OPTIND - 1 ))
    if [ -$# -lt 0 ]; then
        usage
        exit 1
    fi

    get_info $bootimg

    info_line=`openssl asn1parse -inform DER -in $bootimg -offset $total_size | grep "7:d=1"`
    cert_len=${info_line##*l=}
    cert_len=${cert_len%%cons:*}
    cert_len=`echo $cert_len | tr -d " "`
    if [ -n "$pem" ]; then
        dd if=$bootimg bs=1 skip=$(( total_size+7 )) count=$(( cert_len+4 )) 2>/dev/null |\
            openssl x509 -inform DER
    else
        dd if=$bootimg bs=1 skip=$(( total_size+7 )) count=$(( cert_len+4 )) 2>/dev/null |\
            openssl x509 -inform DER -noout -text
    fi
}

# unpack dir
unpack()
{
    # check argument
    if [ $# -ne 1 ]; then
        usage
        exit 1
    fi

    get_info $bootimg

    # mkdir
    dir=$1
    if [ ! -d $dir ]; then
        if ! mkdir $dir; then
            echo "Failed to make dir: $dir" && exit 1
        fi
    else
        echo "$dir alreday exists. Remove it first!" && exit 1
    fi

    # unpack
    dd if=$bootimg of=$dir/header bs=$page_size count=1 >/dev/null 2>&1
    test $kernel_size -ne 0 && copy_from_file $bootimg $dir/kernel $page_size $kernel_size\
        $page_size
    test $ramdisk_size -ne 0 && copy_from_file $bootimg $dir/ramdisk\
        $(( page_size + padded_kernel_size )) $ramdisk_size $page_size
    test $second_size -ne 0 && copy_from_file $bootimg $dir/second\
        $(( page_size + padded_kernel_size + padded_ramdisk_size )) $second_size $page_size
    mkdir $dir/root
    cd $dir/root
    gunzip -c ../ramdisk | cpio -i >/dev/null 2>&1
    cd - >/dev/null 2>&1
}

# build dir
build()
{
    # check argument
    if [ $# -gt 2 ]; then
        usage
        exit 1
    fi

    local dir=$1
    local key_path=$2

    # check dir
    if [ ! -d $dir ]; then
        echo "$dir not exists." && exit 1
    fi
    if [ ! -f $dir/header ]; then
        echo "$dir/header not exists." && exit 1
    fi
    if [ -n "${key_path}" ]; then
        if [ ! -f ${key_path}.pk8 -o ! -f ${key_path}.x509.pem ]; then
            echo "${key_path}.{pk8,x509} not found." && exit 1
        fi
    fi

    # build
    # cd $dir/root || ( echo "No root dir" && exit 1 )
    # find . | cpio -o -H newc 2>/dev/null | gzip > ../new_ramdisk
    # cd - >/dev/null 2>&1
    $tool_home/bin/mkbootfs $dir/root > $dir/new_ramdisk

    local kernel=""
    local ramdisk=""
    local second=""

    get_info $dir/header
    dd if=$dir/header of=${bootimg}_unsigned 2>/dev/null
    if [ -f $dir/kernel ];then
        kernel=$dir/kernel
        append_file_and_update_header $kernel ${bootimg}_unsigned $page_size $ks_off
    fi
    if [ -f $dir/new_ramdisk ];then
        ramdisk=$dir/new_ramdisk
        append_file_and_update_header $ramdisk ${bootimg}_unsigned $page_size $rs_off
    fi
    if [ -f $dir/second ];then
        second=$dir/second
        append_file_and_update_header $second ${bootimg}_unsigned $page_size $ss_off
    fi

    cat $kernel $ramdisk $second | openssl sha1 -binary | dd of=${bootimg}_unsigned bs=1\
        count=20 seek=$id_off conv=notrunc 2>/dev/null
    if [ -z "$key_path" ]; then
        cp ${bootimg}_unsigned ${bootimg}
    else
        java -jar $tool_home/bin/BootSignature.jar /boot ${bootimg}_unsigned\
            ${key_path}.pk8 ${key_path}.x509.pem $bootimg
    fi
}

############ main #############

if [ $# -lt 2 ]; then
    usage
    exit 1
fi

tool_home=`dirname $0`
bootimg=$1
cmd=$2
shift 2


case $cmd in
    "printinfo")
        print_info $@;;
    "printcert")
        print_cert $@;;
    "unpack")
        unpack $@;;
    "build")
        build $@;;
    *)
        usage
        exit 1
esac
