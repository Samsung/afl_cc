#pragma once

#ifdef AFL_LLVM_RT
# define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
# define ASSERT(x) if (!(x)) { printf("assert( " #x " ) failed in file %s at line %u\n", __FILENAME__, __LINE__); exit(-1); }
#endif

static inline u32 get_bbmap_size(u32 size) {
  return (((size - 1) / 8) + 1);
}

static inline u32 get_map_size(u32 size) {
  ASSERT(size);
  size = (((size - 1) / 8) + 1);
  ASSERT((size <= (u32)(-1) / 8) && "Map size too large");
  return size * 8;
}

static inline void set_bit_from_bb_id(u8 * bb_trace_map, u32 trace_map_size, u32 bb_id) {
	u32 byte_n = bb_id / 8;
  u32 bit = (bb_id & 7);
  ASSERT(byte_n < trace_map_size); /* Sanity check things work as expected... */
  bb_trace_map[byte_n] |= (1 << bit);
}

static inline u8 get_bit_from_bb_id(u8 * bb_trace_map, u32 trace_map_size, u32 bb_id) {
	u32 byte_n = bb_id / 8;
  u32 bit = (bb_id & 7);
  ASSERT(byte_n < trace_map_size); /* Sanity check things work as expected... */
  return !!(bb_trace_map[byte_n] & (1 << bit));
}
