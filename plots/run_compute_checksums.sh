#!/bin/sh
# Do this as preparation:
# mount -t tmpfs -o size=512m tmpfs ~/ramdisk
# dd if=/dev/urandom of=~/ramdisk/1-test.img bs=1014 count=$[1000*1] iflag=fullblock
# dd if=/dev/urandom of=~/ramdisk/2-test.img bs=1024 count=$[1000*2] iflag=fullblock
# dd if=/dev/urandom of=~/ramdisk/4-test.img bs=1024 count=$[1000*4] iflag=fullblock
# dd if=/dev/urandom of=~/ramdisk/8-test.img bs=1024 count=$[1000*8] iflag=fullblock
# dd if=/dev/urandom of=~/ramdisk/16-test.img bs=1024 count=$[1000*16] iflag=fullblock
# dd if=/dev/urandom of=~/ramdisk/32-test.img bs=1024 count=$[1000*32] iflag=fullblock
# dd if=/dev/urandom of=~/ramdisk/64-test.img bs=1024 count=$[1000*64] iflag=fullblock
# dd if=/dev/urandom of=~/ramdisk/128-test.img bs=1024 count=$[1000*128] iflag=fullblock
# dd if=/dev/urandom of=~/ramdisk/256-test.img bs=1024 count=$[1000*256] iflag=fullblock
# dd if=/dev/urandom of=~/ramdisk/512-test.img bs=1024 count=$[1000*512] iflag=fullblock
# dd if=/dev/urandom of=~/ramdisk/1024-test.img bs=1024 count=$[1000*1024] iflag=fullblock

./compute_checksums mmap $(ls ~/ramdisk/*.img | sort -n) > /tmp/mmap.log
./compute_checksums 0.25 $(ls ~/ramdisk/*.img | sort -n) > /tmp/0.25.log 
./compute_checksums 0.50 $(ls ~/ramdisk/*.img | sort -n) > /tmp/0.50.log 
./compute_checksums 1.00 $(ls ~/ramdisk/*.img | sort -n) > /tmp/1.00.log 
./compute_checksums 2.00 $(ls ~/ramdisk/*.img | sort -n) > /tmp/2.00.log 
./compute_checksums 3.00 $(ls ~/ramdisk/*.img | sort -n) > /tmp/3.00.log 
./compute_checksums 4.00 $(ls ~/ramdisk/*.img | sort -n) > /tmp/4.00.log 

# Run render_plot.py /tmp/*.log now.
