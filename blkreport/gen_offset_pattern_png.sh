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

btt -i $device.blktrace.bin -B $device.offset > /dev/null 
filename=`ls ${device}.offset*_c.dat`

awk '{print $1,$2,$3-$2}' $filename >${filename}.new

echo "
set terminal pngcairo lw 2
set title \"$device Generate Block access\"
set xlabel \"Time (second)\"
set ylabel \"Block Number\"
set zlabel \"# Blocks Per IO\"
set grid
set output '${device}_offset_pattern.png'
splot \"${filename}.new\" u 1:2:3 w l ls 1 title \"Offset\" 
set output
set terminal wxt
" | /usr/bin/gnuplot

echo "
set terminal pngcairo lw 2
set title \"$device offset \"
set xlabel \"time (second)\"
set ylabel \"offset (# block)\"
set output '${device}_offset.png'
plot \"$filename\" u 1:2 w lp pt 7 title \"offset of device\" 
set output
set terminal wxt
" | /usr/bin/gnuplot

