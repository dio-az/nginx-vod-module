#include "mp4_progressive_builder.h"
#include "mp4_defs.h"
#include "mp4_write_stream.h"
#include "mp4_init_segment.h"

// Builds the populated sample tables (stbl children) for a non-fragmented, multi-clip
// progressive MP4. This is the piece nginx-vod-module lacks: mp4_init_segment.c writes an
// empty stbl (fixed_stbl_atoms) because for fMP4 the timing lives in each fragment's trun.
// A progressive moov needs the tables filled from the concatenated frame lists of all clips.
//
// Layout decision (v1): the mdat is written non-interleaved, one contiguous run per track
// (all of track 0's frames across every clip, then all of track 1's, ...). That makes stsc a
// single entry and stco a single offset per track. Non-interleaved is fine for a download that
// is saved and opened in an editor; interleaving is a later refinement.
//
// Sample numbering and chunk offsets are cumulative across clips. Frames come from
// media_set->filtered_tracks[c * total_track_count + t] for clip c, output track t.

// ---------------------------------------------------------------------------
// Per-track walk: gather the counts pass 1 needs, without allocating.
// ---------------------------------------------------------------------------

// Iterate every frame of output track `track_index` across all clips, in output order.
// The callback returns VOD_OK to continue.
static void
pb_walk_track_stats(media_set_t* media_set, uint32_t track_index, pb_track_stats_t* out)
{
	uint32_t total_track_count = media_set->total_track_count;
	uint32_t clip_index;
	bool_t first = TRUE;
	uint32_t prev_duration = 0;
	int64_t prev_pts_delay = 0;

	vod_memzero(out, sizeof(*out));
	out->stsz_uniform = TRUE;

	for (clip_index = 0; clip_index < media_set->clip_count; clip_index++) {
		media_track_t* track = &media_set->filtered_tracks[clip_index * total_track_count + track_index];
		frame_list_part_t* part = &track->frames;
		input_frame_t* cur;

		for (; part != NULL; part = part->next) {
			for (cur = part->first_frame; cur < part->last_frame; cur++) {
				// stsz
				if (out->frame_count == 0) {
					out->stsz_uniform_size = cur->size;
				} else if (cur->size != out->stsz_uniform_size) {
					out->stsz_uniform = FALSE;
				}
				out->total_data_size += cur->size;
				out->total_duration += cur->duration;

				// stts run-length
				if (first || cur->duration != prev_duration) {
					out->stts_run_count++;
					prev_duration = cur->duration;
				}

				// ctts run-length (only if any non-zero pts_delay appears)
				if (first || (int64_t)cur->pts_delay != prev_pts_delay) {
					out->ctts_run_count++;
					prev_pts_delay = (int64_t)cur->pts_delay;
				}

				// stss
				if (cur->key_frame) {
					out->stss_count++;
				}

				if (cur->pts_delay != 0) {
					out->ctts_needed = TRUE;
				}

				out->frame_count++;
				first = FALSE;
			}
		}
	}
}

// Size of the six stbl children for one track, given its stats. stsd is added by the moov
// assembler (reused from mp4_init_segment via the stsd writer), not here.
uint64_t
mp4_progressive_stbl_children_size(const pb_track_stats_t* s, bool_t use_co64, bool_t emit_ctts)
{
	uint64_t size = 0;

	// stts: fullbox + entry_count + 8 * runs
	size += ATOM_HEADER_SIZE + 8 + (uint64_t)s->stts_run_count * 8;

	// stsc: one chunk => 1 entry of 12 bytes
	size += ATOM_HEADER_SIZE + 8 + 12;

	// stsz: fullbox + sample_size + sample_count (+ table if not uniform)
	size += ATOM_HEADER_SIZE + 12;
	if (!s->stsz_uniform) {
		size += (uint64_t)s->frame_count * 4;
	}

	// stco / co64: one entry
	size += ATOM_HEADER_SIZE + 8 + (use_co64 ? 8 : 4);

	// stss: omitted when all frames are key frames
	if (s->stss_count != s->frame_count) {
		size += ATOM_HEADER_SIZE + 8 + (uint64_t)s->stss_count * 4;
	}

	// ctts: omitted when every pts_delay is zero
	if (emit_ctts) {
		size += ATOM_HEADER_SIZE + 8 + (uint64_t)s->ctts_run_count * 8;
	}

	return size;
}

