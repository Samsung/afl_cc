#!/bin/sh

LIB_DIR="$( cd "$(dirname "$0")" ; pwd -P )"
. $LIB_DIR/library.sh

# libraries
is_empty()
{
	if [ -z "$@" ]; then
		true
	else
		false
	fi
}

is_integer()
{
	if is_empty "$@"; then
		false
	elif [ "$@" -eq "$@" ] 2>/dev/null; then
		true
	else
		false
	fi
}

is_positive_integer()
{
	if ! is_integer "$@"; then
		false
	else
		N="$@"
		C1=$(echo $N | head -c1)
		if [ "$C1" = "-" ]; then
			false
		else
			true
		fi
	fi
}

validate_args() 
{
	

	if [ "$#" -lt 10 ]; then
 		err "Illegal number of parameters"
    	err "$0 <afl-fuzz> <n_bins> <bin_1 bin_2 ... bin_n> <args> <n_runs> <n_instances> <infolder> <outfolder> <dict_1 dict_2 ... dict_n> <time_hrs>"
    	exit 1
	fi
}

kill_all()
{
	name=$1
	# echo "1=$1"
	# echo "2=$2"

	# echo "name=$name"
	# echo "cmd=ps -C $name --no-header --format 'pid'"
	pids=$(ps -C $name --no-header --format 'pid' 2>&1 1>/dev/null)
	while [ ! -z "$pids" ];
	do
		kill -9 $pids 2>&1 1>/dev/null
		pids=$(ps -C "$name" --no-header --format 'pid' 2>&1 1>/dev/null)
	done
}

run_group()
{
	# $GROUPID $AFLFUZZ $BINS $ARGS $INFOLDER $OUTFOLDER $N_PARALLEL_RUNS $N_INSTANCE $DICTFILE $TIME
	GROUPID=$1
	AFLFUZZ=$2
	BINS=$3
	ARGS=$4
	INFOLDER=$5
	OUTFOLDER=$6
	N_PARALLEL_RUNS=$7
	N_INSTANCE=$8
	DICTFILES=$9
	TIME=$10
	USE_MASTER=$11

	trap_pid_list=""
	wait_pid_list=""
	
	kill_all "afl-fuzz"
	kill_all "timeout"
	kill_all "python"
	
	N=`echo $BINS | wc -w`
	
	for r in `seq 1 $N_PARALLEL_RUNS`
	do
		
		# run a bin
		ind=0
		master_done=0
		for j in `seq 1 $N_INSTANCE`
		do
			for i in `seq 1 $N`
			do
				# read binary and dictionary
				b=$(echo $BINS | awk -v ind=$i 'BEGIN{OFS=IFS=" "} {print $ind}')
				DICTFILE=$(echo $DICTFILES | awk -v ind=$i 'BEGIN{OFS=IFS=" "} {print $ind}')
			
				B=`get_fn_from_file $b`-$j-$i
				OUT=$OUTFOLDER/$GROUPID-$r
				
				if [ $USE_MASTER -ne 0 ]; then
					# we want a single master, ie deterministic mode instance
					# when N_INSTANCE = 1, only one instance will have DICTFILE = none.
					# however if we have N_INSTANCE > 1, we use the dict only for the first instance
					if [ "$DICTFILE" != "none" ] && [ $master_done -eq 0 ]; then
						master_done=1
						cmd="timeout ${TIME}h $AFLFUZZ -m none -i $INFOLDER -o $OUT -M M-$B -x $DICTFILE $b $ARGS @@"
					else
						cmd="timeout ${TIME}h $AFLFUZZ -m none -i $INFOLDER -o $OUT -S S-$B $b $ARGS @@"
					fi
				else
					if [ "$DICTFILE" != "none" ]; then
						cmd="timeout ${TIME}h $AFLFUZZ -m none -i $INFOLDER -o $OUT -S $B -x $DICTFILE $b $ARGS @@"
					else
						cmd="timeout ${TIME}h $AFLFUZZ -m none -i $INFOLDER -o $OUT -S $B $b $ARGS @@"
					fi
				fi

				ind=$((ind+1))
				info "$cmd"
				eval "$cmd &" || { err "$cmd"; exit 1; }
				trap_pid_list="$trap_pid_list -$!"
				wait_pid_list="$wait_pid_list $!"

				#sleep 3s
			done
		done
		
	done

	# https://unix.stackexchange.com/questions/57667/why-cant-i-kill-a-timeout-called-from-a-bash-script-with-a-keystroke
	cmd="trap 'kill -INT $trap_pid_list' INT"
	info "$cmd"
	eval "$cmd" || { err "$cmd"; exit 1; }
	cmd="wait $wait_pid_list"
	info "$cmd"
	eval "$cmd"

	kill_all "afl-fuzz"
	kill_all "timeout"
	kill_all "python"
}

