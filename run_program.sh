#!/bin/bash

LIB_DIR="$( cd "$(dirname "$0")" ; pwd -P )"
. $LIB_DIR/library.sh


# afl-fuzz
AFLFUZZ=$LIB_DIR/afl-fuzz
RUN_BENCHMARK=$LIB_DIR/run_benchmark.sh
N_RUNS=10 		# number of runs to get some statistical confidence :)
N_INSTANCE=1	# how many instances of the same afl do we run together
TIME=24			# time to run for, in hrs
USE_MASTER=0	# run one master. 0 means all instances run as slaves

run_one() 
{
	BIN1=$1
	BIN2=$2
	BIN3=$3
	ARGS=$4
	shift 
	shift 
	shift 
	shift
	info sh $RUN_BENCHMARK $AFLFUZZ 3 $BIN1 $BIN2 $BIN3 \"$ARGS\" "$@"
	sh $RUN_BENCHMARK $AFLFUZZ 3 $BIN1 $BIN2 $BIN3 "$ARGS" "$@"
}

# run_test()
# {
# 	run_one afl-fuzz "$@" in out 
# }

err_exit()
{
	err "$@"
	exit 1
}

run_program()
{
	EXE=$1
	IN=$2
	ARGS=$3
	OUT=out-$EXE/
	OLDDICT=$4
	BINDICT=$5

	cp $OLDDICT $EXE-old.dict || err_exit "Cannot copy old dict"
	if [ $BINDICT != "none" ]; then
		cp $BINDICT $EXE-auto.dict || err_exit "Cannot copy old dict"	
	fi
	
	
	if [ $USE_MASTER -eq 0 ]; then

		# original normalized no-collision-*-opt 
		run_one $EXE-original $EXE-normalized-opt $EXE-no-collision-all-opt "$ARGS" $N_RUNS $N_INSTANCE $IN o-n-c-all-opt $EXE-normalized.dict $EXE-normalized-opt.dict $EXE-no-collision-all-opt.dict $TIME $USE_MASTER || err_exit "7"
		run_one $EXE-original $EXE-normalized-opt $EXE-no-collision-notdict-opt "$ARGS" $N_RUNS $N_INSTANCE $IN o-n-c-nodict-opt $EXE-normalized.dict $EXE-normalized-opt.dict $EXE-no-collision-notdict-opt.dict $TIME $USE_MASTER || err_exit "8"
		run_one $EXE-original $EXE-normalized-opt $EXE-no-collision-none-opt "$ARGS" $N_RUNS $N_INSTANCE $IN o-n-c-none-opt $EXE-normalized.dict $EXE-normalized-opt.dict $EXE-no-collision-none-opt.dict $TIME $USE_MASTER || err_exit "9"

		# original normalized no-collision-all-noopt 
		run_one $EXE-original $EXE-normalized $EXE-no-collision-all-noopt "$ARGS" $N_RUNS $N_INSTANCE $IN o-n-c-all-none none none none $TIME $USE_MASTER || err_exit "4"
		run_one $EXE-original $EXE-normalized $EXE-no-collision-all-noopt "$ARGS" $N_RUNS $N_INSTANCE $IN o-n-c-all-noopt $EXE-normalized.dict $EXE-normalized.dict $EXE-no-collision-all-noopt.dict $TIME $USE_MASTER || err_exit "5"
		run_one $EXE-original $EXE-normalized $EXE-no-collision-all-noopt "$ARGS" $N_RUNS $N_INSTANCE $IN o-n-c-all-olddict $EXE-old.dict $EXE-old.dict $EXE-old.dict $TIME $USE_MASTER || err_exit "6"
		if [ $BINDICT != "none" ]; then
			run_one $EXE-original $EXE-normalized $EXE-no-collision-all-noopt "$ARGS" $N_RUNS $N_INSTANCE $IN o-n-c-all-autodict $EXE-auto.dict $EXE-auto.dict $EXE-auto.dict $TIME $USE_MASTER || err_exit "6"
		fi

		# original original original
		run_one $EXE-original $EXE-original $EXE-original "$ARGS" $N_RUNS $N_INSTANCE $IN o-o-o-oldict $EXE-old.dict $EXE-old.dict $EXE-old.dict $TIME $USE_MASTER || err_exit "1"
		run_one $EXE-original $EXE-original $EXE-original "$ARGS" $N_RUNS $N_INSTANCE $IN o-o-o-none none none none $TIME $USE_MASTER || err_exit "2"
		run_one $EXE-original $EXE-original $EXE-original "$ARGS" $N_RUNS $N_INSTANCE $IN o-o-o-newdict $EXE-normalized.dict $EXE-normalized.dict $EXE-normalized.dict $TIME $USE_MASTER || err_exit "3"
		if [ $BINDICT != "none" ]; then
			run_one $EXE-original $EXE-original $EXE-original "$ARGS" $N_RUNS $N_INSTANCE $IN o-o-o-autodict $EXE-auto.dict $EXE-auto.dict $EXE-auto.dict $TIME $USE_MASTER || err_exit "3"
		fi

		# original normalized no-collision-notdict-noopt 
		run_one $EXE-original $EXE-normalized $EXE-no-collision-notdict-noopt "$ARGS" $N_RUNS $N_INSTANCE $IN o-n-c-notdict-noopt $EXE-normalized.dict $EXE-normalized.dict $EXE-no-collision-notdict-noopt.dict $TIME $USE_MASTER || err_exit "10"

	else

		# original normalized no-collision-*-opt 
		run_one $EXE-original $EXE-normalized-opt $EXE-no-collision-all-opt "$ARGS" $N_RUNS $N_INSTANCE $IN o-n-c-all-opt none $EXE-normalized-opt.dict none $TIME $USE_MASTER || err_exit "7"
		run_one $EXE-original $EXE-normalized-opt $EXE-no-collision-notdict-opt "$ARGS" $N_RUNS $N_INSTANCE $IN o-n-c-nodict-opt none $EXE-normalized-opt.dict none $TIME $USE_MASTER || err_exit "8"
		run_one $EXE-original $EXE-normalized-opt $EXE-no-collision-none-opt "$ARGS" $N_RUNS $N_INSTANCE $IN o-n-c-none-opt none $EXE-normalized-opt.dict none $TIME $USE_MASTER || err_exit "9"

		# original normalized no-collision-all-noopt 
		run_one $EXE-original $EXE-normalized $EXE-no-collision-all-noopt "$ARGS" $N_RUNS $N_INSTANCE $IN o-n-c-all-none none none none $TIME $USE_MASTER || err_exit "4"
		run_one $EXE-original $EXE-normalized $EXE-no-collision-all-noopt "$ARGS" $N_RUNS $N_INSTANCE $IN o-n-c-all-noopt none $EXE-normalized.dict none $TIME $USE_MASTER || err_exit "5"
		run_one $EXE-original $EXE-normalized $EXE-no-collision-all-noopt "$ARGS" $N_RUNS $N_INSTANCE $IN o-n-c-all-olddict none $EXE-old.dict none $TIME $USE_MASTER || err_exit "6"
		if [ $BINDICT != "none" ]; then
			run_one $EXE-original $EXE-normalized $EXE-no-collision-all-noopt "$ARGS" $N_RUNS $N_INSTANCE $IN o-n-c-all-autodict $EXE-auto.dict $EXE-auto.dict $EXE-auto.dict $TIME $USE_MASTER || err_exit "6"
		fi

		# original original original
		run_one $EXE-original $EXE-original $EXE-original "$ARGS" $N_RUNS $N_INSTANCE $IN o-o-o-oldict none $EXE-old.dict none $TIME $USE_MASTER || err_exit "1"
		run_one $EXE-original $EXE-original $EXE-original "$ARGS" $N_RUNS $N_INSTANCE $IN o-o-o-none none none none $TIME $USE_MASTER || err_exit "2"
		run_one $EXE-original $EXE-original $EXE-original "$ARGS" $N_RUNS $N_INSTANCE $IN o-o-o-newdict none $EXE-normalized.dict none $TIME $USE_MASTER || err_exit "3"
		if [ $BINDICT != "none" ]; then
			run_one $EXE-original $EXE-original $EXE-original "$ARGS" $N_RUNS $N_INSTANCE $IN o-o-o-autodict none $EXE-auto.dict none $TIME $USE_MASTER || err_exit "3"
		fi

		# original normalized no-collision-notdict-noopt 
		run_one $EXE-original $EXE-normalized $EXE-no-collision-notdict-noopt "$ARGS" $N_RUNS $N_INSTANCE $IN o-n-c-notdict-noopt none $EXE-normalized.dict none $TIME  || err_exit "10"

	fi
}