// ---------------------------------------------------------------------------
// Writers for the six tables. `chunk_offset` is the absolute file offset where this
// track's contiguous run begins in the mdat.
// ---------------------------------------------------------------------------

static u_char*
pb_write_stts(u_char* p, media_set_t* media_set, uint32_t track_index, uint32_t run_count)
{
	uint32_t total_track_count = media_set->total_track_count;
	uint32_t clip_index;
	size_t atom_size = ATOM_HEADER_SIZE + 8 + (size_t)run_count * 8;
	bool_t first = TRUE;
	uint32_t prev_duration = 0;
	uint32_t run_len = 0;

	write_atom_header(p, atom_size, 's', 't', 't', 's');
	write_fullbox_header(p, 0, 0);
	write_be32(p, run_count);

	for (clip_index = 0; clip_index < media_set->clip_count; clip_index++) {
		media_track_t* track = &media_set->filtered_tracks[clip_index * total_track_count + track_index];
		frame_list_part_t* part = &track->frames;
		input_frame_t* cur;
		for (; part != NULL; part = part->next) {
			for (cur = part->first_frame; cur < part->last_frame; cur++) {
				if (first) {
					prev_duration = cur->duration;
					run_len = 1;
					first = FALSE;
				} else if (cur->duration == prev_duration) {
					run_len++;
				} else {
					write_be32(p, run_len);
					write_be32(p, prev_duration);
					prev_duration = cur->duration;
					run_len = 1;
				}
			}
		}
	}
	if (!first) {
		write_be32(p, run_len);
		write_be32(p, prev_duration);
	}
	return p;
}

static u_char*
pb_write_stsc(u_char* p, uint32_t frame_count)
{
	size_t atom_size = ATOM_HEADER_SIZE + 8 + 12;
	write_atom_header(p, atom_size, 's', 't', 's', 'c');
	write_fullbox_header(p, 0, 0);
	write_be32(p, 1);           // entry_count
	write_be32(p, 1);           // first_chunk
	write_be32(p, frame_count); // samples_per_chunk
	write_be32(p, 1);           // sample_description_index
	return p;
}

static u_char*
pb_write_stsz(u_char* p, media_set_t* media_set, uint32_t track_index, const pb_track_stats_t* s)
{
	uint32_t total_track_count = media_set->total_track_count;
	uint32_t clip_index;
	size_t atom_size = ATOM_HEADER_SIZE + 12 + (s->stsz_uniform ? 0 : (size_t)s->frame_count * 4);

	write_atom_header(p, atom_size, 's', 't', 's', 'z');
	write_fullbox_header(p, 0, 0);
	write_be32(p, s->stsz_uniform ? s->stsz_uniform_size : 0);
	write_be32(p, s->frame_count);
	if (s->stsz_uniform) {
		return p;
	}
	for (clip_index = 0; clip_index < media_set->clip_count; clip_index++) {
		media_track_t* track = &media_set->filtered_tracks[clip_index * total_track_count + track_index];
		frame_list_part_t* part = &track->frames;
		input_frame_t* cur;
		for (; part != NULL; part = part->next) {
			for (cur = part->first_frame; cur < part->last_frame; cur++) {
				write_be32(p, cur->size);
			}
		}
	}
	return p;
}

static u_char*
pb_write_stco(u_char* p, uint64_t chunk_offset, bool_t use_co64)
{
	if (use_co64) {
		size_t atom_size = ATOM_HEADER_SIZE + 8 + 8;
		write_atom_header(p, atom_size, 'c', 'o', '6', '4');
		write_fullbox_header(p, 0, 0);
		write_be32(p, 1);
		write_be64(p, chunk_offset);
	} else {
		size_t atom_size = ATOM_HEADER_SIZE + 8 + 4;
		write_atom_header(p, atom_size, 's', 't', 'c', 'o');
		write_fullbox_header(p, 0, 0);
		write_be32(p, 1);
		write_be32(p, (uint32_t)chunk_offset);
	}
	return p;
}

