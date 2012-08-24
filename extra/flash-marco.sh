#!/bin/bash
# Flashing images and/or filesystem to SDCARD or NAND disk
# Copyright (C) 2011 CSR plc. All rights reserved
#
# Usage flash.sh <devfile> [<targets>]
#
# devfile may be any of: sdb, sdc,..., which is the name of your
#   SDCARD or NAND disk device
# targets may be one or more of:
#  nboot uboot uimage minigps hibernation modules rootfs system data
#  images
#  kernel
#  all
#

readonly usage="\
Flashing images and filesystems to SDCARD or NAND disk for CSR
Linux-based systems. Copyright (C) 2011 CSR plc. Version: 1.23

Usage: $0 <devfile> [<targets>]
				targets can be one or more of
				nboot, uboot, uimage, minigps,
				hibernation, modules, rootfs,
				system and data

   or: $0 <devfile> kernel
				kernel include uimage and modules

   or: $0 <devfile> images
				images include n/uboot, uimage,
				minigps and hibernation

   or: $0 <devfile> all
				include all images and filesystems,
				will create partitions if not available

   or: $0 <devfile> 
				include all images and kernel modules

Examples:
	$0 sdb images
	$0 sdb kernel
	$0 sdb all
	$0 sdb
"

#
# macros used in this file
#
readonly size_1K=1024
readonly size_1M=$((1024*1024))
# part size for auto creating partitions, in MBs
boot_part_size=20
root_part_size=200
syst_part_size=200
data_part_size=200
data_sys_part_size=100
if [ "$(which mkfs.ext4)" = "" ]; then
readonly rootfs_type="ext3"
else
readonly rootfs_type="ext4"
fi

#
# image file name stored in boot partition
#
readonly nboot=nboot.bin
readonly uboot=u-boot-marco.bin
readonly uimage=uImage
readonly dtb=dtb
readonly minigps=minigps.bin
readonly hibernation=hibernation.bin

#
# device file & path
#
readonly devfile=$1
readonly devpath=/dev/$devfile
readonly bootdev=${devpath}1 # boot partition
readonly rootdev=${devpath}2 # root partition
readonly systdev=${devpath}3 # system partition (android)
readonly datadev=${devpath}5 # data partition (android)
readonly datasysdev=${devpath}6 # data.sys partition (android)

#
# config file
#
readonly configfile=.config

show_help()
{
	echo -e "$usage"
}

