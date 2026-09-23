// Stubs so the pure-logic tests link against mp4_progressive_builder.o without the nginx runtime.
// mp4_progressive_builder.o pulls in ngx_palloc (vod_alloc), mp4_init_segment_build_ex (called by
// the moov assembler) and ngx_log_error_core (error-path logging) - none are exercised on the
// success paths the tests assert, so trivial stand-ins are enough.
#include <stdlib.h>
void* ngx_palloc(void* pool, unsigned long size) { (void)pool; return malloc(size); }
int mp4_init_segment_build_ex() { return -1; }
void ngx_log_error_core() {}