static u_char*
pb_write_stss(u_char* p, media_set_t* media_set, uint32_t track_index, uint32_t stss_count)
{
	uint32_t total_track_count = media_set->total_track_count;
	uint32_t clip_index;
	uint32_t sample_number = 0;
	size_t atom_size = ATOM_HEADER_SIZE + 8 + (size_t)stss_count * 4;

	write_atom_header(p, atom_size, 's', 't', 's', 's');
	write_fullbox_header(p, 0, 0);
	write_be32(p, stss_count);
	for (clip_index = 0; clip_index < media_set->clip_count; clip_index++) {
		media_track_t* track = &media_set->filtered_tracks[clip_index * total_track_count + track_index];
		frame_list_part_t* part = &track->frames;
		input_frame_t* cur;
		for (; part != NULL; part = part->next) {
			for (cur = part->first_frame; cur < part->last_frame; cur++) {
				sample_number++;
				if (cur->key_frame) {
					write_be32(p, sample_number);
				}
			}
		}
	}
	return p;
}

static u_char*
pb_write_ctts(u_char* p, media_set_t* media_set, uint32_t track_index, uint32_t run_count)
{
	uint32_t total_track_count = media_set->total_track_count;
	uint32_t clip_index;
	size_t atom_size = ATOM_HEADER_SIZE + 8 + (size_t)run_count * 8;
	bool_t first = TRUE;
	int64_t prev = 0;
	uint32_t run_len = 0;

	// version 0: sample_offset is uint32. The module stores pts_delay as uint32 (>= 0), so
	// version 0 is always valid here. A signed ctts (version 1) + edit list is a refinement.
	write_atom_header(p, atom_size, 'c', 't', 't', 's');
	write_fullbox_header(p, 0, 0);
	write_be32(p, run_count);
	for (clip_index = 0; clip_index < media_set->clip_count; clip_index++) {
		media_track_t* track = &media_set->filtered_tracks[clip_index * total_track_count + track_index];
		frame_list_part_t* part = &track->frames;
		input_frame_t* cur;
		for (; part != NULL; part = part->next) {
			for (cur = part->first_frame; cur < part->last_frame; cur++) {
				if (first) {
					prev = (int64_t)cur->pts_delay;
					run_len = 1;
					first = FALSE;
				} else if ((int64_t)cur->pts_delay == prev) {
					run_len++;
				} else {
					write_be32(p, run_len);
					write_be32(p, (uint32_t)prev);
					prev = (int64_t)cur->pts_delay;
					run_len = 1;
				}
			}
		}
	}
	if (!first) {
		write_be32(p, run_len);
		write_be32(p, (uint32_t)prev);
	}
	return p;
}

// Public: write the populated stbl children for one track at p, returning the new p.
// `chunk_offset` is the absolute file offset of this track's mdat run.
u_char*
mp4_progressive_write_stbl_children(
	u_char* p,
	media_set_t* media_set,
	uint32_t track_index,
	const pb_track_stats_t* s,
	uint64_t chunk_offset,
	bool_t use_co64,
	bool_t emit_ctts)
{
	p = pb_write_stts(p, media_set, track_index, s->stts_run_count);
	p = pb_write_stsc(p, s->frame_count);
	p = pb_write_stsz(p, media_set, track_index, s);
	p = pb_write_stco(p, chunk_offset, use_co64);
	if (s->stss_count != s->frame_count) {
		p = pb_write_stss(p, media_set, track_index, s->stss_count);
	}
	if (emit_ctts) {
		p = pb_write_ctts(p, media_set, track_index, s->ctts_run_count);
	}
	return p;
}

// Gather stats for every output track. Caller allocates stats[total_track_count].
void
mp4_progressive_collect_stats(media_set_t* media_set, pb_track_stats_t* stats)
{
	uint32_t t;
	for (t = 0; t < media_set->total_track_count; t++) {
		pb_walk_track_stats(media_set, t, &stats[t]);
	}
}

// ---------------------------------------------------------------------------
// Offset computation (pure; unit-testable without a pool)
// ---------------------------------------------------------------------------

// Sum of every track's contiguous mdat run.
uint64_t
mp4_progressive_total_data_size(const pb_track_stats_t* stats, uint32_t track_count)
{
	uint64_t total = 0;
	uint32_t t;
	for (t = 0; t < track_count; t++) {
		total += stats[t].total_data_size;
	}
	return total;
}