# see compile_all.sh
# AFL_DICT_TYPE=NORMAL AFL_CONVERT_COMPARISON_TYPE=NONE AFL_COVERAGE_TYPE=ORIGINAL ../aflc-clang-fast -O3 <exe>.bc -o <exe>-original
# AFL_DICT_TYPE=NORMAL AFL_CONVERT_COMPARISON_TYPE=NONE AFL_COVERAGE_TYPE=ORIGINAL ../aflc-clang-fast <exe>.bc -o <exe>-normalized
# AFL_DICT_TYPE=NORMAL AFL_CONVERT_COMPARISON_TYPE=ALL AFL_COVERAGE_TYPE=NO_COLLISION ../aflc-clang-fast <exe>.bc -o <exe>-no-collision-all-noopt
# AFL_DICT_TYPE=NORMAL AFL_CONVERT_COMPARISON_TYPE=NOT_DICT AFL_COVERAGE_TYPE=NO_COLLISION ../aflc-clang-fast <exe>.bc -o <exe>-no-collision-notdict-noopt
# AFL_DICT_TYPE=OPTIMIZED AFL_CONVERT_COMPARISON_TYPE=ALL AFL_COVERAGE_TYPE=NO_COLLISION ../aflc-clang-fast <exe>.bc -o <exe>-no-collision-all-opt
# AFL_DICT_TYPE=OPTIMIZED AFL_CONVERT_COMPARISON_TYPE=NOT_DICT AFL_COVERAGE_TYPE=NO_COLLISION ../aflc-clang-fast <exe>.bc -o <exe>-no-collision-notdict-opt


