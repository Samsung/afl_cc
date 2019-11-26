#!/bin/sh

LIB_DIR="$( cd "$(dirname "$0")" ; pwd -P )"
. $LIB_DIR/library.sh

if [ "$#" -lt 3 ]; then
	err "Illegal number of parameters"
	err "$0 <exe.bc> <linker_options> <optional:pass_args>"
	err "$0 objdump.bc \"-ldl -lz\" \"-mllvm pass_args\""
	exit 1
fi

EXEBC=$1
shift
LDFLAGS="$1"
shift
LLVM_PASS_ARGS="$1" # note: may be empty

#${@}
CC=`basename $0`
if [ $CC = "compile_program.sh" ]; then
	BCLANG_FAST=$LIB_DIR/aflc-clang-fast
elif [ $CC = "compile_program++.sh" ]; then
	BCLANG_FAST=$LIB_DIR/aflc-clang-fast++
else
	fatal "Invalid filename '$CC'"
fi



if ! regular_file_exists $BCLANGPP_FAST; then
	err "'$BCLANGPP_FAST' does not exist"
	exit 1
fi

END=`echo -n $EXEBC | tail -c 3`
if [ $END != ".bc" ]; then
	err "Invalid extension for '$EXEBC'. Expecting .bc"
	exit 1
fi

if ! regular_file_exists $EXEBC; then
	err "'$EXEBC' does not exist"
	exit 1
fi

EXE=`echo $EXEBC | rev | cut -c 4- | rev`-afl

err_exit()
{
	err "$@"
	exit 0
}

# TODO: coverage: for now just -reuse $EXE-no-collision-none-noopt

# original AFL build, ie with -O3
info  "AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=NORMAL AFL_CONVERT_COMPARISON_TYPE=NONE AFL_COVERAGE_TYPE=ORIGINAL $BCLANG_FAST -O3 $EXEBC -o $EXE-original $LDFLAGS"
AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=NORMAL AFL_CONVERT_COMPARISON_TYPE=NONE AFL_COVERAGE_TYPE=ORIGINAL $BCLANG_FAST -O3 $EXEBC -o $EXE-original $LDFLAGS || err_exit "Failed 2"

# normalized, different coverage, normal dict (not used anyway)
info  "AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=NORMAL AFL_CONVERT_COMPARISON_TYPE=NONE AFL_COVERAGE_TYPE=ORIGINAL $BCLANG_FAST $EXEBC $LLVM_PASS_ARGS -o $EXE-normalized $LDFLAGS"
AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=NORMAL AFL_CONVERT_COMPARISON_TYPE=NONE AFL_COVERAGE_TYPE=ORIGINAL $BCLANG_FAST $EXEBC $LLVM_PASS_ARGS -o $EXE-normalized $LDFLAGS || err_exit "Failed 3"

# normalized, no collision coverage, normal dictionary, break down conditions (all or only those not in dictionary)
info  "AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=NORMAL AFL_CONVERT_COMPARISON_TYPE=ALL AFL_COVERAGE_TYPE=NO_COLLISION $BCLANG_FAST $EXEBC $LLVM_PASS_ARGS -o $EXE-no-collision-all-noopt $LDFLAGS"
AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=NORMAL AFL_CONVERT_COMPARISON_TYPE=ALL AFL_COVERAGE_TYPE=NO_COLLISION $BCLANG_FAST $EXEBC $LLVM_PASS_ARGS -o $EXE-no-collision-all-noopt $LDFLAGS || err_exit "Failed 4"
info  "AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=NORMAL AFL_CONVERT_COMPARISON_TYPE=NOT_DICT AFL_COVERAGE_TYPE=NO_COLLISION $BCLANG_FAST $EXEBC $LLVM_PASS_ARGS -o $EXE-no-collision-notdict-noopt $LDFLAGS"
AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=NORMAL AFL_CONVERT_COMPARISON_TYPE=NOT_DICT AFL_COVERAGE_TYPE=NO_COLLISION $BCLANG_FAST $EXEBC $LLVM_PASS_ARGS -o $EXE-no-collision-notdict-noopt $LDFLAGS || err_exit "Failed 5"

# normalized, no collision coverage, optimized dictionary, break down conditions (all or only those not in dictionary)
info "AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=OPTIMIZED AFL_CONVERT_COMPARISON_TYPE=NONE AFL_COVERAGE_TYPE=NO_COLLISION $BCLANG_FAST $EXEBC $LLVM_PASS_ARGS -o $EXE-no-collision-none-opt $LDFLAGS"
AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=OPTIMIZED AFL_CONVERT_COMPARISON_TYPE=NONE AFL_COVERAGE_TYPE=NO_COLLISION $BCLANG_FAST $EXEBC $LLVM_PASS_ARGS -o $EXE-no-collision-none-opt $LDFLAGS || err_exit "Failed 6"
info "AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=OPTIMIZED AFL_CONVERT_COMPARISON_TYPE=ALL AFL_COVERAGE_TYPE=NO_COLLISION $BCLANG_FAST $EXEBC $LLVM_PASS_ARGS -o $EXE-no-collision-all-opt $LDFLAGS"
AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=OPTIMIZED AFL_CONVERT_COMPARISON_TYPE=ALL AFL_COVERAGE_TYPE=NO_COLLISION $BCLANG_FAST $EXEBC $LLVM_PASS_ARGS -o $EXE-no-collision-all-opt $LDFLAGS || err_exit "Failed 7"
info "AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=OPTIMIZED AFL_CONVERT_COMPARISON_TYPE=NOT_DICT AFL_COVERAGE_TYPE=NO_COLLISION $BCLANG_FAST $EXEBC $LLVM_PASS_ARGS -o $EXE-no-collision-notdict-opt $LDFLAGS"
AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=OPTIMIZED AFL_CONVERT_COMPARISON_TYPE=NOT_DICT AFL_COVERAGE_TYPE=NO_COLLISION $BCLANG_FAST $EXEBC $LLVM_PASS_ARGS -o $EXE-no-collision-notdict-opt $LDFLAGS || err_exit "Failed 8"

# normalized, original coverage, optimized dictionary, no break down conditions
info "AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=OPTIMIZED AFL_CONVERT_COMPARISON_TYPE=NONE AFL_COVERAGE_TYPE=ORIGINAL $BCLANG_FAST $EXEBC $LLVM_PASS_ARGS -o $EXE-normalized-opt $LDFLAGS"
AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=OPTIMIZED AFL_CONVERT_COMPARISON_TYPE=NONE AFL_COVERAGE_TYPE=ORIGINAL $BCLANG_FAST $EXEBC $LLVM_PASS_ARGS -o $EXE-normalized-opt $LDFLAGS || err_exit "Failed 9"


# TODO: original coverage + no comparison + optimized dict
# new coverage + no comparison + optimized dict