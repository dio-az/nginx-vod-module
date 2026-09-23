// Standalone unit test for mp4_progressive_builder.
// Fabricates a 2-clip, 2-track (video + audio) media_set, runs the builder, and asserts the
// emitted stbl atoms byte by byte - focusing on the risky parts: sample numbering cumulative
// across clips (stss), correct filtered_tracks[c*ttc+t] indexing, run-length stts, stss omission
// for audio, and the stsz table.
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "vod/mp4/mp4_progressive_builder.h"

static int g_failures = 0;
#define CHECK(cond, msg, ...) do { \
	if (cond) { printf("  ok   " msg "\n", ##__VA_ARGS__); } \
	else      { printf("  FAIL " msg "\n", ##__VA_ARGS__); g_failures++; } \
} while (0)

static uint32_t rd32(const u_char* p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// Build a media_track_t with a single frame_list_part pointing at `frames`.
static void mk_track(media_track_t* t, input_frame_t* frames, uint32_t n) {
	memset(t, 0, sizeof(*t));
	t->frames.first_frame = frames;
	t->frames.last_frame = frames + n;
	t->frames.next = NULL;
	t->frame_count = n;
}

int main(void) {
	// input_frame_t: { offset, size, key_frame, duration, pts_delay }
	// clip0 video: 3 frames, sizes 1000/500/500, dur 3000, key 1/0/0
	input_frame_t c0v[] = {
		{ .offset=0, .size=1000, .key_frame=1, .duration=3000, .pts_delay=0 },
		{ .offset=0, .size=500,  .key_frame=0, .duration=3000, .pts_delay=0 },
		{ .offset=0, .size=500,  .key_frame=0, .duration=3000, .pts_delay=0 },
	};
	// clip0 audio: 2 frames, sizes 100/100, dur 1024, all key
	input_frame_t c0a[] = {
		{ .offset=0, .size=100, .key_frame=1, .duration=1024, .pts_delay=0 },
		{ .offset=0, .size=100, .key_frame=1, .duration=1024, .pts_delay=0 },
	};
	// clip1 video: 2 frames, sizes 2000/800, dur 3000, key 1/0
	input_frame_t c1v[] = {
		{ .offset=0, .size=2000, .key_frame=1, .duration=3000, .pts_delay=0 },
		{ .offset=0, .size=800,  .key_frame=0, .duration=3000, .pts_delay=0 },
	};
	// clip1 audio: 2 frames, sizes 120/110, dur 1024, all key
	input_frame_t c1a[] = {
		{ .offset=0, .size=120, .key_frame=1, .duration=1024, .pts_delay=0 },
		{ .offset=0, .size=110, .key_frame=1, .duration=1024, .pts_delay=0 },
	};

	uint32_t ttc = 2; // tracks per clip: 0=video, 1=audio
	uint32_t clips = 2;
	media_track_t tracks[4]; // [c*ttc + t]: 0=c0v,1=c0a,2=c1v,3=c1a
	mk_track(&tracks[0], c0v, 3);
	mk_track(&tracks[1], c0a, 2);
	mk_track(&tracks[2], c1v, 2);
	mk_track(&tracks[3], c1a, 2);

	media_set_t ms;
	memset(&ms, 0, sizeof(ms));
	ms.total_track_count = ttc;
	ms.clip_count = clips;
	ms.filtered_tracks = tracks;

	pb_track_stats_t stats[2];
	mp4_progressive_collect_stats(&ms, stats);

	printf("== stats ==\n");
	// video (track 0): 3 + 2 = 5 frames, 1 stts run, 2 keyframes, not uniform, no ctts
	CHECK(stats[0].frame_count == 5, "video frame_count = 5 (got %u)", stats[0].frame_count);
	CHECK(stats[0].stts_run_count == 1, "video stts runs = 1 (got %u)", stats[0].stts_run_count);
	CHECK(stats[0].stss_count == 2, "video keyframes = 2 (got %u)", stats[0].stss_count);
	CHECK(stats[0].ctts_needed == 0, "video ctts not needed");
	CHECK(stats[0].stsz_uniform == 0, "video stsz not uniform");
	CHECK(stats[0].total_data_size == 1000+500+500+2000+800, "video data size (got %llu)",
		(unsigned long long)stats[0].total_data_size);
	// audio (track 1): 2 + 2 = 4 frames, all key, uniform? 100,100,120,110 -> not uniform
	CHECK(stats[1].frame_count == 4, "audio frame_count = 4 (got %u)", stats[1].frame_count);
	CHECK(stats[1].stss_count == 4, "audio all keyframes (got %u)", stats[1].stss_count);
	CHECK(stats[1].stts_run_count == 1, "audio stts runs = 1 (got %u)", stats[1].stts_run_count);

	// ---- write video stbl children and parse ----
	printf("== video stbl ==\n");
	uint64_t vsize = mp4_progressive_stbl_children_size(&stats[0], 0, stats[0].ctts_needed);
	u_char buf[4096];
	uint64_t chunk_off = 0x11223344ULL;
	u_char* end = mp4_progressive_write_stbl_children(buf, &ms, 0, &stats[0], chunk_off, 0, stats[0].ctts_needed);
	CHECK((uint64_t)(end - buf) == vsize, "video written size == computed size (%llu vs %llu)",
		(unsigned long long)(end - buf), (unsigned long long)vsize);

	// walk atoms
	u_char* p = buf;
	int seen_stts=0, seen_stsc=0, seen_stsz=0, seen_stco=0, seen_stss=0, seen_ctts=0;
	while (p < end) {
		uint32_t asize = rd32(p);
		char name[5] = { p[4], p[5], p[6], p[7], 0 };
		u_char* body = p + 8;
		if (!strcmp(name, "stts")) {
			seen_stts = 1;
			uint32_t entries = rd32(body + 4);
			uint32_t count = rd32(body + 8), delta = rd32(body + 12);
			CHECK(entries == 1, "stts entry_count = 1 (got %u)", entries);
			CHECK(count == 5 && delta == 3000, "stts run = {5,3000} (got {%u,%u})", count, delta);
		} else if (!strcmp(name, "stsc")) {
			seen_stsc = 1;
			uint32_t first_chunk = rd32(body + 8);
			uint32_t spc = rd32(body + 12); // entry: first_chunk, samples_per_chunk, sdi
			CHECK(first_chunk == 1 && spc == 5, "stsc {first_chunk=1, spc=5} (got {%u,%u})", first_chunk, spc);
		} else if (!strcmp(name, "stsz")) {
			seen_stsz = 1;
			uint32_t ssize = rd32(body + 4), scount = rd32(body + 8);
			uint32_t s0 = rd32(body + 12), s3 = rd32(body + 12 + 3*4);
			CHECK(ssize == 0 && scount == 5, "stsz sample_size=0 count=5 (got %u,%u)", ssize, scount);
			CHECK(s0 == 1000 && s3 == 2000, "stsz[0]=1000 stsz[3]=2000 (got %u,%u)", s0, s3);
		} else if (!strcmp(name, "stco")) {
			seen_stco = 1;
			uint32_t off = rd32(body + 8);
			CHECK(off == (uint32_t)chunk_off, "stco[0] = chunk offset (got 0x%x)", off);
		} else if (!strcmp(name, "stss")) {
			seen_stss = 1;
			uint32_t entries = rd32(body + 4);
			uint32_t k0 = rd32(body + 8), k1 = rd32(body + 12);
			CHECK(entries == 2, "stss entry_count = 2 (got %u)", entries);
			CHECK(k0 == 1 && k1 == 4, "stss keyframes CUMULATIVE = {1,4} (got {%u,%u})", k0, k1);
		} else if (!strcmp(name, "ctts")) {
			seen_ctts = 1;
		}
		p += asize;
	}
	CHECK(seen_stts && seen_stsc && seen_stsz && seen_stco, "video: stts/stsc/stsz/stco present");
	CHECK(seen_stss, "video: stss present (has non-key frames)");
	CHECK(!seen_ctts, "video: ctts omitted (all pts_delay 0)");

	// ---- audio: stss must be OMITTED (all key frames) ----
	printf("== audio stbl ==\n");
	u_char abuf[4096];
	u_char* aend = mp4_progressive_write_stbl_children(abuf, &ms, 1, &stats[1], 0x5000, 0, stats[1].ctts_needed);
	p = abuf; int a_stss=0, a_stsz_count=0;
	while (p < aend) {
		uint32_t asize = rd32(p);
		char name[5] = { p[4], p[5], p[6], p[7], 0 };
		if (!strcmp(name, "stss")) a_stss = 1;
		if (!strcmp(name, "stsz")) a_stsz_count = rd32(p + 8 + 8);
		p += asize;
	}
	CHECK(!a_stss, "audio: stss OMITTED (all frames sync)");
	CHECK(a_stsz_count == 4, "audio: stsz count = 4 (got %u)", a_stsz_count);

	printf("\n%s (%d failures)\n", g_failures ? "TESTS FAILED" : "ALL TESTS PASSED", g_failures);
	return g_failures ? 1 : 0;
}
