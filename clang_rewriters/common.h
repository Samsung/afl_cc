#pragma once

#include <unistd.h>
#include <errno.h>

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define ASSERT(x) if (!(x)) {errs() << "assert( " << #x << " ) failed in file " << __FILENAME__ << " at line " << __LINE__ << "\n"; exit(-1); }