if [ "$#" -ne 5 ]; then
	err "Illegal number of parameters"
	err "$0 </path/to/exe-base> <args> <in> <hand-crafted-dict> <bin-extracted-dict>"
	err "Example: $0 /path/to/readelf-afl" \"-a\" ./in/ /path/to/hand-crafted-dict.dict /path/to/bin-extracted-dict.dict
	exit 1
fi

EXE=$1
ARGS=$2
IN=$3
OLDDICT=$4
BINDICT=$5

OLDPWD=$PWD

if ! regular_file_exists $OLDDICT; then
	err_exit "'$OLDDICT' does not exist"
fi


if [ $BINDICT != "none" ]; then
	if ! regular_file_exists $BINDICT; then
		err_exit "'$BINDICT' does not exist"
	fi
fi

# setup env for AFL
echo core | sudo tee /proc/sys/kernel/core_pattern 1>/dev/null
value=`cat /proc/sys/kernel/core_pattern`
if [ $value != "core" ];
then
	err_exit "Cannot setup /proc/sys/kernel/core_pattern"
fi

cd /sys/devices/system/cpu 1>/dev/null || err_exit "cd /sys/devices/system/cpu"
echo performance | sudo tee cpu*/cpufreq/scaling_governor 1>/dev/null || err_exit "Echo performance"

ulimit -s unlimited || err_exit "ulimit"

cd $OLDPWD || err_exit "cd $OLDPWD"

run_program $EXE $IN "$ARGS" "$OLDDICT" "$BINDICT"
