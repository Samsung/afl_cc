# Fuzzing with controlled compilation

This is the repo for the code we used in the paper [Improving Fuzzing through Controlled Compilation](controlled_compilation.pdf). It is built using [AFL fuzzer](https://github.com/google/AFL). There's a lot of commands on this page, and not everything will make sense :). If you have questions, don't hesitate to contact me!

Prerequesites (tested on Ubuntu 16.04.6 LTS)
-------------------------------------------
```console
me@machine:$ sudo apt-get install make gcc cmake texinfo bison
me@machine:$ export AFL_CONVERT_COMPARISON_TYPE=NONE -- this is explained later
me@machine:$ export AFL_COVERAGE_TYPE=ORIGINAL -- this is explained later
me@machine:$ export AFL_BUILD_TYPE=FUZZING -- this is explained later
me@machine:$ export AFL_DICT_TYPE=NORMAL -- this is explained later
```

Quick Installation (supports fuzzing only):
----------------------------------------------
```console
me@machine:$ sudo apt-get install llvm-3.8 clang-3.8
me@machine:$ export LLVM_CONFIG=`which llvm-config-3.8`
me@machine:$ git clone https://github.com/Samsung/afl_cc.git && cd afl_cc
me@machine:$ export AFL_ROOT=$PWD
me@machine:$ make
me@machine:$ cd $AFL_ROOT/llvm_mode && make && cd -
# if this fails with error "/usr/bin/ld: unrecognized option '--no-keep-files-mapped'", install gold linker
```
Long Installation (supports both fuzzing and coverage extraction):
--------------------------------------------------------------------
```console
me@machine:$ git clone https://github.com/Samsung/afl_cc.git && cd afl_cc
me@machine:$ export AFL_ROOT=$PWD
# support for gold plugin
me@machine:$ ln -s /usr/bin/ld.gold /usr/bin/ld # Note: you may need to rm /usr/bin/ld first
# build clang/llvm 3.8
me@machine:$ cd ../
me@machine:$ git clone https://github.com/llvm-mirror/llvm.git -b release_38 --single-branch --depth 1
me@machine:$ cd llvm/tools
me@machine:$ git clone https://github.com/llvm-mirror/clang.git -b release_38 --single-branch --depth 1
me@machine:$ cp -R ../../afl_cc/clang_format_fixes/clang/* clang/ # apply some patches
me@machine:$ cd ..
me@machine:$ mkdir build && cd build
# remove/update -DLLVM_TARGETS_TO_BUILD="X86" if you're building for a different architecture 
me@machine:$ cmake -DLLVM_BINUTILS_INCDIR=/usr/include -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD="X86" ../ -DCMAKE_CXX_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=0"
me@machine:$ cmake --build .
me@machine:$ cd bin && export LLVM_BINDIR=$PWD
me@machine:$ export LLVM_CONFIG=$LLVM_BINDIR/llvm-config
# build AFL
me@machine:$ cd $AFL_ROOT
me@machine:$ make clean && make
me@machine:$ cd llvm_mode && make && cd -
me@machine:$ cd clang_rewriters/ && make && cd -
me@machine:$ cd dsa/lib/DSA && make && cd -
me@machine:$ cd dsa/lib/AssistDS && make && cd -
```

Setup the aflc-gclang compiler (to generate .bc file):
-----------------------------------------------------
```console
me@machine:$ cd $AFL_ROOT
me@machine:$ sh setup-aflc-gclang.sh
```

Control the compilation of the program to generate thru:
--------------------------------------------------------
	1. AFL_CONVERT_COMPARISON_TYPE=
		ALL = will transform ALL comparisons to bytes comparison
		NONE = will transform NO comparisons to bytes comaprison
		NO_DICT = will transform comparison that are *not* added to dictionary.
	2. AFL_COVERAGE_TYPE=
		ORIGINAL = original LLVM pass using edges calculated using BB IDs
		NO_COLLISION = new pass that removes collision (binary will run slower).
	3. AFL_BUILD_TYPE=
		COVERAGE = to generate a coverage build. To be used along with aflc-gclang-cov
		FUZZING = to generate a build used for fuzzing. To be used along with aflc-gclang
	4. AFL_DICT_TYPE=
		OPTIMIZED = the generated .dict and the binary will use an optimized dictionary
		NORMAL = normal dictionary, ie like vanilla AFL
	5. For example, to compile like the original AFL, set:
		AFL_COVERAGE_TYPE=ORIGINAL
		AFL_CONVERT_COMPARISON_TYPE=NONE
		AFL_BUILD_TYPE=FUZZING
		AFL_DICT_TYPE=NORMAL
	6. For example, to generate with optimized dictionary and break down conditions only if not in dictionary:
		AFL_COVERAGE_TYPE=NO_COLLISION
		AFL_CONVERT_COMPARISON_TYPE=NO_DICT
		AFL_BUILD_TYPE=FUZZING
		AFL_DICT_TYPE=OPTIMIZED
    7. For example, to generate with optimized dictionary and break down all conditions:
		AFL_COVERAGE_TYPE=NO_COLLISION
		AFL_CONVERT_COMPARISON_TYPE=ALL
		AFL_BUILD_TYPE=FUZZING
		AFL_DICT_TYPE=OPTIMIZED
	8. For example, to generate a coverage build:
		AFL_COVERAGE_TYPE=ORIGINAL
		AFL_CONVERT_COMPARISON_TYPE=NONE
		AFL_BUILD_TYPE=COVERAGE
		AFL_DICT_TYPE=NORMAL -- note: this won't generate a dict file

Example 1: program compilation:
------------------------------
Consider the following example code, call it test.c:
```c
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char *argv[]) {

  int fd;
  unsigned char buf[45];
  
  if (!(argc>1)) {
    printf("missing arg\n");
    return -2;
  }
  
  fd = open(argv[1], O_RDONLY);
  if (fd==-1) {
    printf("err open %s\n", strerror(errno));
    return -1;
  }

  // may read less than 45 bytes
  read(fd, buf, 45);

  if (buf[0] == 1 && buf[1] == 2 && buf[2] == 3 && buf[3] == 4 && buf[4] == 5 && buf[5] == 6)
    abort();

  close(fd);

  return 0;
}

```

To compile a program for fuzzing:
1. Make sure variable LLVM_CONFIG is set appropriately, as explained in previous sections.
2. Set your compiler to $AFL_ROOT/aflc-gclang ($AFL_ROOT/aflc-gclang for C++ programs). For example with autotools you may do:

```console
me@machine:$ CC=$AFL_ROOT/aflc-gclang ./configure
```
    
In our example, we just invoke it directly as:

```console
me@machine:$ $AFL_ROOT/aflc-gclang test.c -o test
```

aflc-gclang is just a wrapper around [gllvm](https://github.com/SRI-CSL/gllvm), so the following steps should work as long as gllvm does :)
    
3. Extract the bitcote file (.bc) from the executable:

```console
me@machine:$ $AFL_ROOT/aflc-get-bc test
```

Upon success, you will see a corresponding test.bc file. If this fails, hopefully you'll get a comprehensible error message. If not, let me know.

4. Finish the compilation by invoking aflc-clang-fast (instead of the usual afl-clang-fast that AFL uses). For example, let's generate 3 builds: 1) one with optimizations (as used by vanilla AFL), 2) one build with controlled compilation (ie, with certain optimizations on and other off), and 3) a build with controlled compilation + byte splitting (ie break multi-byte comparisons into a series of single-byte comparisons) + optimized dictionary (ie dictionary with magic values and the ID of the basic block where the value is used):

The first build is the same as used by AFL with optimizations enabled (-O3):
```console
me@machine:$ AFL_COVERAGE_TYPE=ORIGINAL AFL_CONVERT_COMPARISON_TYPE=NONE AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=NORMAL $AFL_ROOT/aflc-clang-fast -O3 test.bc -o test-afl-original
```

You should see a message saying 'WARNING: No coverage file generated', which is normal since we are generating a binary for fuzzing, not to extract coverage information. We will see later how to generate and use coverage files.

You should also see a message saying 'WARNING: No dictionary was generated' which means no dictionary was automatically generated. This is normal because the generation is only supported when when no optimizations are enabled.

The second build is the one with controlled compilation (we call this build 'normalized'):
```console
me@machine:$ AFL_COVERAGE_TYPE=ORIGINAL AFL_CONVERT_COMPARISON_TYPE=NONE AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=NORMAL $AFL_ROOT/aflc-clang-fast test.bc -o test-afl-normalized
```

You should also see 'INFO: Dictionary file generated as test-afl-normalized.dict', which means a dictionary file was generated. This will be used for fuzzing. Open the file:
```
# AFL_DICT_TYPE=NORMAL; AFL_COVERAGE_TYPE=ORIGINAL; AFL_BUILD_ID=0fb01607cf2302f9
AFL_C2U_test_c_28_00000005="\x02"
AFL_C2U_test_c_28_00000006="\x03"
AFL_C2U_test_c_28_00000007="\x04"
AFL_C2U_test_c_28_00000008="\x05"
AFL_C2U_test_c_28_00000009="\x06"
```

You note that the hardcoded values used in if-statements in the source code each appear in the dictionary.

The third build is the one with controlled compilation + byte splitting + optimized dictionary:
```console
me@machine:$ AFL_COVERAGE_TYPE=NO_COLLISION AFL_CONVERT_COMPARISON_TYPE=ALL AFL_BUILD_TYPE=FUZZING AFL_DICT_TYPE=OPTIMIZED $AFL_ROOT/aflc-clang-fast test.bc -o test-afl-no-collision-all-opt
```

Open the ditcionary file test-afl-no-collision-all-opt.dict:
```
# AFL_DICT_TYPE=OPTIMIZED; AFL_COVERAGE_TYPE=NO_COLLISION; AFL_BUILD_ID=d91ff835ab545a68
AFL_C2U_test_c_28_00000009="\x02"
...
AFL_C2U_test_c_28_0000000b="\x03"
...
AFL_C2U_test_c_28_0000000d="\x04"
...
AFL_C2U_test_c_28_00000001="\x05"
...
AFL_C2U_test_c_28_00000001="\x06"
...

```

Let's examine the fields, for example AFL_C2U_test_c_28_00000009: test_c_28 means the magic value 0x2 was found in the file test.c at line 28. 00000009 is the unique ID of the basic block which AFL will use during fuzzing to select relevent values.

As you can see, you may mix and match the AFL macros (AFL_COVERAGE_TYPE, AFL_CONVERT_COMPARISON_TYPE, AFL_BUILD_TYPE and AFL_DICT_TYPE) as you wish to generate the build you want. Since this is not fun and error prone, there is a script you can use to do this for you:

```console
# Note: there's a compile_program++.sh file for C++ programs
me@machine:$ sh $AFL_ROOT/compile_program.sh ./test.bc "" ""
```

The first parameters is the <file.bc>, second is linking flags (none in our case), and the last parameters is arguments to LLVM passes (none in our case). Note that this last parameter only supports something like "-mllvm -fignore-strings-to=func1,func2", where each func1,func2,etc is a function name for which hardcoded arguments should be excluded during dictionary generation. For example if there is a function foo() that prints to stdin (eg foo("hello world")), we may pass "-mllvm -fignore-strings-to=foo" to ignore hardcoded strings from being added to the dictionary. 

After issuing the command above, you should now have a bunch of new files in your directory, each starting with 'test-afl-'. The naming is not great... you can open $AFL_ROOT/compile_program.sh to see what they mean (I've added comments). If you cannot figure it out, drop me a message :) 

Example 2: running the fuzzer manually:
--------------------------------------
Let's see how to run the 3 builds we generated in the previous section. There's nothing really new here, it's just like running the original AFL. First let's create a starting seed for AFL to use:
```console
me@machine:$ mkdir in && echo hello >in/hello # create an initial seed
```

Then open 3 terminals:

Terminal 1:
```console
me@machine:$ $AFL_ROOT/afl-fuzz -m none -i ./in/ -o C_OD_FBSP -S test-afl-original -x ./test-afl-normalized.dict ./test-afl-original @@
```
Terminal 2:
```console
me@machine:$ $AFL_ROOT/afl-fuzz -m none -i ./in/ -o C_OD_FBSP -S test-afl-normalized-opt -x ./test-afl-normalized-opt.dict ./test-afl-normalized-opt @@
```
Terminal 3:
```console
me@machine:$ $AFL_ROOT/afl-fuzz -m none -i ./in/ -o C_OD_FBSP -S test-afl-no-collision-all-opt -x ./test-afl-no-collision-all-opt.dict ./test-afl-no-collision-all-opt @@
```

Example 3: running the fuzzer thru scripts:
------------------------------------------
There is a script to run 3 AFL parallel instances using a combination of the binaries generated by the compile_program.sh script. The script is called run_program.sh (This is the script we used to run the experiments in the paper). At the top of the file you will see:
```
N_RUNS=10 # this means we will run each fuzzer 10 times. If you're trying to extract statically significant info from the results of each run, then you may use 10 or even more, like 30.
TIME=24 This is the time in hours to run the fuzzer for.
```
You can change those for your needs. The script will run most of the configurations presented in the paper, except a few that needed some manual changes. If you want to run some of these missing configurations, drop me a message. Let's run the script to see what happens:

```console
me@machine:$ mkdir in && echo hello >in/hello # create an initial seed
me@machine:$ echo '"#"' > handcrafted.dict # a manually-generated dictionary with one entry
me@machine:$ sh $AFL_ROOT/run_program.sh ./test-afl "" ./in/ handcrafted.dict none
```

This will take a while...several weeks if not months in fact :). If you just want to run a quick example instead, use the modified script run_program_example.sh that will run 10 independent runs of one configuration only (C_OD_FBSP) for just 30 seconds each. The following command should take a few minutes to complete:

```console
me@machine:$ rm -rf o-n-c-all-opt && rm -rf C_OD_FBSP # cleanup directories created before
me@machine:$ sh $AFL_ROOT/run_program_example.sh ./test-afl "" ./in/ handcrafted.dict none
```

After completion, there will be a new folder called o-n-c-all-opt: this is the configuration C_OD_FBSP. The name is different because before writing the paper, we used different names in our scripts... Not ideal... but hey!

Let's check what is under o-n-c-all-opt: You should see at least 10 folders (since N_RUNS=10), each containing the result of an independent run. For example there's one folder called 0-1: it contains 3 subfolders that each corresponds to an AFL instance. Each of these instances ran in parallel for 30 seconds and shared their seeds. Each of these subfolders has a weird-looking name generated by our script. It starts with the name of the binary that was run with some additional numbers to make the subfolder unique. For example there is one called test-afl-no-collision-all-opt-1-3, which ran the test-afl-no-collision-all-opt binary. You can verify this by using:
```console
me@machine:$ grep command_line C_OD_FBSP/0-1/test-afl-no-collision-all-opt-1-3/fuzzer_stats
			 command_line      : /home/me/afl_cc/afl-fuzz -m none -i ./in/ -o o-n-c-all-opt/0-1 -S test-afl-no-collision-all-opt-1-3 -x ./test-afl-no-collision-all-opt.dict ./test-afl-no-collision-all-opt @@
```

This is the same configuration we ran manually in the previous section.

Example 3: extracting coverage information:
-------------------------------------------
If you want to extract coverage information (eg lines visited by the fuzzer), then this section is for you. If you only care about bugs found by the fuzzer, then don't bother :) First, make sure you follow the 'Long Installation' instructions from the beginning of this document. Then, the steps are similar to what we did early.

To compile a program to extract coverage information:
1. Make sure variable LLVM_CONFIG is set appropriately, as explained in previous sections.
2. Set your compiler to $AFL_ROOT/aflc-gclang-cov ($AFL_ROOT/aflc-gclang-cov++ for C++ programs). For example with autotools you may do:

```console
me@machine:$ CC=$AFL_ROOT/aflc-gclang-cov ./configure
```
    
For our toy example, we just invoke it directly as:

```console
me@machine:$ $AFL_ROOT/aflc-gclang-cov test.c -o test-coverage
```

Under the hood, this creates a file called 'normalized_test.c' which normalizes the file so that each statement/condition appears on a unique line. This way we can tell apart fuzzers that pass each of these conditions. Open the file:
```c
[...]
int main(int argc, char *argv[]) {

  [...]
  // each condition is a a seprate line
  if (buf[0] == 1 &&
      buf[1] == 2 &&
      buf[2] == 3 &&
      buf[3] == 4 &&
      buf[4] == 5 &&
      buf[5] == 6)
    abort();

  close(fd);

  return 0;
}

```

WARNING: the re-factoring of the code is implemented with a combination of custom Clang plugins and clang-format. During our tests, certain inputs would crash clang-format. To get around this problem, we patched clang-format and fiddled with several options. Unfortunately, this quick-and-dirty fix is unlikely to always work :( So if you encounter problems using $AFL_ROOT/aflc-gclang-cov, don't panic and reach out!

3. Extract the bitcote file (.bc) from the executable:

```console
me@machine:$ $AFL_ROOT/aflc-get-bc test-coverage
```

Upon success, you will see a corresponding test-coverage.bc file. If this fails, hopefully you'll get a comprehensible error message. If not, let me know.

4. Finish the compilation by invoking aflc-clang-fast with coverage configuration (instead of the usual afl-clang-fast that AFL uses):

```console
me@machine:$ AFL_COVERAGE_TYPE=ORIGINAL AFL_CONVERT_COMPARISON_TYPE=NONE AFL_BUILD_TYPE=COVERAGE $AFL_ROOT/aflc-clang-fast test-coverage.bc -o test-coverage # WARNING: this replaces the test-coverage already present
```

You will see a message 'WARNING: No dictionary was generated', which is normal because we're generating a build for coverage, so we don't need a dictionary.

You will also see the message 'INFO: Mapping (BB<->SRC) file generated as test-coverage.c2s', which contains the mapping between basic block IDs and line numbers (note that these basic block IDs are separate from the ones used by AFL for edge calculation). Open it:
```
0=/home/me/test.c:1442
1=/home/me/test.c:1443,/home/me/test.c:1444
2=/home/me/test.c:1447,/home/me/test.c:1448
3=/home/me/test.c:1449,/home/me/test.c:1450
4=/home/me/test.c:1453,/home/me/test.c:1455
5=/home/me/test.c:1456
6=/home/me/test.c:1457
7=/home/me/test.c:1458
8=/home/me/test.c:1459
9=/home/me/test.c:1455,/home/me/test.c:1460
10=/home/me/test.c:1461
11=/home/me/test.c:1463,/home/me/test.c:1465
12=/home/me/test.c:1466
```

To run the extraction, use afl-coverage as:

```console
me@machine:$ AFL_NO_UI=1 AFL_NO_CAL=1 AFL_SKIP_CRASHES=1 $AFL_ROOT/afl-coverage -m none -i INFOLDER -o OUTFOLDER ./test-coverage @@
# for example for our toy example if we want to extract the coverage for one run (name '0-1') of the three parallel instances:
me@machine:$ mkdir C_OD_FBSP/0-1/incov
me@machine:$ cp C_OD_FBSP/0-1/test-afl-no-collision-all-opt-1-3/queue/* C_OD_FBSP/0-1/incov/
me@machine:$ cp C_OD_FBSP/0-1/test-afl-no-collision-all-opt-1-3/crashes/* C_OD_FBSP/0-1/incov/
me@machine:$ cp C_OD_FBSP/0-1/test-afl-normalized-opt-1-2/queue/* C_OD_FBSP/0-1/incov/
me@machine:$ cp C_OD_FBSP/0-1/test-afl-normalized-opt-1-2/crashes/* C_OD_FBSP/0-1/incov/
me@machine:$ cp C_OD_FBSP/0-1/test-afl-original-1-1/queue/* C_OD_FBSP/0-1/incov/
me@machine:$ cp C_OD_FBSP/0-1/test-afl-original-1-1/crashes/* C_OD_FBSP/0-1/incov/ # this will probably throw an error as no crashes were found: just ignore
me@machine:$ AFL_NO_UI=1 AFL_NO_CAL=1 AFL_SKIP_CRASHES=1 $AFL_ROOT/afl-coverage -m none -i C_OD_FBSP/0-1/incov -o C_OD_FBSP/0-1/outcov ./test-coverage @@
```

You can find the coverage information in the file C_OD_FBSP/0-1/outcov/coverage_bitmap. Each bit set to 1 corresponds to a basic bock ID visited. Once you have the BB ID, you can lookup which lines it corresponds to using the test-coverage.c2s file.

Let's see what the coverage file says:
```console
me@machine:$ xxd -b C_OD_FBSP/0-1/outcov/coverage_bitmap
             00000000: 11110101 00011111
```

Each bit corresponds to a basic block. Basic block 0 is represented by bit 0 and is the right-most bit of the first byte. Similarly, basic block 1 is represented by bit 1 and is the second right-most bit of the first byte. Basic block 8 is represented by bit 8 and is the right-most bit of the second byte (functions for setting and getting the bit is in file [utils.h](blob/master/utils.h)). Let's look at some examples. In the coverage_bitmap file, bit 0 is set, which, according to the test-coverage.c2s file, corresponds to line 1442. So far so good. WARNING: the line numbers correspond to the normalized file normalized_test.c, not the original test.c!
```
0=/home/me/test.c:1442
```

Bit 1 and 3 are not set. Bit 1 corresponds to the basic block of lines 1443 and 1444:
```
1=/home/me/test.c:1443,/home/me/test.c:1444
```
which correspond to
```c
    printf("missing arg\n");
    return -2;
```
This makes sense: AFL was run with an input file as argument, so these lines were never visited. Similarily, bit 3 corresponds to lines 1449 and 1450:
```
3=/home/me/test.c:1449,/home/me/test.c:1450
```
which corresponds to the code 
```c
    printf("err open %s\n", strerror((*__errno_location())));
    return -1;
```
in normalized_test.c: since AFL was always able to open the file, these lines were never visited.

Example 4: using the scripts to extract/plot coverage:
-----------------------------------------------------
You need to use the file run_cov.py. All the scripts expect the output be generated by the run_program.sh script...
Drop me a message and I should be able to help out :)
