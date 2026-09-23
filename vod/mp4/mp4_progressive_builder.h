#ifndef __MP4_PROGRESSIVE_BUILDER_H__
#define __MP4_PROGRESSIVE_BUILDER_H__

#include "../media_set.h"
#include "../common.h"

// Per-track statistics gathered in one pass over the concatenated frame lists of all clips.
// Used both to size the stbl children and to decide which optional tables to emit.
typedef struct {
	uint32_t frame_count;
	uint32_t stts_run_count;
	uint32_t ctts_run_count;
	uint32_t stss_count;
	bool_t   ctts_needed;     // TRUE if any frame has a non-zero pts_delay
	bool_t   stsz_uniform;
	uint32_t stsz_uniform_size;
	uint64_t total_data_size; // this track's contiguous mdat run length
	uint64_t total_duration;  // in the track timescale
} pb_track_stats_t;

// One pass over media_set->filtered_tracks for every output track. `stats` must have room for
// media_set->total_track_count entries.
void mp4_progressive_collect_stats(media_set_t* media_set, pb_track_stats_t* stats);

// Byte size of the six stbl children (stts/stsc/stsz/stco|co64[/stss][/ctts]) for one track.
// stsd is not included here - the moov assembler adds it, reusing mp4_init_segment's stsd writer.
uint64_t mp4_progressive_stbl_children_size(const pb_track_stats_t* s, bool_t use_co64, bool_t emit_ctts);

// Write the populated stbl children for one track. `chunk_offset` is the absolute file offset
// where this track's contiguous mdat run begins.
u_char* mp4_progressive_write_stbl_children(
	u_char* p,
	media_set_t* media_set,
	uint32_t track_index,
	const pb_track_stats_t* s,
	uint64_t chunk_offset,
	bool_t use_co64,
	bool_t emit_ctts);

// Sum of every track's contiguous mdat run.
uint64_t mp4_progressive_total_data_size(const pb_track_stats_t* stats, uint32_t track_count);

// Per-track absolute chunk offset: track t begins at (end of moov) + mdat header + prior runs.
void mp4_progressive_compute_chunk_offsets(
	uint64_t moov_total_size,
	uint32_t mdat_header_size,
	const pb_track_stats_t* stats,
	uint32_t track_count,
	uint64_t* out_offsets);

// Build ftyp + non-fragmented moov (populated stbl, no mvex) into `result`. The caller writes the
// mdat separately, in track order, matching the chunk offsets. `mdat_header_size` is 8 or 16;
// `*out_use_co64` reports the chunk-offset width chosen so the caller sizes the mdat header to match.
vod_status_t mp4_progressive_build_moov(
	request_context_t* request_context,
	media_set_t* media_set,
	uint32_t mdat_header_size,
	bool_t* out_use_co64,
	vod_str_t* result);

// One-shot header for the whole progressive response: ftyp + non-fragmented moov + the mdat box
// header (8 or 16 bytes, chosen from the total media size). `header` is written first (via
// write_tail) by the caller; the mdat payload is then streamed by the writer below. `content_length`
// is header->len + total media data, so the caller can send Content-Length up front. `content_type`
// is set to "video/mp4".
vod_status_t mp4_progressive_build_header(
	request_context_t* request_context,
	media_set_t* media_set,
	vod_str_t* header,
	size_t* content_length,
	vod_str_t* content_type);

// ---------------------------------------------------------------------------
// mdat streaming: writes each track's frame bytes, in track order (track 0's frames across every
// clip, then track 1's, ...), matching the stco/co64 offsets baked into the moov. Mirrors
// mp4_fragment's async frame writer (VOD_OK when done, VOD_AGAIN to yield until the next read).
// ---------------------------------------------------------------------------

typedef struct pb_mdat_writer_state_s pb_mdat_writer_state_t;

vod_status_t mp4_progressive_mdat_writer_init(
	request_context_t* request_context,
	media_set_t* media_set,
	write_callback_t write_callback,
	void* write_context,
	bool_t reuse_buffers,
	pb_mdat_writer_state_t** result);

vod_status_t mp4_progressive_mdat_writer_process(pb_mdat_writer_state_t* state);

#endif // __MP4_PROGRESSIVE_BUILDER_H__
