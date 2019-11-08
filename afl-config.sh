if [ -z "$LLVM_CONFIG" ]; then
	fatal "LLVM_CONFIG not defined"
fi

OPT=`$LLVM_CONFIG --bindir`/opt
LLVM_AS=`$LLVM_CONFIG --bindir`/llvm-as
LLVM_AR=`$LLVM_CONFIG --bindir`/llvm-ar
LLVM_LINK=`$LLVM_CONFIG --bindir`/llvm-link

OPT_ARGS="-internalize -internalize-public-api-list=main -globaldce -deadargelim -dse -die -argpromotion -disable-simplify-libcalls -inline -instcombine -loop-deletion -loop-unswitch -lowerswitch -memcpyopt -mem2reg -mergereturn"
OPT_ARGS_ARCHIVE="-globaldce -deadargelim -dse -die -argpromotion -disable-simplify-libcalls -inline -instcombine -loop-deletion -loop-unswitch -lowerswitch -memcpyopt -mem2reg -mergereturn"
