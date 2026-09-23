// Standalone unit test for the mdat streaming writer (mp4_progressive_mdat_writer_*).
// It fabricates a 2-clip x 2-track media_set backed by an in-memory frames_source, runs the writer
// to completion, and asserts:
//   - the bytes are emitted in track-outer / clip-inner order (track 0 across every clip, then
//     track 1), which is what the single per-track chunk offsets in stco/co64 assume;
//   - every frame's bytes appear exactly once, in frame order within each clip;
//   - the total emitted size equals the sum of all frame sizes.
// The frames_source returns each frame in a single synchronous read (no VOD_AGAIN), so one call to
// the process function runs the whole stream - the async resume path is exercised structurally by
// mirroring mp4_fragment's loop, and covered at runtime in the lab.
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "vod/mp4/mp4_progressive_builder.h"
#include "vod/input/frames_source.h"

static int g_failures = 0;
#define CHECK(cond, msg, ...) do { \
	if (cond) { printf("  ok   " msg "\n", ##__VA_ARGS__); } \
	else      { printf("  FAIL " msg "\n", ##__VA_ARGS__); g_failures++; } \
} while (0)

// ---- in-memory frames_source ----
typedef struct {
	u_char* blob;
	uint64_t cur;
	uint64_t end;
} mem_src_ctx_t;

static vod_status_t mem_start_frame(void* c, struct input_frame_s* frame, read_cache_hint_t* hint) {
	mem_src_ctx_t* ctx = c;
	(void)hint;
	ctx->cur = frame->offset;
	ctx->end = frame->offset + frame->size;
	return VOD_OK;
}
static vod_status_t mem_read(void* c, u_char** buffer, uint32_t* size, bool_t* frame_done) {
	mem_src_ctx_t* ctx = c;
	*buffer = ctx->blob + ctx->cur;
	*size = (uint32_t)(ctx->end - ctx->cur);
	ctx->cur = ctx->end;
	*frame_done = TRUE;
	return VOD_OK;
}
static void mem_set_slot(void* c, int slot) { (void)c; (void)slot; }
static void mem_disable(void* c) { (void)c; }
static vod_status_t mem_skip(void* c, uint32_t n) { (void)c; (void)n; return VOD_OK; }

static frames_source_t mem_source = {
	mem_set_slot, mem_start_frame, mem_read, mem_disable, mem_skip
};

// ---- output sink ----
typedef struct { u_char* out; uint32_t len; } sink_t;
static vod_status_t sink_write(void* c, u_char* buffer, uint32_t size) {
	sink_t* s = c;
	if (buffer != NULL && size > 0) {
		memcpy(s->out + s->len, buffer, size);
		s->len += size;
	}
	return VOD_OK;
}

static void mk_track(media_track_t* t, input_frame_t* frames, uint32_t n, mem_src_ctx_t* src_ctx) {
	memset(t, 0, sizeof(*t));
	t->frames.first_frame = frames;
	t->frames.last_frame = frames + n;
	t->frames.next = NULL;
	t->frames.frames_source = &mem_source;
	t->frames.frames_source_context = src_ctx;
	t->frame_count = n;
}

int main(void) {
	// sizes per (track, clip), distinct so ordering bugs surface as wrong bytes:
	//   track 0 (video): clip0 = {1000,500,500}, clip1 = {2000,800}
	//   track 1 (audio): clip0 = {100,100},      clip1 = {120,110}
	// lay every frame's bytes out contiguously in one blob, each filled with a unique signature
	uint32_t all_sizes[] = {1000,500,500, 2000,800, 100,100, 120,110};
	u_char sigs[]        = {10, 11, 12,   13,  14,  20, 21,  22,  23}; // one signature per frame
	uint32_t nframes = sizeof(all_sizes)/sizeof(all_sizes[0]);

	uint64_t total = 0;
	uint32_t i;
	for (i = 0; i < nframes; i++) total += all_sizes[i];

	u_char* blob = malloc(total);
	uint64_t off = 0;
	uint64_t offsets[16];
	for (i = 0; i < nframes; i++) {
		offsets[i] = off;
		memset(blob + off, sigs[i], all_sizes[i]);
		off += all_sizes[i];
	}

	// build frames with offsets into the blob. index map into all_sizes/offsets:
	// 0..2 = c0v, 3..4 = c1v, 5..6 = c0a, 7..8 = c1a
	#define MKF(idx) { .offset = offsets[idx], .size = all_sizes[idx], .key_frame = 1, .duration = 1, .pts_delay = 0 }
	input_frame_t c0v[] = { MKF(0), MKF(1), MKF(2) };
	input_frame_t c1v[] = { MKF(3), MKF(4) };
	input_frame_t c0a[] = { MKF(5), MKF(6) };
	input_frame_t c1a[] = { MKF(7), MKF(8) };

	mem_src_ctx_t src_ctx = { .blob = blob, .cur = 0, .end = 0 };

	uint32_t ttc = 2, clips = 2;
	media_track_t tracks[4]; // [c*ttc + t]: 0=c0v, 1=c0a, 2=c1v, 3=c1a
	mk_track(&tracks[0], c0v, 3, &src_ctx);
	mk_track(&tracks[1], c0a, 2, &src_ctx);
	mk_track(&tracks[2], c1v, 2, &src_ctx);
	mk_track(&tracks[3], c1a, 2, &src_ctx);

	media_set_t ms;
	memset(&ms, 0, sizeof(ms));
	ms.total_track_count = ttc;
	ms.clip_count = clips;
	ms.filtered_tracks = tracks;

	// expected: track 0 across all clips (c0v then c1v), then track 1 (c0a then c1a)
	u_char* expected = malloc(total);
	uint32_t elen = 0;
	uint32_t order[] = {0,1,2, 3,4, /*track0*/ 5,6, 7,8 /*track1*/};
	for (i = 0; i < nframes; i++) {
		memset(expected + elen, sigs[order[i]], all_sizes[order[i]]);
		elen += all_sizes[order[i]];
	}

	// run the writer
	request_context_t rc_ctx;
	memset(&rc_ctx, 0, sizeof(rc_ctx));

	sink_t sink = { .out = malloc(total), .len = 0 };
	pb_mdat_writer_state_t* state = NULL;
	vod_status_t rc = mp4_progressive_mdat_writer_init(
		&rc_ctx, &ms, sink_write, &sink, FALSE, &state);
	CHECK(rc == VOD_OK && state != NULL, "writer init ok (rc=%d)", (int)rc);

	rc = mp4_progressive_mdat_writer_process(state);
	CHECK(rc == VOD_OK, "writer process ran to completion (rc=%d)", (int)rc);

	CHECK(sink.len == total, "emitted total bytes = %llu (got %u)",
		(unsigned long long)total, sink.len);
	CHECK(sink.len == elen && memcmp(sink.out, expected, elen) == 0,
		"emitted bytes match track-outer / clip-inner order");

	printf("\n%s (%d failures)\n", g_failures ? "TESTS FAILED" : "ALL TESTS PASSED", g_failures);
	return g_failures ? 1 : 0;
}
