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



if [ ! -e $device.blktrace.bin ]; then
    blkparse -i $device -d $device.blktrace.bin  > /dev/null
fi

btt -i $device.blktrace.bin -B $device.offset > /dev/null 
filename=`ls ${device}.offset*_c.dat`

awk '{print $1,$2,$3-$2}' $filename >${filename}.new

echo "
set terminal pngcairo enhanced font 'arial,10' fontscale 1.0 size 800, 600  
set title \"$device Generate Block access\"
set xlabel \"Time (second)\"
set ylabel \"Block Number\"
set zlabel \"# Blocks Per IO\" rotate by 90
set xtics rotate by 60
set grid
set output '${device}_offset_pattern.png'
splot \"${filename}.new\" u 1:2:3 w l ls 1 title \"Offset\" 
set output
set terminal wxt
" | /usr/bin/gnuplot

echo "
set terminal pngcairo enhanced font 'arial,10' fontscale 1.0 size 800, 600  
set title \"$device offset \"
set xlabel \"time (second)\"
set ylabel \"offset (# block)\"
set output '${device}_offset.png'
plot \"$filename\" u 1:2 w lp pt 7 title \"offset of device\" 
set output
set terminal wxt
" | /usr/bin/gnuplot

total_io=`awk '{print $3-$2}' ${device}.offset_*_c.dat |sort -k 1 |uniq -c | awk 'BEGIN {  totol=0;} {total += $1;}  END {print total}'`

awk '{print $3-$2}' ${device}.offset_*_r.dat |sort -k 1 |uniq -c |sort -nk 2 | awk -v total=$total_io '{print $2,$1*100.0/total}' > $device.iosize_r_freq
awk '{print $3-$2}' ${device}.offset_*_w.dat |sort -k 1 |uniq -c |sort -nk 2 | awk -v total=$total_io '{print $2,$1*100.0/total}' > $device.iosize_w_freq

cat ${device}.iosize_r_freq ${device}.iosize_w_freq |sort -nk 1 > ${device}.iosize_freq
join -1 1 -2 1 -o 0,1.2,2.2 -e 0 -a1 -a2 $device.iosize_r_freq $device.iosize_w_freq > $device.iosize_freq 2>/dev/null

echo "
set terminal pngcairo enhanced font 'arial,10' fontscale 1.0 size 800, 600  
set auto x   
set yrange [0:100] 
set ylabel \"% of Total I/O\"  
set xlabel \"I/O Size (sector)\"      
set title \"I/O Distribution by I/O Size \" 
set style histogram rowstacked  
set boxwidth 0.50 relative 
set style fill transparent solid 0.5 noborder 
set xtics rotate by -45   
set grid  
set ytics 5 
set output '${device}_iosize_hist.png' 
plot \"$device.iosize_freq\"  u 2:xticlabels(1)  with boxes ti \"read\" , ''  using 3:xticlabels(1) with boxes ti \"write\" 
" | /usr/bin/gnuplot
