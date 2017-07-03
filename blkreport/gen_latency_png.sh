#!/bin/bash

usage()
{
    echo $(basename $0) " [device_name] [type]"
    echo "valid type name is  q2c and d2c"
    
}
if [ $# != 2 ]; then 
  usage
  exit 1
fi

device=$1
l_type=$2

if [ x"$device" = x"" ];then
  echo "device is null"
  exit 1 
fi


if [ $l_type != 'q2c' -a $l_type != "d2c" ]; then
  echo "invalid type"
  exit 1
fi


blkparse -i $device -d $device.blktrace.bin  > /dev/null

if [ $l_type == "q2c" ];then
    btt -i $device.blktrace.bin -q $device.q2c_latency > /dev/null 
fi
if [ $l_type == "d2c" ]; then
    btt -i $device.blktrace.bin -l $device.d2c_latency > /dev/null 
fi

filename=$(ls $device.${l_type}_latency*.dat)

echo "
set terminal pngcairo lw 2
set title \"$device ${l_type^^} latency\"
set xlabel \"time\"
set ylabel \"$type latency\"
set output '$device.${l_type}_latency.png'
plot \"$filename\" title \"latency\" 
set output
set terminal wxt
" | /usr/bin/gnuplot