// Per-track absolute chunk offset into the output file. mdat layout is track 0's whole run, then
// track 1's, ... so track t begins at (end of moov) + mdat header + sum of prior tracks' runs.
void
mp4_progressive_compute_chunk_offsets(
	uint64_t moov_total_size,
	uint32_t mdat_header_size,
	const pb_track_stats_t* stats,
	uint32_t track_count,
	uint64_t* out_offsets)
{
	uint64_t pos = moov_total_size + mdat_header_size;
	uint32_t t;
	for (t = 0; t < track_count; t++) {
		out_offsets[t] = pos;
		pos += stats[t].total_data_size;
	}
}

// ---------------------------------------------------------------------------
// moov assembler: ftyp + non-fragmented moov (populated stbl, no mvex)
// ---------------------------------------------------------------------------

typedef struct {
	media_set_t* media_set;
	pb_track_stats_t* stats;
	uint32_t track_index;
	uint64_t chunk_offset; // filled in after the sizing pass
	bool_t use_co64;
	bool_t emit_ctts;
} pb_stbl_ctx_t;

static u_char*
pb_stbl_write(void* ctx_, u_char* p)
{
	pb_stbl_ctx_t* c = ctx_;
	return mp4_progressive_write_stbl_children(
		p, c->media_set, c->track_index, c->stats, c->chunk_offset, c->use_co64, c->emit_ctts);
}

// Build ftyp + moov for a non-fragmented multi-clip MP4 into `result`. The caller writes the mdat
// (its header of `mdat_header_size` bytes, then each track's frame bytes in track order - track 0's
// frames across every clip, then track 1's, ...) so it lines up with the stco/co64 offsets baked in
// here. `mdat_header_size` is 8 (32-bit mdat) or 16 (64-bit large mdat). `*out_use_co64` reports
// which chunk-offset width was chosen so the caller can size the mdat header to match.
vod_status_t
mp4_progressive_build_moov(
	request_context_t* request_context,
	media_set_t* media_set,
	uint32_t mdat_header_size,
	bool_t* out_use_co64,
	vod_str_t* result)
{
	uint32_t ttc = media_set->total_track_count;
	pb_track_stats_t* stats;
	pb_stbl_ctx_t* ctxs;
	atom_writer_t* writers;
	uint64_t* offsets;
	uint64_t total_data;
	bool_t use_co64;
	vod_str_t sizing;
	uint32_t t;
	vod_status_t rc;

	stats = vod_alloc(request_context->pool, sizeof(*stats) * ttc);
	ctxs = vod_alloc(request_context->pool, sizeof(*ctxs) * ttc);
	writers = vod_alloc(request_context->pool, sizeof(*writers) * ttc);
	offsets = vod_alloc(request_context->pool, sizeof(*offsets) * ttc);
	if (stats == NULL || ctxs == NULL || writers == NULL || offsets == NULL) {
		return VOD_ALLOC_FAILED;
	}

	mp4_progressive_collect_stats(media_set, stats);

	// choose 32- vs 64-bit chunk offsets from a safe upper bound (the moov is small next to 4 GB)
	total_data = mp4_progressive_total_data_size(stats, ttc);
	use_co64 = (total_data + (64ULL << 20)) > 0xFFFFFFFFULL;

	for (t = 0; t < ttc; t++) {
		ctxs[t].media_set = media_set;
		ctxs[t].stats = stats;
		ctxs[t].track_index = t;
		ctxs[t].chunk_offset = 0;
		ctxs[t].use_co64 = use_co64;
		ctxs[t].emit_ctts = stats[t].ctts_needed;

		writers[t].atom_size = mp4_progressive_stbl_children_size(&stats[t], use_co64, stats[t].ctts_needed);
		writers[t].write = pb_stbl_write;
		writers[t].context = &ctxs[t];
	}

	// pass 1 - size the moov so the chunk offsets can be resolved
	vod_memzero(&sizing, sizeof(sizing));
	rc = mp4_init_segment_build_ex(request_context, media_set, TRUE, NULL, NULL, writers, TRUE, &sizing);
	if (rc != VOD_OK) {
		return rc;
	}

	mp4_progressive_compute_chunk_offsets(sizing.len, mdat_header_size, stats, ttc, offsets);
	for (t = 0; t < ttc; t++) {
		ctxs[t].chunk_offset = offsets[t];
	}

	// pass 2 - write the moov with resolved offsets
	rc = mp4_init_segment_build_ex(request_context, media_set, FALSE, NULL, NULL, writers, TRUE, result);
	if (rc != VOD_OK) {
		return rc;
	}

	if (out_use_co64 != NULL) {
		*out_use_co64 = use_co64;
	}
	return VOD_OK;
}

