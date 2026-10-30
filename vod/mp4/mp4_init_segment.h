#ifndef __MP4_INIT_SEGMENT_H__
#define __MP4_INIT_SEGMENT_H__

// includes
#include "../media_set.h"

// typedefs
typedef u_char* (*atom_writer_func_t)(void* context, u_char* p);

typedef struct {
	size_t atom_size;
	atom_writer_func_t write;
	void* context;
} atom_writer_t;

// functions
vod_status_t
mp4_init_segment_build_stsd_atom(request_context_t* request_context, media_track_t* track);

vod_status_t mp4_init_segment_build(
	request_context_t* request_context,
	media_set_t* media_set,
	bool_t size_only,
	atom_writer_t* extra_moov_atoms_writer,
	atom_writer_t* stsd_atom_writers,
	vod_str_t* result
);

// Extended variant: `stbl_atom_writers` (per-track, may be NULL) supplies populated sample tables
// instead of the empty defaults, and `no_mvex` omits the mvex/trex atoms - together these turn the
// init-segment builder into a non-fragmented (progressive) moov builder. Passing NULL/FALSE is
// byte-for-byte identical to mp4_init_segment_build.
vod_status_t mp4_init_segment_build_ex(
	request_context_t* request_context,
	media_set_t* media_set,
	bool_t size_only,
	atom_writer_t* extra_moov_atoms_writer,
	atom_writer_t* stsd_atom_writers,
	atom_writer_t* stbl_atom_writers,
	bool_t no_mvex,
	vod_str_t* result
);

vod_status_t mp4_init_segment_get_encrypted_stsd_writers(
	request_context_t* request_context,
	media_set_t* media_set,
	uint32_t scheme_type,
	bool_t has_clear_lead,
	u_char* default_kid,
	u_char* iv,
	atom_writer_t** result
);

#endif // __MP4_INIT_SEGMENT_H__