if [ $# = 0 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
	show_help
	exit 1
fi

if [ ! "$(whoami)" = "root" ]; then
	echo "Permission denied. Please use 'sudo $0 ...'"
	exit 1
fi

if ! fdisk -s $devpath 2>/dev/null 1>/dev/null; then
	echo -e "invalid block device: '$devpath'\n"
	show_help
	exit 1
fi

readonly removable=$(cat /sys/block/$devfile/removable)

if [ ! "$removable" = "1" ]; then
	echo -e "'$devpath' is not your SDCARD or NAND disk! Please be careful!\a"
	exit 1
fi

readonly sector_size=$(cat /sys/block/$devfile/queue/hw_sector_size)

#import config info, such as SDRAM_SIZE
if test -e $configfile; then
. $configfile
else
  echo "can not find config file: $configfile"
  exit 1
fi


#
# ---------------
# storage layout:
# ---------------
#
# sector[0]====================================
#         |  MBR         - (512B~16KB)         |
# sector[1]------------------------------------
#         |  nboot/uboot - (2MB-$sector_size)  |
#     [2MB]------------------------------------
#         |  uboot commit flag sector          |
#[2MB+1sec]------------------------------------
#         |  nothing                           |
#   [2.5MB]------------------------------------
#         |  storage layout table -  16K       |
#[3MB-64KB]------------------------------------
#         |  uboot environment variables area  |
#     [3MB]------------------------------------
#         |  uImage                            |
#     [7MB]------------------------------------
#         |  minigps.bin                       |
#     [...]------------------------------------
#         |  minigps data                      |
#     [...]------------------------------------
#         |  hibernation.bin                   |
#     [...]------------------------------------
#         |  hibernation data                  |
#     [...]====================================
#         |  rootfs partition (ext3/ext4 - ro) |
#     [...]====================================
#         |  system partition (ext3/ext4 - ro) |
#     [...]====================================
#         |  data partition (ext3/ext4 - rw)   |
#     [...]====================================
#         |  data sys part  (ext3/ext4 - rw)   |
#     [...]====================================
#         |  user partition (fat32... - rw)    |
#          ====================================
#

#
# storage max reserve length
#
readonly nboot_max_length=$((16*size_1K))
readonly uboot_max_length=$((2*$size_1M-$sector_size))
readonly uboot_commit_flag_max_length=$sector_size
readonly layout_tbl_max_length=$((16*size_1K))
readonly uboot_env_length=$((64*$size_1K))
readonly uimage_max_length=$((4*$size_1M-64*$size_1K))
readonly dtb_max_length=$((64*$size_1K))
readonly minigps_bin_max_length=$((4*$size_1M))
readonly minigps_data_max_length=$((5*$size_1M))
readonly hiber_bin_max_length=$((1*$size_1M))
readonly hiber_one_snapshot_size=$(($SDRAM_SIZE*$size_1M/3))

#
# caculate boot partition layout
#
if test -e $nboot; then
	# nboot only used by Atlas4 & Prima
	readonly nboot_beg_sector=1
	readonly nboot_toc_sector=$(($nboot_max_length/$sector_size+1))
fi
# uboot begin sector
readonly uboot_beg_sector=$(($nboot_toc_sector+1))
# uboot commit flag sector (only used by nanddisk)
readonly uboot_flg_sector=$((2*$size_1M/$sector_size))
# storage layout table begin sector, 2.5M
readonly layout_tbl_beg_byte=$(((2048+512)*$size_1K))
readonly layout_tbl_beg_sector=$(($layout_tbl_beg_byte/$sector_size))
# uboot environment start sector
readonly uboot_env_sector=$(((3*$size_1M-64*$size_1K)/$sector_size))
# uimage begin sector
readonly uimage_beg_sector=$((3*$size_1M/$sector_size))
# dtb begin sector
readonly dtb_beg_sector=$(((7*$size_1M-64*$size_1K)/$sector_size))

# current boot partition size
boot_part_size=$(($uimage_beg_sector*$sector_size+$uimage_max_length+$dtb_max_length))

if test -e $minigps; then
	# minigps.bin begin sector
	readonly minigps_bin_beg_sector=$(($boot_part_size/$sector_size))
	# reserve space for minigps.bin in boot partition
	boot_part_size=$(($boot_part_size+$minigps_bin_max_length))
	# minigps data begin sector and length
	readonly minigps_data_beg_sector=$(($boot_part_size/$sector_size))
	# reserve space for minigps data in boot partition
	boot_part_size=$(($boot_part_size+$minigps_data_max_length))
fi

if  [ ! "$ENABLE_HIBERNATION" = "" ] || [ ! "$ENABLE_ACCELEBOOT" = "" ]; then
	if test -e $hibernation ; then
		# hibernation.bin begin sector
		readonly hiber_bin_beg_sector=$(($boot_part_size/$sector_size))
		# reserve space for hibernation.bin in boot partition
		boot_part_size=$(($boot_part_size+$hiber_bin_max_length))

		# hibernation data begin sector and length
		readonly hiber_data_beg_sector=$(($boot_part_size/$sector_size))

		# reserve space for hibernation data in boot partition
		hiber_data_max_length=0;
		if test -n "$ENABLE_HIBERNATION" ; then
			echo hibernation enabled.
			hiber_data_max_length=$(($hiber_one_snapshot_size+$hiber_data_max_length))
		fi
		if test -n "$ENABLE_ACCELEBOOT" ; then
			echo acceleboot enabled.
			hiber_data_max_length=$(($hiber_one_snapshot_size+$hiber_data_max_length))
		fi
		boot_part_size=$(($boot_part_size+$hiber_data_max_length))
	else
		echo warning! $hibernation does not exist.
	fi
fi

#reserve more 5M storage in boot partition
boot_part_size=$(($boot_part_size+(5*$size_1M)))

#partition size is MB unit
boot_part_size=$(($boot_part_size/$size_1M))

#
# bytes_to_sectors(bytes)
#    bytes must sector size aligned
bytes_to_sectors()
{
	echo $(($1 / $sector_size))
}

#
# file_length_bytes(file)
#
file_length_bytes()
{
	echo $(($(stat -c %s $1 2>/dev/null)))
}

#
# file_length_sectors(file)
#
file_length_sectors()
{
	echo $((($(file_length_bytes $1) + $sector_size - 1) / $sector_size))
}

#
# file_out_of_range(file, bytes)
#
file_out_of_range()
{
	if ! test -e $1; then
		echo "$1: No such file"
		return
	fi

	local length=$(file_length_bytes $1)
	if [ $length -gt $2 ]; then
		echo "error: $1 out of range ($length bytes > $2 bytes)!"
	fi
}

#
# erase_sector(start, count)
#
erase_sector()
{
	echo erase_sector\($1, $2\)
	dd seek=$1 count=$2 bs=$sector_size if=/dev/zero of=$devpath
}

#
# write_file(start_sector, file)
#
write_file()
{
	echo "write_file($1, $2)"
	if ! test -e $2 ; then
		echo "$2: No such file"
		return
	fi
	local count=$(file_length_sectors $2)
	dd seek=$1 if=$2 of=$devpath bs=$sector_size count=$count
}

#
# flash_layout_tbl()
#
flash_layout_tbl()
{
	local layout_info=
	
	if ! test -e create_layout_tbl.plx; then
		echo can not find create_layout_tbl.plx, faild to create layout table.
	fi

	if test -n "$nboot_beg_sector"; then
			layout_info+=" $nboot $nboot_beg_sector $(bytes_to_sectors $nboot_max_length) $(file_length_sectors  $nboot)"
	fi

	layout_info+=" $uboot $uboot_beg_sector $(bytes_to_sectors $uboot_max_length) $(file_length_sectors $uboot)" 
	layout_info+=" uboot_commit_flag $uboot_flg_sector $(bytes_to_sectors $uboot_commit_flag_max_length) $(bytes_to_sectors $uboot_commit_flag_max_length)" 
	layout_info+=" layout_tbl $layout_tbl_beg_sector $(bytes_to_sectors $layout_tbl_max_length) $(bytes_to_sectors $layout_tbl_max_length)"
	layout_info+=" uboot_env  $uboot_env_sector $(bytes_to_sectors $uboot_env_length) $(bytes_to_sectors $uboot_env_length)"
	layout_info+=" $uimage  $uimage_beg_sector  $(bytes_to_sectors $uimage_max_length) $(file_length_sectors $uimage)"
	layout_info+=" $dtb  $dtb_beg_sector  $(bytes_to_sectors $dtb_max_length) $(file_length_sectors $dtb)"

	if test -n "$minigps_beg_sector"; then
			layout_info+=" $minigps $minigps_bin_beg_sector $(bytes_to_sectors $minigps_bin_max_length) $(file_length_sectors $minigps)"
			layout_info+=" minigps_data $minigps_data_beg_sector $(bytes_to_sectors $minigps_data_max_length) $(bytes_to_sectors $minigps_data_max_length)"
	fi
			
	if test -n "$hiber_bin_beg_sector"; then
			layout_info+=" $hibernation $hiber_bin_beg_sector $(bytes_to_sectors $hiber_bin_max_length) $(file_length_sectors $hibernation)"
			layout_info+=" hibernation_data $hiber_data_beg_sector $(bytes_to_sectors $hiber_data_max_length) $(bytes_to_sectors $hiber_data_max_length)"
	fi
	
	#replace all '.' and '-' to '_' in entry name
	layout_info=${layout_info//./_}
	layout_info=${layout_info//-/_}

	./create_layout_tbl.plx $devpath $layout_tbl_beg_byte $layout_info
}

#
# flash_nboot()
#
flash_nboot()
{
	local err=$(file_out_of_range $nboot $nboot_max_length)
	if [ ! "$err" = "" ]; then
		echo $err
		return
	fi
	write_file $nboot_beg_sector $nboot
}

#
# flash_uboot()
#
flash_uboot()
{
	local err=$(file_out_of_range $uboot $uboot_max_length)
	if [ ! "$err" = "" ]; then
		echo $err
		return
	fi
	# erase environment variables
	erase_sector $uboot_env_sector $(($uboot_env_length/$sector_size))
	# write u-boot
	write_file $uboot_beg_sector $uboot
	# commit u-boot to NAND flash - required by CSR proprietary NANDdisk
	erase_sector $uboot_flg_sector 1
}

#
# mount_partition(mountdev, mountdir)
#
mount_partition()
{
	local mountdev=$1
	local mountdir=$2

	if [ "$mountdir" = "" ]; then
		return
	fi

	mkdir -p $mountdir 2>/dev/null
	# umount old directory
	mount | grep $mountdev | awk {'print $3'} | xargs umount 2>/dev/null
	if mount -t $rootfs_type $mountdev $mountdir; then
		echo done
	else
		rm -r $mountdir
	fi
}

#
# umount_partition(mountdir)
#
umount_partition()
{
	local mountdir=$1
	if umount -fl $mountdir; then
		rm -r $mountdir
		echo done
	fi
}

#
# flash_uimage()
#
flash_uimage()
{
	local err=$(file_out_of_range $uimage $uimage_max_length)
	if [ ! "$err" = "" ]; then
		echo $err
		return
	fi
	write_file $uimage_beg_sector $uimage
}

#
# flash_dtb()
#
flash_dtb()
{
	local err=$(file_out_of_range $dtb $dtb_max_length)
	if [ ! "$err" = "" ]; then
		echo $err
		return
	fi
	write_file $dtb_beg_sector $dtb
}

#
# copy_modules()
#
copy_modules()
{
	if [ ! "$(mount_partition $rootdev disk)" = "" ]; then
		echo -n "copying kernel modules..."
		mkdir -p disk/lib/modules 2>/dev/null
		cp -fr rootfs/lib/modules/* disk/lib/modules/
		umount_partition disk
	fi
}

#
# flash_kernel()
#
flash_kernel()
{
	flash_uimage
	flash_dtb
	copy_modules
}

#
# flash minigps.bin
#
flash_minigps()
{
	local err=$(file_out_of_range $minigps $minigps_max_length)
	if [ ! "$err" = "" ]; then
		echo $err
		return
	fi
	write_file $minigps_beg_sector $minigps
}

#
# flash hibernation.bin
#
flash_hibernation()
{
	local err=$(file_out_of_range $hibernation $hiber_bin_max_length)
	if [ ! "$err" = "" ]; then
		echo $err
		return
	fi
	write_file $hiber_bin_beg_sector $hibernation

	#destory old hibernation data
	dd seek=$hiber_data_beg_sector if=/dev/zero of=$devpath bs=$sector_size count=300
}

#
# flash all images & kernel modules
#
flash_images()
{
	if test -e $nboot; then
		flash_nboot
	fi
	flash_uboot
	flash_uimage
	flash_dtb

	if test -e $minigps; then
		flash_minigps
	fi
	if  [ ! "$ENABLE_HIBERNATION" = "" ] || [ ! "$ENABLE_ACCELEBOOT" = "" ]; then
		if test -e $hibernation ; then
			flash_hibernation
		else
			echo error! $hibernation does not exist.
			exit 1
		fi
	else
		echo warning! hibernation and acceleboot not enabled in menuconfig.
	fi
	flash_layout_tbl
}

#
# calc_min_parts
#
calc_min_parts()
{
	#
	# already has correct boot partition size
	#
	min_boot_part_size=$boot_part_size

	#
	# calculate min root partition size
	#
	if ! test -d rootfs; then return; fi
	min_root_part_size=$(expr $(du -b rootfs | tail -1 | awk {'print $1'}) / $size_1M)
	min_root_part_size=$((($min_root_part_size + 59)/10*10)) # extra 50MB
	if [ $min_root_part_size -gt $root_part_size ]; then
		root_part_size=$min_root_part_size
	fi

	#
	# calculate min system partition size
	#
	if ! test -d system; then return; fi
	min_syst_part_size=$(expr $(du -b system | tail -1 | awk {'print $1'}) / $size_1M)
	min_syst_part_size=$((($min_syst_part_size + 59)/10*10)) # extra 50MB
	if [ $min_syst_part_size -gt $syst_part_size ]; then
		syst_part_size=$min_syst_part_size
	fi

	#
	# calcuate min data partition size
	#
	if ! test -d data; then return; fi
	min_data_part_size=$(expr $(du -b data | tail -1 | awk {'print $1'}) / $size_1M)
	min_data_part_size=$((($min_data_part_size + 59)/10*10)) # extra 50MB
	if [ $min_syst_part_size -gt $syst_part_size ]; then
		data_part_size=$min_data_part_size
	fi
}

#
# does the disk have valid partitions?
#
has_valid_partitions()
{
	local block_size=$size_1K

	for i in $bootdev $rootdev $systdev $datadev $datasysdev;  do
		if [ ! $(fdisk -s $i 2>/dev/null) ]; then
			echo no
			return
		fi
	done
	calc_min_parts >/dev/null
	local boot_part_size=$(expr $(fdisk -s $bootdev) / $block_size)
	if [[ $boot_part_size -lt $min_boot_part_size ]]; then
		echo no
		return
	fi
	local root_part_size=$(expr $(fdisk -s $rootdev) / $block_size)
	if [[ $root_part_size -lt $min_root_part_size ]]; then
		echo no
		return
	fi
	local syst_part_size=$(expr $(fdisk -s $systdev) / $block_size)
	if [[ $syst_part_size -lt $min_syst_part_size ]]; then
		echo no
		return
	fi
	local data_part_size=$(expr $(fdisk -s $datadev) / $block_size)
	if [[ $data_part_size -lt $min_data_part_size ]]; then
		echo no
		return
	fi
	local data_sys_part_size=$(expr $(fdisk -s $datasysdev) / $block_size)
	if [[ $data_sys_part_size -lt $min_data_part_size ]]; then
		echo no
		return
	fi

	for i in $rootdev $systdev $datadev $datasysdev; do
		if [[ ! $(mount_partition $i disk) ]]; then
			echo no
			return
		fi
		umount_partition disk >/dev/null
	done
	echo yes
}

#
# umount all partitions
#
umount_all_partitions()
{
	mount | grep "$devpath" | awk {'print $3'} | xargs umount 2>/dev/null
}

#
# remove all partitions
#
remove_all_partitions()
{
	fdisk -cul $devpath | awk {'print $1'} | grep $devpath | \
	sed 's/[^0-9]//g' | xargs -i parted $devpath rm {} >/dev/null
}

#
# create_partition(part_type, part_num, part_size)
#
create_partition()
{
	local part_type=$1
	local part_num=$2
	local part_size=$3

	if [ "$part_size" = "" ]; then
		show_size=+all
	else
		show_size=$part_size
	fi

	echo -n "create_partition($part_type, $part_num, $show_size)..."

	case $part_type in
	p|e|l);;
	*)
		echo $part_type not supported!;;
	esac

	fdisk -cu $devpath 1>/dev/null << EOF
	n
	$part_type
	$part_num

	$part_size
	wq
EOF
	echo "done"
}

#
# format_partition(part_num, fs_type, label)
#
format_partition()
{
	local part_num=$1
	local fs_type=$2
	local label=$3

	echo -n "format_partition($part_num, $fs_type)..."

	for i in {1..5}; do
		if test -b ${devpath}${part_num}; then
			break
		fi
		sleep 1
	done

	case $fs_type in
	fat32)
		fdisk -cu $devpath 1>/dev/null << EOF
		t
		$part_num
		c
		wq
EOF
		sleep 1
		if mkfs.vfat -F 32 ${devpath}${part_num} -n $label 1>/dev/null 2>/dev/null; then
			echo done
		else
			echo failed!
		fi
		;;
	*)
		if mkfs.$fs_type ${devpath}${part_num} -L $label 1>/dev/null 2>/dev/null; then
			echo done
		else
			echo failed!
		fi
		;;
	esac
}

#
# create_all_partitions required
#
create_all_partitions()
{
	if ! parted -s $devpath mklabel "msdos"; then
		return
	fi

	calc_min_parts

	create_partition p 1 +${boot_part_size}M
	create_partition p 2 +${root_part_size}M
	create_partition p 3 +${syst_part_size}M

	create_partition e 4
	create_partition l 5 +${data_part_size}M
	create_partition l 6 +${data_sys_part_size}M
	create_partition l 7

	format_partition 2 $rootfs_type root
	format_partition 3 $rootfs_type system
	format_partition 5 $rootfs_type data
	format_partition 6 $rootfs_type data.sys
	format_partition 7 fat32 user
}

#
# generate test MAC address for Unifi Wi-Fi
#
generate_mac()
{
	local dat=$(date +%H:%M:%S)
	local mac=$(find disk/lib/firmware -iname mac.txt 2>/dev/null)
	for i in $mac; do
		echo "00:02:5b:"$dat > $i
	done
}

#
# copy_filesystem(copydir partdev)
#
copy_filesystem()
{
	local copydir=$1
	local partdev=$2

	if ! test -d $copydir; then
		return
	fi

	if [[ ! $(mount_partition $partdev disk) ]]; then
		return
	fi

	echo -n "copying $copydir to $partdev..."
	rm -fr disk/*
	cp -fr $copydir/* disk/
	if [ "$partdev" = "$rootdev" ]; then
		generate_mac
		if test -d system/etc; then
			cp -fr system/etc/* disk/etc/
		fi
		mknod disk/dev/null c 1 3
		mknod disk/dev/console c 5 1
		mknod disk/dev/ttyS1 c 4 65
	elif [ "$partdev" = "$systdev" ]; then
		rm -fr disk/etc
		ln -fs /etc disk/etc
	fi
	umount_partition disk
}

#
# flash all images and rootfs
#
flash_all()
{
	if [ "`has_valid_partitions`" = "no" ]; then
		echo "-----------------------------------------------------"
		echo "PARTITIONS NOT VALID, WILL REMOVE AND RECREATE ALL!!!"
		echo "-----------------------------------------------------"
		umount_all_partitions
		remove_all_partitions
		create_all_partitions
	fi
	flash_images
	copy_filesystem rootfs $rootdev
	copy_filesystem system $systdev
	copy_filesystem data $datadev
}

if [ "$#" = "1" ]; then
	flash_images
	copy_modules
fi

for i in $*
do
	case $i in
	sd*)
		;;
	nboot)
		flash_nboot
		;;
	uboot)
		flash_uboot
		;;
	uimage)
		flash_uimage
		;;
	modules)
		copy_modules
		;;
	kernel)
		flash_kernel
		;;
	minigps)
		flash_minigps
		;;
	hibernation)
		flash_hibernation
		;;
	rootfs)
		copy_filesystem rootfs $rootdev
		;;
	system)
		copy_filesystem system $systdev
		;;
	data)
		copy_filesystem data $datadev
		;;
	images)
		flash_images
		;;
	all)
		flash_all
		;;
	*)
		echo "wrong target: $i"
		exit 1
		;;
	esac
done
eject $devfile
