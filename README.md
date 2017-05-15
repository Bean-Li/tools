# tools

# streamwriter

usage:  ./streamwriter -p 1 -s 10240 -n 1 -f 10 -S 100 -b 2

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


