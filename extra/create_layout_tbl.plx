#!/usr/bin/perl
# storage layout create tool.
# Copyright (C) 2011 CSR plc. All rights reserved
#
# usage: create_layout_tbl.plx dev_file skip_bytes tbl_entry0 tlb_entry1 ...
#   write layout table into file "dev_file", from offset "skip_bytes".
#   the layout table format: 
#           layout_table_magic        4 bytes
#           layout entrys_number      4 bytes_
#           entrys_array              sizeof(layout_entry)* layout entrys_number
#   the layout_entry format:
#           entry_name                32 chars, null terminated.
#           start_sector              4 bytes, unsigned int
#           max_sectors               4 bytes, unsigned int           
#           actual_sectors            4 bytes, unsigned int  

use strict;
use Fcntl;

my $LAYOUT_MAGIC = 0x4C42544C; # 'LTBL'

print "$0:begin to create storage layout table.\n";
#print "input list:@ARGV\n";	

#
#check parameters
if(@ARGV < 2 or (@ARGV - 2)%4 != 0){
	&err_invalid_parameter; 
}

my $dev_file = shift(@ARGV);
my $skip_bytes = shift(@ARGV);
print "dev_file:$dev_file, skip_bytes:$skip_bytes\n";

my $entrys_num = @ARGV / 4;
my $raw_data;

#
#open dev file
sysopen(DEV_FILE, "$dev_file",O_RDWR) or die $!;
binmode(DEV_FILE);
seek(DEV_FILE,$skip_bytes,0) or die $!;

#
#write magic and entrys num
print "total $entrys_num entrys\n";
$raw_data = pack("I2",$LAYOUT_MAGIC,$entrys_num);
print DEV_FILE $raw_data;

#
#write entrys
if(@ARGV != 0){
	print "in below table, unit is sector\n";
	print "index:\tstart\tmax len\tactual\tname\n";
}

my $entry_idx = 0;
while(@ARGV != 0){
	my $entry_name =  shift(@ARGV);
	my $start =  shift(@ARGV);
	my $max_size =  shift(@ARGV);	
	my $actual_size =  shift(@ARGV);	  	
	if($start == 0 or $max_size == 0){
		&err_invalid_parameter; 	
	}
	print "$entry_idx:\t$start\t$max_size\t$actual_size\t$entry_name\n";
	$raw_data=pack("a32I3",$entry_name,$start,$max_size,$actual_size);
	print DEV_FILE $raw_data;
	$entry_idx += 1;
}
#
#done

close(DEV_FILE);
print "create layout table done!\n";

sub err_invalid_parameter{
	print "invalid parameter!\n";
	print "usage format: $0 dev_file skip_bytes tbl_entry0 tlb_entry1 ...\n";
	print "the tbl_entry format is: entry_name start_sector max_sectors actual_sectors\n";
	die;
}


