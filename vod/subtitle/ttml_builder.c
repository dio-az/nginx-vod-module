#include "ttml_builder.h"

// constants
#define TTML_TIMESTAMP_FORMAT "%02uD:%02uD:%02uD.%03uD"
#define TTML_TIMESTAMP_MAX_SIZE (VOD_INT32_LEN + sizeof(":00:00.000") - 1)

#define TTML_HEADER                                \
	"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" \
	"<tt xmlns=\"http://www.w3.org/ns/ttml\">\n"   \
	"  <head/>\n"                                  \
	"  <body>\n"                                   \
	"    <div>\n"

#define TTML_FOOTER \
	"    </div>\n"  \
	"  </body>\n"   \
	"</tt>\n"

#define TTML_P_HEADER_PART1 "      <p begin=\""
#define TTML_P_HEADER_PART2 "\" end=\""
#define TTML_P_HEADER_PART3 "\">"
#define TTML_P_FOOTER "</p>\n"

#define TTML_P_MAX_SIZE                  \
	((sizeof(TTML_P_HEADER_PART1) - 1)   \
	 + (sizeof(TTML_P_HEADER_PART2) - 1) \
	 + (sizeof(TTML_P_HEADER_PART3) - 1) \
	 + TTML_TIMESTAMP_MAX_SIZE * 2       \
	 + (sizeof(TTML_P_FOOTER) - 1))

static u_char*
ttml_builder_write_timestamp(u_char* p, uint64_t timestamp) {
	return vod_sprintf(
		p,
		TTML_TIMESTAMP_FORMAT,
		(uint32_t)(timestamp / 3600000),
		(uint32_t)((timestamp / 60000) % 60),
		(uint32_t)((timestamp / 1000) % 60),
		(uint32_t)(timestamp % 1000)
	);
}

static u_char*
ttml_copy_payload_without_styles(u_char* p, u_char* src, uint32_t len) {
	u_char* end = src + len;
	u_char* next_lt;

	// skip the cue settings (until first webvtt newline)
	for (; src < end; src++) {
		if (*src == '\r') {
			src++;
			if (*src == '\n') {
				src++;
			}
			break;
		} else if (*src == '\n') {
			src++;
			break;
		}
	}

	for (;;) {
		// copy up to next lt
		next_lt = memchr(src, '<', end - src);
		if (next_lt == NULL) {
			p = vod_copy(p, src, end - src);
			break;
		}

		p = vod_copy(p, src, next_lt - src);

		// skip up to next gt
		src = memchr(next_lt, '>', end - next_lt);
		if (src == NULL) {
			break;
		}
		src++;
	}

	return p;
}

size_t
ttml_builder_get_max_size(media_set_t* media_set) {
	media_track_t* cur_track;
	size_t result = (sizeof(TTML_HEADER) - 1) + (sizeof(TTML_FOOTER) - 1);

	for (cur_track = media_set->filtered_tracks; cur_track < media_set->filtered_tracks_end; cur_track++) {
		result += cur_track->total_frames_size + TTML_P_MAX_SIZE * cur_track->frame_count;
	}

	return result;
}

u_char*
ttml_builder_write(media_set_t* media_set, u_char* p) {
	frame_list_part_t* part;
	media_track_t* cur_track;
	input_frame_t* cur_frame;
	input_frame_t* last_frame;
	uint64_t start_time;
	uint32_t id_size;
	u_char* src;

	p = vod_copy(p, TTML_HEADER, sizeof(TTML_HEADER) - 1);

	for (cur_track = media_set->filtered_tracks; cur_track < media_set->filtered_tracks_end; cur_track++) {
		start_time = cur_track->clip_start_time + cur_track->first_frame_time_offset;
		part = &cur_track->frames;
		last_frame = part->last_frame;
		for (cur_frame = part->first_frame;; cur_frame++) {
			if (cur_frame >= last_frame) {
				if (part->next == NULL) {
					break;
				}
				part = part->next;
				cur_frame = part->first_frame;
				last_frame = part->last_frame;
			}

			// open p tag
			p = vod_copy(p, TTML_P_HEADER_PART1, sizeof(TTML_P_HEADER_PART1) - 1);
			p = ttml_builder_write_timestamp(p, start_time);
			p = vod_copy(p, TTML_P_HEADER_PART2, sizeof(TTML_P_HEADER_PART2) - 1);
			p = ttml_builder_write_timestamp(p, start_time + cur_frame->pts_delay);
			p = vod_copy(p, TTML_P_HEADER_PART3, sizeof(TTML_P_HEADER_PART3) - 1);
			start_time += cur_frame->duration;

			// cue body
			src = (u_char*)(uintptr_t)cur_frame->offset;
			id_size = cur_frame->key_frame;
			src += id_size;
			p = ttml_copy_payload_without_styles(p, src, cur_frame->size - id_size);

			// close p tag
			p = vod_copy(p, TTML_P_FOOTER, sizeof(TTML_P_FOOTER) - 1);
		}
	}

	p = vod_copy(p, TTML_FOOTER, sizeof(TTML_FOOTER) - 1);
	return p;
}
