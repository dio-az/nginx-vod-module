#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "vod/mp4/mp4_progressive_builder.h"

static int fails = 0;
#define CHECK(c, m, ...) do { if(c){printf("  ok   " m "\n", ##__VA_ARGS__);} else {printf("  FAIL " m "\n", ##__VA_ARGS__); fails++;} } while(0)

int main(void) {
    // 3 tracks with data sizes 1000, 2000, 500. moov total (ftyp+moov) = 800. mdat header = 8.
    pb_track_stats_t st[3];
    memset(st, 0, sizeof(st));
    st[0].total_data_size = 1000;
    st[1].total_data_size = 2000;
    st[2].total_data_size = 500;

    CHECK(mp4_progressive_total_data_size(st, 3) == 3500, "total data = 3500");

    uint64_t off[3];
    mp4_progressive_compute_chunk_offsets(800, 8, st, 3, off);
    // data starts at 800 + 8 = 808
    CHECK(off[0] == 808,  "track0 offset = 808 (got %llu)", (unsigned long long)off[0]);
    CHECK(off[1] == 1808, "track1 offset = 808+1000 (got %llu)", (unsigned long long)off[1]);
    CHECK(off[2] == 3808, "track2 offset = 808+1000+2000 (got %llu)", (unsigned long long)off[2]);

    // co64 boundary: a track just under vs over needs 64-bit offsets
    pb_track_stats_t big[1]; memset(big, 0, sizeof(big));
    big[0].total_data_size = 5ULL << 30; // 5 GB
    uint64_t boff[1];
    mp4_progressive_compute_chunk_offsets(1000, 16, big, 1, boff);
    CHECK(boff[0] == 1016, "big track offset (got %llu)", (unsigned long long)boff[0]);
    CHECK(mp4_progressive_total_data_size(big, 1) == (5ULL<<30), "5GB total preserved (64-bit)");

    printf("\n%s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}