// ---------------------------------------------------------------------------
// One-shot header: ftyp + moov + mdat box header, plus content length / type
// ---------------------------------------------------------------------------

static vod_str_t mp4_progressive_content_type = vod_string("video/mp4");

vod_status_t
mp4_progressive_build_header(
	request_context_t* request_context,
	media_set_t* media_set,
	vod_str_t* header,
	size_t* content_length,
	vod_str_t* content_type)
{
	pb_track_stats_t* stats;
	vod_str_t moov;
	uint64_t total_data;
	uint32_t mdat_header_size;
	uint64_t mdat_box_size;
	u_char* buffer;
	u_char* p;
	vod_status_t rc;

	// total media size decides the mdat box header width (8 vs 16 bytes)
	stats = vod_alloc(request_context->pool, sizeof(*stats) * media_set->total_track_count);
	if (stats == NULL) {
		return VOD_ALLOC_FAILED;
	}
	mp4_progressive_collect_stats(media_set, stats);
	total_data = mp4_progressive_total_data_size(stats, media_set->total_track_count);
	mdat_header_size = (total_data + 8 > 0xFFFFFFFFULL) ? 16 : 8;

	// build ftyp + moov with chunk offsets that account for the mdat header
	rc = mp4_progressive_build_moov(request_context, media_set, mdat_header_size, NULL, &moov);
	if (rc != VOD_OK) {
		return rc;
	}

	// append the mdat box header right after the moov
	buffer = vod_alloc(request_context->pool, moov.len + mdat_header_size);
	if (buffer == NULL) {
		return VOD_ALLOC_FAILED;
	}
	vod_memcpy(buffer, moov.data, moov.len);
	p = buffer + moov.len;

	mdat_box_size = (uint64_t)mdat_header_size + total_data;
	if (mdat_header_size == 16) {
		write_atom_header64(p, mdat_box_size, 'm', 'd', 'a', 't');
	} else {
		write_atom_header(p, (uint32_t)mdat_box_size, 'm', 'd', 'a', 't');
	}

	header->data = buffer;
	header->len = moov.len + mdat_header_size;
	*content_length = header->len + total_data;
	*content_type = mp4_progressive_content_type;

	return VOD_OK;
}

// ---------------------------------------------------------------------------
// mdat streaming writer (mirrors mp4_fragment_frame_writer, track-outer/clip-inner)
// ---------------------------------------------------------------------------

struct pb_mdat_writer_state_s {
	request_context_t* request_context;
	write_callback_t write_callback;
	void* write_context;
	bool_t reuse_buffers;

	media_set_t* media_set;
	uint32_t total_track_count;

	uint32_t track_index; // outer loop: 0 .. total_track_count - 1
	uint32_t clip_index;  // inner loop: 0 .. clip_count - 1

	frame_list_part_t cur_frame_part; // held by value, like mp4_fragment
	input_frame_t* cur_frame;
	bool_t first_time;
	bool_t frame_started;
};

// Point the cursor at the frames of filtered_tracks[clip_index * ttc + track_index].
static void
pb_mdat_init_track(pb_mdat_writer_state_t* state)
{
	media_track_t* track = &state->media_set->filtered_tracks[
		state->clip_index * state->total_track_count + state->track_index];

	state->first_time = TRUE;
	state->cur_frame_part = track->frames;
	state->cur_frame = track->frames.first_frame;

	if (!state->reuse_buffers) {
		state->cur_frame_part.frames_source->disable_buffer_reuse(
			state->cur_frame_part.frames_source_context);
	}
}

// Advance to the next frame: walk parts within a clip, then clips within a track, then tracks.
// Returns FALSE when every frame of every track has been emitted.
static bool_t
pb_mdat_move_to_next_frame(pb_mdat_writer_state_t* state)
{
	while (state->cur_frame >= state->cur_frame_part.last_frame) {
		if (state->cur_frame_part.next != NULL) {
			state->cur_frame_part = *state->cur_frame_part.next;
			state->cur_frame = state->cur_frame_part.first_frame;
			state->first_time = TRUE;
			continue; // re-check (a part could be empty)
		}

		// next clip of the current track
		state->clip_index++;
		if (state->clip_index >= state->media_set->clip_count) {
			// next track, back to clip 0
			state->track_index++;
			if (state->track_index >= state->total_track_count) {
				return FALSE;
			}
			state->clip_index = 0;
		}

		pb_mdat_init_track(state);
	}

	return TRUE;
}

