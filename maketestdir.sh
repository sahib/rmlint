#!/bin/sh
#TODO:  add header and credits

defaults() {
set -u
dirname=testdir #directory name to create
DO_ASK=
groups=3		#number of groups of dupes to make for each size
count=10		#number of dupes to make per group
herringratio=1	#number of red herrings per actual dupe			
flipbytes=10	#number of bytes to change in the red herrings
				#relative to the "true" duplicates
seed=$(date)	#for random number generator
}

ask() {
cat << EOF
This script will build a 'testdir' directory under the current path
and populate it with files, links and loops for the purposes of
testing rmlint.
It is advisable to view this script before running for the first time!

Execute this script with -d to disable this message
Hit enter to continue; CTRL-C to abort immediately
EOF
read dummy_var
}

usage()
{
cat << EOF
usage: $0 options [size1 size2 size3 ...]

if no size given, defaults to filesize 1024 bytes
OPTIONS:
-h            Show this message
-n dirname    Set name of created directory ($dirname)
-g groups     Sets number of dupe groups at each filesize ($groups)
-c count      Sets number of dupefiles to create per group ($count)
-r ratio      Sets (integer) ratio of false dupes to true dupes ($herringratio)
-f flipbytes  Sets number of bytes to flip (in a random location) in each file ($flipbytes)
-d            Do not ask before running
EOF
}

check_existing(){
if [ -e "$dirname" ] || [ -f "$dirname" ]; then
    echo "Error: directory $dirname already exists"
    exit 2
fi
}

random_string()
{
    cat /dev/urandom | env LC_CTYPE=C tr -dc 'a-zA-Z0-9' | fold -w ${1:-10} | head -n 1
}

make_rbind()
{
	mkdir -p $dirname/rbind1/rbind2_
	mkdir -p $dirname/rbind2/rbind3_
	mkdir -p $dirname/rbind3/rbind1_
	sudo mount -o rbind $dirname/rbind1 $dirname/rbind3/rbind1_
	sudo mount -o rbind $dirname/rbind2 $dirname/rbind1/rbind2_
	sudo mount -o rbind $dirname/rbind3 $dirname/rbind2/rbind3_
	mkdir -p $dirname/rbind3/rbind2__
	sudo mount -o rbind $dirname/rbind1/rbind2_ $dirname/rbind3/rbind2__
	dd if=/dev/urandom of=$dirname/rbind1/uniquefile1 bs=1024 count=1
	dd if=/dev/urandom of=$dirname/rbind2/uniquefile2 bs=1024 count=1
	# note the above two files are both originals so should not be removed by rmlint, even with -H option
	dd if=/dev/urandom of=$dirname/rbind3/dupfile3 bs=1024 count=1
	cp --reflink=auto $dirname/rbind3/dupfile3 $dirname/rbind1/dupfile2
	cp --reflink=auto $dirname/rbind3/dupfile3 $dirname/rbind2/dupfile1
	
	dd if=/dev/urandom of=$dirname/rbind1/hardlink_samename bs=1024 count=1
	ln $dirname/rbind1/hardlink_samename $dirname/rbind2/hardlink_samename
	ln $dirname/rbind2/hardlink_samename $dirname/rbind3/hardlink_samename
	ln $dirname/rbind2/hardlink_samename $dirname/rbind3/hardlink_diffname
}

make_dupes()
{
	asize=$1
	if [[ $asize =~ ^-?[0-9]+$ ]]; then
		for g in `seq 1 $groups`; do
			for d in `seq 1 $count`; do
				filename=$d"duplicate"$(random_string)
				if [ "$d" == "1" ]; then
					#create "original" duplicate
					echo creating original $filename
					dd if=/dev/urandom of="$dirname/dupes/$filename" bs="$asize" count=1 &> /dev/null
					seedfile="$filename"
				else
					#create "copy" duplicate.  Note we bypass cp to prevent reflink copy
					echo creating copy of $seedfile as $filename
					cp --reflink=auto "$dirname/dupes/$seedfile" "$dirname/dupes/$filename" > /dev/null
				fi
				for h in `seq 1 $herringratio`; do
					#create red herring (same size as dupes but different content)
					filename=$d"herring"$(random_string)
					echo creating red herring $filename
					# first make an exact copy
					cp --reflink=auto "$dirname/dupes/$seedfile" "$dirname/dupes/$filename" > /dev/null
					# now change some bytes
					byte_to_change=$(( $RANDOM % `expr $asize - $flipbytes` ))
					echo Changing after $byte_to_change bytes
					newbytes=$(random_string $flipbytes)
					printf $newbytes | dd of="$dirname/dupes/$filename" bs=$flipbytes seek=$byte_to_change count=1 conv=notrunc &> /dev/null
				done
			done
			
		done
	else
		echo "Error: non-integer size $asize"
	fi
}

#----MAIN----
defaults
while getopts “dhn:c:s:r:g:f:” OPTION
do
  case $OPTION in
     h)
       usage
       exit 1
       ;;
     d)
       DO_ASK=false
       ;;
     n)
       dirname="$OPTARG"
       ;;
     c)
       count="$OPTARG"
       ;;
     s)
       size="$OPTARG"
       ;;
     r)
       herringratio="$OPTARG"
       ;;
     f)
       flipbytes="$OPTARG"
       ;;
     g)
       groups="$OPTARG"
       ;;
  esac
done

if [ -z $DO_ASK ]
then
  usage
  ask
fi

check_existing

mkdir "$dirname"
mkdir "$dirname/dupes"

shift $(( $OPTIND -1 ))

if [ $# -eq 0 ]; then
	make_dupes 1024
else
	for size in "$@"
	do
		make_dupes $size
	done
fi

make_rbind

