#
# american fuzzy lop - LLVM instrumentation
# -----------------------------------------
#
# Written by Laszlo Szekeres <lszekeres@google.com> and
#            Michal Zalewski <lcamtuf@google.com>
#
# LLVM integration design comes from Laszlo Szekeres.
#
# Copyright 2015, 2016 Google Inc. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at:
#
#   http://www.apache.org/licenses/LICENSE-2.0
#

ifeq ($(LLVM_CONFIG),)
  $(error LLVM_CONFIG is not set)
endif


CLANG_CFL    = `$(LLVM_CONFIG) --cxxflags` $(CXXFLAGS)
CLANG_LFL    = `$(LLVM_CONFIG) --ldflags` $(LDFLAGS)
LLVM_VERSION = `$(LLVM_CONFIG) --version`


ifeq "$(origin CC)" "default"
  CC         = `$(LLVM_CONFIG) --bindir`/clang
  CXX        = `$(LLVM_CONFIG) --bindir`/clang++
endif


INCLUDES	 = -I../../include
SOURCES  	 = $(shell find . -type f -name '*.cpp')
OBJECTS  	 = $(SOURCES:.cpp=.o)

SHARED_LIB	 = ../../LLVMDataStructure.so


all: $(SHARED_LIB)


$(SHARED_LIB): $(OBJECTS)
	$(CXX) $(INCLUDES) $(CLANG_CFL) -shared $^ -o $@ $(CLANG_LFL)

%.o: %.cpp 
	$(CXX) $(INCLUDES) $(CLANG_CFL) -c $< -o $@

# %.o: %.cpp
# 	$(CXX) -c $(CPPFLAGS) $< -o $@

clean:
	rm -f *.o *.so *~
	rm -f $(SHARED_LIB)
