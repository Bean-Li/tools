# tools

# streamwriter

usage:  ./streamwriter -p 3 -s 10240 -n 2 -f 10 -S 80 -b 5

-p parallel   thread num in parallel 
-s speed      stream speed of single thread
-n num        number of files that  every thread will generate
-f frames     frames in one second : 10 mean write 1 frame every 100 ms
-S size       file size of single file 
-b prefix     prefix of every file 

the file name is like this :
X-Y-Z.mp4

X mean prefix 
Y mean thread index
Z mean file index 


the output of this program is like this :

```
============SUMMARY:===========
parallel             : 3
speed                : 10240 (KBPS)
frame rate           : 10 (frames/s)
single file size     : 80 (MB)
filenum per thread   : 2

filename begin_index : 5

total_frame          : 480
avg time per frame   : 25266 us
drop_frame           : 0
drop rate            : 0.0000 
----------------------------------------------
     time-range (ms):	     times
      [    0~     1]:	         0
      [    1~     2]:	       120
      [    2~     4]:	        63
      [    4~     6]:	         4
      [    6~     8]:	         2
      [    8~    10]:	         3
      [   10~    15]:	         3
      [   15~    20]:	         0
      [   20~    25]:	        13
      [   25~    30]:	        22
      [   30~    40]:	        88
      [   40~    50]:	       134
      [   50~    60]:	        26
      [   60~    70]:	         2
      [   70~    80]:	         0
      [   80~    90]:	         0
      [   90~   100]:	         0
      [  100~   120]:	         0
      [  120~   150]:	         0
      [  150~   200]:	         0
      [  200~   300]:	         0
      [  300~   400]:	         0
      [  400~   500]:	         0
      [  500~  1000]:	         0
      [ 1000~  2000]:	         0
      [ 2000~   N/A]:	         0
----------------------------------------------
```