vod_status_t
mp4_progressive_mdat_writer_init(
	request_context_t* request_context,
	media_set_t* media_set,
	write_callback_t write_callback,
	void* write_context,
	bool_t reuse_buffers,
	pb_mdat_writer_state_t** result)
{
	pb_mdat_writer_state_t* state;

	state = vod_alloc(request_context->pool, sizeof(*state));
	if (state == NULL) {
		vod_log_debug0(
			VOD_LOG_DEBUG_LEVEL, request_context->log, 0, "mp4_progressive_mdat_writer_init: vod_alloc failed");
		return VOD_ALLOC_FAILED;
	}

	state->request_context = request_context;
	state->write_callback = write_callback;
	state->write_context = write_context;
	state->reuse_buffers = reuse_buffers;
	state->media_set = media_set;
	state->total_track_count = media_set->total_track_count;
	state->track_index = 0;
	state->clip_index = 0;
	state->frame_started = FALSE;

	pb_mdat_init_track(state);

	*result = state;
	return VOD_OK;
}

// Async frame pump. Identical control flow to mp4_fragment_frame_writer_process, but the iteration
// order is track-outer / clip-inner so the emitted bytes line up with the single per-track chunk
// offsets written into stco/co64.
vod_status_t
mp4_progressive_mdat_writer_process(pb_mdat_writer_state_t* state)
{
	u_char* read_buffer;
	uint32_t read_size;
	u_char* write_buffer = NULL;
	uint32_t write_buffer_size = 0;
	vod_status_t rc;
	bool_t processed_data = FALSE;
	bool_t frame_done;

	if (!state->frame_started) {
		if (!pb_mdat_move_to_next_frame(state)) {
			return VOD_OK;
		}

		rc = state->cur_frame_part.frames_source->start_frame(
			state->cur_frame_part.frames_source_context, state->cur_frame, NULL);
		if (rc != VOD_OK) {
			return rc;
		}

		state->frame_started = TRUE;
	}

	for (;;) {
		rc = state->cur_frame_part.frames_source->read(
			state->cur_frame_part.frames_source_context, &read_buffer, &read_size, &frame_done);
		if (rc != VOD_OK) {
			if (rc != VOD_AGAIN) {
				return rc;
			}

			if (write_buffer_size != 0) {
				rc = state->write_callback(state->write_context, write_buffer, write_buffer_size);
				if (rc != VOD_OK) {
					return rc;
				}
			} else if (!processed_data && !state->first_time) {
				vod_log_error(
					VOD_LOG_ERR,
					state->request_context->log,
					0,
					"mp4_progressive_mdat_writer_process: no data was handled, probably a truncated file");
				return VOD_BAD_DATA;
			}

			state->first_time = FALSE;
			return VOD_AGAIN;
		}

		processed_data = TRUE;

		if (state->reuse_buffers) {
			rc = state->write_callback(state->write_context, read_buffer, read_size);
			if (rc != VOD_OK) {
				return rc;
			}
		} else if (write_buffer_size != 0) {
			if (write_buffer + write_buffer_size == read_buffer) {
				write_buffer_size += read_size;
			} else {
				rc = state->write_callback(state->write_context, write_buffer, write_buffer_size);
				if (rc != VOD_OK) {
					return rc;
				}
				write_buffer = read_buffer;
				write_buffer_size = read_size;
			}
		} else {
			write_buffer = read_buffer;
			write_buffer_size = read_size;
		}

		if (!frame_done) {
			continue;
		}

		state->cur_frame++;

		if (state->cur_frame >= state->cur_frame_part.last_frame) {
			if (write_buffer_size != 0) {
				rc = state->write_callback(state->write_context, write_buffer, write_buffer_size);
				if (rc != VOD_OK) {
					return rc;
				}
				write_buffer_size = 0;
			}

			if (!pb_mdat_move_to_next_frame(state)) {
				return VOD_OK;
			}
		}

		rc = state->cur_frame_part.frames_source->start_frame(
			state->cur_frame_part.frames_source_context, state->cur_frame, NULL);
		if (rc != VOD_OK) {
			return rc;
		}
	}
}
