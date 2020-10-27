FROM ubuntu:16.04

# Prerequisites
RUN apt-get update && \
    apt-get -y upgrade && \
    apt-get install -y git build-essential wget make gcc cmake texinfo bison software-properties-common binutils-dev && \
    apt-get clean

ENV AFL_CONVERT_COMPARISON_TYPE=NONE \
    AFL_COVERAGE_TYPE=ORIGINAL \
    AFL_BUILD_TYPE=FUZZING \
    AFL_DICT_TYPE=NORMAL \
    AFL_ROOT=/afl_cc

######################################################
# Quick Installation (supports fuzzing only)
RUN apt-get install -y llvm-3.8 clang-3.8
ENV LLVM_CONFIG=/usr/bin/llvm-config-3.8
RUN mkdir -p afl_cc
COPY . afl_cc
WORKDIR /afl_cc
RUN make
RUN cd llvm_mode && make

####################################################
# # Long Installation (supports both fuzzing and coverage extraction)
# RUN mkdir -p afl_cc
# COPY . afl_cc
# ## gold plugin
# RUN rm /usr/bin/ld && ln -s /usr/bin/ld.gold /usr/bin/ld
# RUN git clone https://github.com/llvm-mirror/llvm.git -b release_38 --single-branch --depth 1
# RUN cd llvm/tools && git clone https://github.com/llvm-mirror/clang.git -b release_38 --single-branch --depth 1
# RUN cp -R /afl_cc/clang_format_fixes/clang/* /llvm/tools/clang/
# RUN mkdir -p /llvm/build
# WORKDIR /llvm/build
# RUN cmake -DLLVM_BINUTILS_INCDIR=/usr/include -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD="X86" ../ -DCMAKE_CXX_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=0"
# RUN cmake --build . -- -j20
# ENV LLVM_BINDIR=/llvm/build/bin \
#     LLVM_CONFIG=/llvm/build/bin/llvm-config

# ## build AFL
# WORKDIR /afl_cc
# RUN make
# RUN cd llvm_mode && make
# RUN cd clang_rewriters/ && make
# RUN cd dsa/lib/DSA && make
# RUN cd dsa/lib/AssistDS && make

#######################################################

# Setup the aflc-gclang compiler
RUN sh setup-aflc-gclang.sh

VOLUME ["/data"]
WORKDIR /data