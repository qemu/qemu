DEV="/dev/$1"
DATA_FOR_WRITE="$(cat /dev/urandom | tr -dc A-Za-z | head --bytes=3072)"

assert_read_success(){
	RET="$?"
	if [ "$RET" -ne "0" ]; then
	   echo "Read did not succeed as expected" >&2
	   exit 1
	fi
}


assert_read_failure(){
	RET="$?"
	if [ "$RET" -eq "0" ]; then
	   echo "Read did not fail as expected" >&2
	   exit 1
	fi
}



#format disk
nvme format $DEV --reset

# read without uncor
nvme read $DEV -s 0 -c 5 -z 3072 > /dev/null
assert_read_success

# add uncorrect and try to read again
nvme write-uncor $DEV -s 2 -c 1
nvme read $DEV -s 0 -c 5 -z 3072 > /dev/null
assert_read_failure

# write data - should clear uncorrectable
echo $DATA_FOR_WRITE | nvme write $DEV -s 0 -c 5 -z 3072
nvme read $DEV -s 0 -c 5 -z 3072 > /dev/null
assert_read_success

# write uncorrectable, but read from another block
nvme write-uncor $DEV -s 3 -c 5
nvme read $DEV -s 0 -c 2 -z 3072 > /dev/null
assert_read_success
nvme read $DEV -s 3 -c 1 -z 3072 > /dev/null
assert_read_failure

echo "OK!"
exit 0
