#!/bin/bash

usage()
{
    echo $(basename $0) " [device_name]"
}
if [ $# != 1 ]; then 
  usage
  exit 1
fi

device=$1

if [ x"$device" = x"" ];then
  echo "device is null"
  exit 1 
fi



blkparse -i $device -d $device.blktrace.bin  > /dev/null

btt -i $device.blktrace.bin -q $device.q2c_latency > /dev/null 

filename="sys_mbps_fp.dat"

echo "
set terminal pngcairo lw 2
set title \"$device MBPS\"
set xlabel \"time\"
set ylabel \"IOPS\"
set output '${device}_mbps.png'
plot \"$filename\" w lp pt 7 title \"MBPS\" 
set output
set terminal wxt
" | /usr/bin/gnuplot