# start of code
validate_args "$@"

nproc=`nproc`
N_PROC=$((nproc))

AFLFUZZ=$1
NBINS=$2

if ! is_positive_integer $NBINS; then
	err "Invalid n_bin '$NBINS'"
	exit 1
fi

if ! regular_file_exists $AFLFUZZ; then
	err "'$AFLFUZZ' does not exist"
	exit 1
fi

# for b in `seq 1 $NBINS`
# do

# 	shift 
# done
shift
shift
NARGS=${#}
NSTOP=$((NARGS-NBINS))
BINS=""
while test ${#} -gt $NSTOP
do
	if is_empty $1; then
		err "Invalid file name '$1'"
		exit 0
	fi
  BINS="$BINS $1"
  shift
done

#echo "$@"
ARGS=$1
N_RUNS=$2
N_INSTANCE=$3
INFOLDER=$4
OUTFOLDER=$5
shift 
shift 
shift
shift
shift

NARGS=${#}
NSTOP=$((NARGS-NBINS))
DICTFILES=""
while test ${#} -gt $NSTOP
do
	if is_empty $1; then
		err "Invalid file name '$1'"
		exit 0
	fi
  DICTFILES="$DICTFILES $1"
  shift
done

TIME=$1
USE_MASTER=$2

# validate binaries
for bin in $BINS
do
	if ! regular_file_exists $bin; then
		err "'$bin' does not exist"
		exit 1
	fi
done

# validate dictionaries
for dict in $DICTFILES
do
	if [ "$dict" != "none" ]; then
		if ! regular_file_exists $dict ; then
			err "'$dict' does not exist"
			exit 1
		fi
	fi
done

# validate positive integer
if ! is_positive_integer $N_INSTANCE; then
	err "Invalid n_instance '$N_INSTANCE'"
	exit 1
fi

T_INSTANCES=$((N_INSTANCE*NBINS))

if ! is_positive_integer $N_RUNS; then
	err "Invalid n_instance '$N_RUNS'"
	exit 1
fi

if ! is_positive_integer $TIME; then
	err "Invalid time '$TIME'"
	exit 1
fi

# infolder must exist
if ! folder_exists $INFOLDER; then
	err "'$INFOLDER' does not exist"
	exit 1
fi

# outfolder must not exist
if folder_exists $OUTFOLDER; then
	err "'$OUTFOLDER' already exists"
	exit 1
fi

mkdir $OUTFOLDER


# let's use maximum of 2/3 of cores
USED_CPUS=$((N_PROC*4/5))
if [ $USED_CPUS -lt $T_INSTANCES ]; then
	err "Not enough cores ($USED_CPUS) for $T_INSTANCES instances"
	exit 1
fi


# Note: the check should always pass because of the chack above
N_PARALLEL_RUNS=$((USED_CPUS/T_INSTANCES))
if [ $N_PARALLEL_RUNS -le 0 ]; then
	err "Not enough CPUs to run ($USED_CPUS/$T_INSTANCES)"
	exit 1
fi

ind=0
for b in $BINS
do
	info "BIN$ind = $b"
	ind=$((ind+1))
done

ind=0
for d in $DICTFILES
do
	info "DICT$ind = $d"
	ind=$((ind+1))
done

info "ARGS = '$ARGS'"
info "TIME = $TIME"
info "N_PARALLEL_RUNS = $N_PARALLEL_RUNS"
info "N_RUNS = $N_RUNS"
info "USED_CPUS = $USED_CPUS"
info "T_INSTANCES = $T_INSTANCES"
info "N_INSTANCE = $N_INSTANCE"
info "N_PROC = $N_PROC"
info "USE_MASTER = $USE_MASTER"



# ceil
LOOP_N=$(((N_RUNS/N_PARALLEL_RUNS)))
if [ $((LOOP_N*N_PARALLEL_RUNS)) -lt $N_RUNS ]; then
	LOOP_N=$((LOOP_N+1))
fi

TIME_LEFT=$((LOOP_N*TIME))
warn "Time remaining: ${TIME_LEFT}h"


# disable UI
export AFL_NO_UI=1

for l in `seq 1 $LOOP_N`
do
	now=`date +"%T"`
	info "Start ($l): $now, duration: ${TIME}h"
	run_group $((l-1)) $AFLFUZZ "$BINS" "$ARGS" $INFOLDER $OUTFOLDER $N_PARALLEL_RUNS $N_INSTANCE "$DICTFILES" $TIME $USE_MASTER
done

exit 0
