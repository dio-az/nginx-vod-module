#include "dash_packager.h"
#include "../manifest_utils.h"
#include "../mp4/mp4_defs.h"
#include "../mp4/mp4_fragment.h"

// macros
#define dash_rescale_millis(millis) ((millis) * (DASH_TIMESCALE / 1000))

// constants
#define VOD_DASH_MAX_FRAME_RATE_LEN (1 + 2 * VOD_INT32_LEN)

static const char mpd_header_vod[] =
	"<?xml version=\"1.0\"?>\n"
	"<MPD\n"
	"    xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
	"    xmlns=\"urn:mpeg:dash:schema:mpd:2011\"\n"
	"    xsi:schemaLocation=\"urn:mpeg:dash:schema:mpd:2011 http://standards.iso.org/ittf/PubliclyAvailableStandards/MPEG-DASH_schema_files/DASH-MPD.xsd\"\n"
	"    type=\"static\"\n"
	"    mediaPresentationDuration=\"PT%uD.%03uDS\"\n"
	"    minBufferTime=\"PT%uDS\"\n"
	"    profiles=\"%V\">\n";

static const char mpd_header_live[] =
	"<?xml version=\"1.0\"?>\n"
	"<MPD\n"
	"    xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
	"    xmlns=\"urn:mpeg:dash:schema:mpd:2011\"\n"
	"    xsi:schemaLocation=\"urn:mpeg:dash:schema:mpd:2011 http://standards.iso.org/ittf/PubliclyAvailableStandards/MPEG-DASH_schema_files/DASH-MPD.xsd\"\n"
	"    type=\"dynamic\"\n"
	"    minimumUpdatePeriod=\"PT%uD.%03uDS\"\n"
	"    availabilityStartTime=\"%04d-%02d-%02dT%02d:%02d:%02dZ\"\n"
	"    publishTime=\"%04d-%02d-%02dT%02d:%02d:%02dZ\"\n"
	"    timeShiftBufferDepth=\"PT%uD.%03uDS\"\n"
	"    minBufferTime=\"PT%uD.%03uDS\"\n"
	"    suggestedPresentationDelay=\"PT%uD.%03uDS\"\n"
	"    profiles=\"%V\">\n"
	"  <UTCTiming\n"
	"    schemeIdUri=\"urn:mpeg:dash:utc:direct:2014\"\n"
	"    value=\"%04d-%02d-%02dT%02d:%02d:%02dZ\"/>\n";

static const char mpd_baseurl[] = "  <BaseURL>%V</BaseURL>\n";

static const u_char mpd_period_header[] = "  <Period>\n";

static const char mpd_period_header_duration[] = "  <Period id=\"%uD\" duration=\"PT%uD.%03uDS\">\n";

static const char mpd_period_header_start[] = "  <Period id=\"%uD\" start=\"PT%uD.%03uDS\">\n";

static const u_char mpd_period_header_start_zero[] = "  <Period id=\"0\" start=\"PT0S\">\n";

static const char mpd_period_header_start_duration[] =
	"  <Period id=\"%uD\" start=\"PT%uD.%03uDS\" duration=\"PT%uD.%03uDS\">\n";

static const char mpd_adaptation_header_video[] =
	"    <AdaptationSet\n"
	"        id=\"%uD\"\n"
	"        group=\"1\"\n"
	"        contentType=\"video\"\n"
	"        lang=\"%V\"\n"
	"        segmentAlignment=\"true\"\n"
	"        maxWidth=\"%uD\"\n"
	"        maxHeight=\"%uD\"\n"
	"        maxFrameRate=\"%V\">\n";

static const char mpd_adaptation_header_audio[] =
	"    <AdaptationSet\n"
	"        id=\"%uD\"\n"
	"        group=\"2\"\n"
	"        contentType=\"audio\"\n"
	"        lang=\"%V\"\n"
	"        segmentAlignment=\"true\">\n";

static const char mpd_adaptation_header_subtitle_smpte_tt[] =
	"    <AdaptationSet\n"
	"        id=\"%uD\"\n"
	"        group=\"3\"\n"
	"        contentType=\"text\"\n"
	"        lang=\"%V\">\n";

static const char mpd_adaptation_header_subtitle_vtt[] =
	"    <AdaptationSet\n"
	"        id=\"%uD\"\n"
	"        group=\"3\"\n"
	"        contentType=\"text\"\n"
	"        lang=\"%V\"\n"
	"        mimeType=\"text/vtt\">\n";

static const char mpd_label[] = "      <Label>%V</Label>\n";

static const char mpd_audio_channel_config[] =
	"      <AudioChannelConfiguration\n"
	"          schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\"\n"
	"          value=\"%uD\"/>\n";

static const char mpd_audio_channel_config_eac3[] =
	"      <AudioChannelConfiguration\n"
	"          schemeIdUri=\"tag:dolby.com,2014:dash:audio_channel_configuration:2011\"\n"
	"          value=\"%uxD\"/>\n";

static const char mpd_role[] = "      <Role schemeIdUri=\"urn:mpeg:dash:role:2011\" value=\"%V\"/>\n";

static const char mpd_representation_header_video[] =
	"      <Representation\n"
	"          id=\"%V\"\n"
	"          mimeType=\"%V\"\n"
	"          codecs=\"%V\"\n"
	"          width=\"%uD\"\n"
	"          height=\"%uD\"\n"
	"          frameRate=\"%V\"\n"
	"          sar=\"1:1\"\n"
	"          startWithSAP=\"1\"\n"
	"          bandwidth=\"%uD\">\n";

static const char mpd_representation_header_audio[] =
	"      <Representation\n"
	"          id=\"%V\"\n"
	"          mimeType=\"%V\"\n"
	"          codecs=\"%V\"\n"
	"          audioSamplingRate=\"%uD\"\n"
	"          startWithSAP=\"1\"\n"
	"          bandwidth=\"%uD\">\n";

static const char mpd_representation_header_subtitle_smpte_tt[] =
	"      <Representation\n"
	"          id=\"%V\"\n"
	"          mimeType=\"application/mp4\"\n"
	"          codecs=\"stpp\"\n"
	"          startWithSAP=\"1\"\n"
	"          bandwidth=\"0\">\n";

static const u_char mpd_representation_footer[] = "      </Representation>\n";

static const char mpd_representation_subtitle_vtt[] =
	"      <Representation\n"
	"          id=\"textstream_%uD\"\n"
	"          bandwidth=\"0\">\n"
	"        <BaseURL>%V%V-%s%V.vtt</BaseURL>\n"
	"      </Representation>\n";

static const char mpd_segment_template_fixed[] =
	"      <SegmentTemplate\n"
	"          timescale=\"1000\"\n"
	"          media=\"%V%V-$Number$-%s$RepresentationID$.%V\"\n"
	"          initialization=\"%V%V-%s$RepresentationID$.%V\"\n"
	"          duration=\"%ui\"\n"
	"          startNumber=\"%uD\">\n"
	"      </SegmentTemplate>\n";

static const char mpd_segment_template_header[] =
	"      <SegmentTemplate\n"
	"          timescale=\"1000\"\n"
	"          media=\"%V%V-$Number$-$RepresentationID$.%V\"\n"
	"          initialization=\"%V%V-%s$RepresentationID$.%V\"\n"
	"          startNumber=\"%uD\">\n"
	"        <SegmentTimeline>\n";

static const char mpd_segment_repeat[] = "          <S d=\"%uD\" r=\"%uD\"/>\n";

static const char mpd_segment[] = "          <S d=\"%uD\"/>\n";

static const char mpd_segment_repeat_time[] = "          <S t=\"%uL\" d=\"%uD\" r=\"%uD\"/>\n";

static const char mpd_segment_time[] = "          <S t=\"%uL\" d=\"%uD\"/>\n";

static const u_char mpd_segment_template_footer[] =
	"        </SegmentTimeline>\n"
	"      </SegmentTemplate>\n";

static const char mpd_segment_list_header[] =
	"      <SegmentList timescale=\"1000\" duration=\"%ui\" startNumber=\"%uD\">\n"
	"        <Initialization sourceURL=\"%V%V-%s%V.%V\"/>\n";

static const char mpd_segment_url[] = "        <SegmentURL media=\"%V%V-%uD-%V.%V\"/>\n";

static const u_char mpd_segment_list_footer[] = "      </SegmentList>\n";

static const u_char mpd_adaptation_footer[] = "    </AdaptationSet>\n";

static const u_char mpd_period_footer[] = "  </Period>\n";

static const u_char mpd_footer[] = "</MPD>\n";

#define MAX_TRACK_SPEC_LENGTH (sizeof("f-v") + 2 * VOD_INT32_LEN)
#define MAX_CLIP_SPEC_LENGTH (sizeof("c-") + VOD_INT32_LEN)
#define MAX_INDEX_SHIFT_LENGTH (sizeof("i-") + VOD_INT32_LEN)
#define MAX_MIME_TYPE_SIZE (sizeof("video/webm") - 1)
#define MAX_FILE_EXT_SIZE (sizeof("webm") - 1)

static const vod_str_t unknown_lang = vod_string("und");

// typedefs
typedef struct {
	dash_manifest_config_t* conf;
	vod_str_t base_url;
	media_set_t* media_set;
	dash_manifest_extensions_t extensions;
	u_char* base_url_temp_buffer;
	segment_durations_t segment_durations[MEDIA_TYPE_COUNT];
	segment_duration_item_t** cur_duration_items;
	uint32_t clip_index;
	uint64_t clip_start_time;
	uint64_t segment_base_time;
	adaptation_sets_t adaptation_sets;
} write_period_context_t;

typedef struct {
	vod_str_t mime_type;
	vod_str_t init_file_ext;
	vod_str_t frag_file_ext;
} dash_codec_info_t;

// fragment atoms
typedef struct {
	uint32_t timescale;
	uint64_t total_frames_duration;
	uint64_t earliest_pres_time;
} sidx_params_t;

typedef struct {
	u_char version[1];
	u_char flags[3];
	u_char reference_id[4];
	u_char timescale[4];
	u_char earliest_pres_time[4];
	u_char first_offset[4];
	u_char reserved[2];
	u_char reference_count[2];
	u_char reference_size[4]; // NOTE: from this point forward, assuming reference_count == 1
	u_char subsegment_duration[4];
	u_char sap_type[1];
	u_char sap_delta_time[3];
} sidx_atom_t;

typedef struct {
	u_char version[1];
	u_char flags[3];
	u_char reference_id[4];
	u_char timescale[4];
	u_char earliest_pres_time[8];
	u_char first_offset[8];
	u_char reserved[2];
	u_char reference_count[2];
	u_char reference_size[4]; // NOTE: from this point forward, assuming reference_count == 1
	u_char subsegment_duration[4];
	u_char sap_type[1];
	u_char sap_delta_time[3];
} sidx64_atom_t;

// fixed fragment atoms

// clang-format off
static const u_char styp_atom[] = {
	0x00, 0x00, 0x00, 0x20, // size = 32
	0x73, 0x74, 0x79, 0x70, // styp
	0x69, 0x73, 0x6F, 0x36, // major_brand = iso6
	0x00, 0x00, 0x00, 0x00, // minor_version
	0x69, 0x73, 0x6F, 0x36, // compatible_brand = iso6
	0x63, 0x6D, 0x66, 0x73, // compatible_brand = cmfs
	// backward compatibility
	0x64, 0x61, 0x73, 0x68, // compatible_brand = dash
	0x69, 0x73, 0x6F, 0x6D, // compatible_brand = isom
};
// clang-format on

static dash_codec_info_t dash_codecs[VOD_CODEC_ID_COUNT] = {
	{vod_null_string, vod_null_string, vod_null_string}, // invalid

	{vod_string("video/mp4"), vod_string("mp4"), vod_string("m4s")},    // avc
	{vod_string("video/mp4"), vod_string("mp4"), vod_string("m4s")},    // hevc
	{vod_string("video/webm"), vod_string("webm"), vod_string("webm")}, // vp8
	{vod_string("video/webm"), vod_string("webm"), vod_string("webm")}, // vp9
	{vod_string("video/webm"), vod_string("webm"), vod_string("webm")}, // av1

	{vod_string("audio/mp4"), vod_string("mp4"), vod_string("m4s")},    // aac
	{vod_string("audio/mp4"), vod_string("mp4"), vod_string("m4s")},    // ac3
	{vod_string("audio/mp4"), vod_string("mp4"), vod_string("m4s")},    // eac3
	{vod_string("audio/mp4"), vod_string("mp4"), vod_string("m4s")},    // mp3
	{vod_string("audio/mp4"), vod_string("mp4"), vod_string("m4s")},    // dts
	{vod_string("audio/webm"), vod_string("webm"), vod_string("webm")}, // vorbis
	{vod_string("audio/webm"), vod_string("webm"), vod_string("webm")}, // opus
	{vod_null_string, vod_null_string, vod_null_string},                // volumemap
	{vod_null_string, vod_null_string, vod_null_string},                // flac

	{vod_string("application/mp4"), vod_string("mp4"), vod_string("ttml")}, // webvtt
};

// mpd writing code
static bool_t
dash_packager_compare_tracks(uintptr_t bitrate_threshold, const media_info_t* mi1, const media_info_t* mi2) {
	uint32_t role_index;
	vod_str_t* role1;
	vod_str_t* role2;

	if (mi1->bitrate == 0
	    || mi2->bitrate == 0
	    || mi1->bitrate + bitrate_threshold <= mi2->bitrate
	    || mi2->bitrate + bitrate_threshold <= mi1->bitrate) {
		return FALSE;
	}

	if (mi1->codec_name.len != mi2->codec_name.len
	    || vod_memcmp(mi1->codec_name.data, mi2->codec_name.data, mi2->codec_name.len) != 0) {
		return FALSE;
	}

	if (mi1->media_type == MEDIA_TYPE_VIDEO) {
		return mi1->u.video.width == mi2->u.video.width && mi1->u.video.height == mi2->u.video.height;
	}

	if (mi1->tags.label.len == 0 || mi2->tags.label.len == 0) {
		return TRUE;
	}

	if (!vod_str_equals(mi1->tags.label, mi2->tags.label)) {
		return FALSE;
	}

	if (mi1->tags.is_forced != mi2->tags.is_forced) {
		return FALSE;
	}

	if (!vod_str_equals(mi1->tags.characteristics, mi2->tags.characteristics)) {
		return FALSE;
	}

	if (mi1->tags.roles.nelts != mi2->tags.roles.nelts) {
		return FALSE;
	}

	for (role_index = 0; role_index < mi1->tags.roles.nelts; role_index++) {
		role1 = (vod_str_t*)mi1->tags.roles.elts + role_index;
		role2 = (vod_str_t*)mi2->tags.roles.elts + role_index;

		if (!vod_str_equals(*role1, *role2)) {
			return FALSE;
		}
	}

	return TRUE;
}

static void
dash_packager_get_clip_spec(u_char* result, media_set_t* media_set, uint32_t clip_index) {
	if (media_set->use_discontinuity && media_set->initial_clip_index != INVALID_CLIP_INDEX) {
		vod_sprintf(result, "c%uD-%Z", media_set->initial_clip_index + clip_index + 1);
	} else {
		result[0] = '\0';
	}
}

static void
dash_packager_get_track_spec(
	vod_str_t* result, media_set_t* media_set, media_sequence_t* sequence, media_track_t* track
) {
	u_char* p = manifest_utils_append_track_spec(
		result->data, sequence, track, media_set->has_multi_sequences && sequence->index != INVALID_SEQUENCE_INDEX
	);

	result->len = p - result->data;
}

static uint32_t
dash_packager_get_cur_clip_segment_count(
	segment_durations_t* segment_durations, segment_duration_item_t** cur_item_ptr
) {
	segment_duration_item_t* cur_item;
	segment_duration_item_t* last_item = segment_durations->items + segment_durations->item_count;
	uint32_t result = 0;
	bool_t first_time = TRUE;

	for (cur_item = *cur_item_ptr; cur_item < last_item; cur_item++) {
		// stop on discontinuity, will get called again for the next period
		if (cur_item->discontinuity && !first_time) {
			break;
		}

		first_time = FALSE;

		result += cur_item->repeat_count;
	}

	*cur_item_ptr = cur_item;

	return result;
}

static u_char*
dash_packager_write_segment_template(
	u_char* p,
	dash_manifest_config_t* conf,
	uint32_t start_number,
	uint32_t clip_relative_index,
	u_char* clip_spec,
	media_set_t* media_set,
	media_track_t* reference_track,
	vod_str_t* base_url
) {
	u_char index_shift_str[MAX_INDEX_SHIFT_LENGTH];

	index_shift_str[0] = '\0';
	if (media_set->use_discontinuity) {
		if (start_number > clip_relative_index) {
			vod_sprintf(index_shift_str, "i%uD-%Z", start_number - clip_relative_index);
			start_number = clip_relative_index;
		}
	} else {
		start_number = 0;
	}

	// NOTE: SegmentTemplate is currently printed in the adaptation set level, so it is not possible
	// to mix mp4 and webm representations for the same media type
	p = vod_sprintf(
		p,
		mpd_segment_template_fixed,
		base_url,
		&conf->fragment_file_name_prefix,
		index_shift_str,
		&dash_codecs[reference_track->media_info.codec_id].frag_file_ext,
		base_url,
		&conf->init_file_name_prefix,
		clip_spec,
		&dash_codecs[reference_track->media_info.codec_id].init_file_ext,
		media_set->segmenter_conf->segment_duration,
		start_number + 1
	);

	return p;
}

static u_char*
dash_packager_write_segment_timeline(
	u_char* p,
	dash_manifest_config_t* conf,
	uint32_t start_number,
	uint64_t clip_start_time,
	u_char* clip_spec,
	media_track_t* reference_track,
	segment_durations_t* segment_durations,
	segment_duration_item_t** cur_item_ptr,
	vod_str_t* base_url
) {
	segment_duration_item_t* cur_item;
	segment_duration_item_t* last_item = segment_durations->items + segment_durations->item_count;
	uint64_t start_time;
	uint32_t duration;
	bool_t first_time = TRUE;

	if (segment_durations->start_time > clip_start_time) {
		start_time = segment_durations->start_time - clip_start_time;
	} else {
		start_time = 0;
	}

	// NOTE: SegmentTemplate is currently printed in the adaptation set level, so it is not possible
	// to mix mp4 and webm representations for the same media type
	p = vod_sprintf(
		p,
		mpd_segment_template_header,
		base_url,
		&conf->fragment_file_name_prefix,
		&dash_codecs[reference_track->media_info.codec_id].frag_file_ext,
		base_url,
		&conf->init_file_name_prefix,
		clip_spec,
		&dash_codecs[reference_track->media_info.codec_id].init_file_ext,
		start_number + 1
	);

	for (cur_item = *cur_item_ptr; cur_item < last_item; cur_item++) {
		// stop on discontinuity, will get called again for the next period
		if (cur_item->discontinuity && !first_time) {
			break;
		}

		duration = (uint32_t)rescale_time(cur_item->duration, segment_durations->timescale, 1000);

		if (first_time && start_time != 0) {
			// output the time
			if (cur_item->repeat_count == 1) {
				p = vod_sprintf(p, mpd_segment_time, start_time, duration);
			} else if (cur_item->repeat_count > 1) {
				p = vod_sprintf(
					p, mpd_segment_repeat_time, start_time, duration, cur_item->repeat_count - 1
				);
			}
		} else {
			// don't output the time
			if (cur_item->repeat_count == 1) {
				p = vod_sprintf(p, mpd_segment, duration);
			} else if (cur_item->repeat_count > 1) {
				p = vod_sprintf(p, mpd_segment_repeat, duration, cur_item->repeat_count - 1);
			}
		}

		first_time = FALSE;
	}

	*cur_item_ptr = cur_item;

	p = vod_copy(p, mpd_segment_template_footer, sizeof(mpd_segment_template_footer) - 1);

	return p;
}

static void
dash_packager_get_segment_list_base_url(
	write_period_context_t* context, media_track_t* cur_track, vod_str_t* result, uint32_t* sequence_index
) {
	vod_str_t* base_url = &context->base_url;
	u_char* base_url_temp_buffer = context->base_url_temp_buffer;

	if (base_url->len == 0) {
		result->data = NULL;
		result->len = 0;
		return;
	}

	result->data = base_url_temp_buffer;
	base_url_temp_buffer = vod_copy(base_url_temp_buffer, base_url->data, base_url->len);
	if (cur_track->file_info.uri.len != 0) {
		base_url_temp_buffer =
			vod_copy(base_url_temp_buffer, cur_track->file_info.uri.data, cur_track->file_info.uri.len);

		// no need to pass the sequence index since we have a direct uri
		*sequence_index = INVALID_SEQUENCE_INDEX;
	} else {
		base_url_temp_buffer =
			vod_copy(base_url_temp_buffer, context->media_set->uri.data, context->media_set->uri.len);
	}
	*base_url_temp_buffer++ = '/';
	result->len = base_url_temp_buffer - result->data;
}

static u_char*
dash_packager_write_segment_list(
	u_char* p,
	write_period_context_t* context,
	uint32_t start_number,
	u_char* clip_spec,
	media_sequence_t* cur_sequence,
	media_track_t* cur_track,
	uint32_t segment_count
) {
	dash_manifest_config_t* conf = context->conf;
	media_set_t* media_set = context->media_set;
	vod_str_t track_spec;
	vod_str_t cur_base_url;
	u_char track_spec_buffer[MAX_TRACK_SPEC_LENGTH];
	uint32_t i;

	track_spec.data = track_spec_buffer;

	// build the base url
	dash_packager_get_segment_list_base_url(context, cur_track, &cur_base_url, &cur_sequence->index);

	// get the track specification
	dash_packager_get_track_spec(&track_spec, media_set, cur_sequence, cur_track);

	// write the header
	p = vod_sprintf(
		p,
		mpd_segment_list_header,
		media_set->segmenter_conf->segment_duration,
		context->clip_index == 0 ? media_set->initial_segment_clip_relative_index + 1 : 1,
		&cur_base_url,
		&conf->init_file_name_prefix,
		clip_spec,
		&track_spec,
		&dash_codecs[cur_track->media_info.codec_id].init_file_ext
	);

	// write the urls
	for (i = 0; i < segment_count; i++) {
		p = vod_sprintf(
			p,
			mpd_segment_url,
			&cur_base_url,
			&conf->fragment_file_name_prefix,
			start_number + i + 1,
			&track_spec,
			&dash_codecs[cur_track->media_info.codec_id].frag_file_ext
		);
	}

	p = vod_copy(p, mpd_segment_list_footer, sizeof(mpd_segment_list_footer) - 1);

	return p;
}

static uint32_t
dash_packager_find_gcd(uint32_t num1, uint32_t num2) {
	while (num1 != num2) {
		if (num1 > num2) {
			num1 -= num2;
		} else {
			num2 -= num1;
		}
	}

	return num1;
}

static void
dash_packager_write_frame_rate(uint32_t duration, uint32_t timescale, vod_str_t* result) {
	uint32_t gcd = dash_packager_find_gcd(duration, timescale);
	u_char* p = result->data;

	duration /= gcd;
	timescale /= gcd;

	if (duration == 1) {
		result->len = vod_sprintf(p, "%uD", timescale) - p;
	} else {
		result->len = vod_sprintf(p, "%uD/%uD", timescale, duration) - p;
	}
}

static u_char*
dash_packager_write_roles(u_char* p, media_info_t* media_info) {
	for (uint32_t role_index = 0; role_index < media_info->tags.roles.nelts; role_index++) {
		p = vod_sprintf(p, mpd_role, (vod_str_t*)media_info->tags.roles.elts + role_index);
	}

	return p;
}

static uint32_t
dash_packager_get_eac3_channel_config(media_info_t* media_info) {
	static uint64_t channel_layout_map[] = {
		VOD_CH_FRONT_LEFT,
		VOD_CH_FRONT_CENTER,
		VOD_CH_FRONT_RIGHT,
		VOD_CH_SIDE_LEFT,
		VOD_CH_SIDE_RIGHT,
		VOD_CH_FRONT_LEFT_OF_CENTER | VOD_CH_FRONT_RIGHT_OF_CENTER,
		VOD_CH_BACK_LEFT | VOD_CH_BACK_RIGHT,
		VOD_CH_BACK_CENTER,
		VOD_CH_TOP_CENTER,
		VOD_CH_SURROUND_DIRECT_LEFT | VOD_CH_SURROUND_DIRECT_RIGHT,
		VOD_CH_WIDE_LEFT | VOD_CH_WIDE_RIGHT,
		VOD_CH_TOP_FRONT_LEFT | VOD_CH_TOP_FRONT_RIGHT,
		VOD_CH_TOP_FRONT_CENTER,
		VOD_CH_TOP_BACK_LEFT | VOD_CH_TOP_BACK_RIGHT,
		VOD_CH_LOW_FREQUENCY_2,
		VOD_CH_LOW_FREQUENCY,
	};

	uint64_t cur;
	uint32_t result = 0;
	uint32_t i;

	for (i = 0; i < vod_array_entries(channel_layout_map); i++) {
		cur = channel_layout_map[i];
		if ((media_info->u.audio.channel_layout & cur) == cur) {
			result |= 1 << (15 - i);
		}
	}

	return result;
}

static u_char*
dash_packager_write_mpd_period(u_char* p, write_period_context_t* context) {
	segment_duration_item_t** cur_duration_items;
	media_sequence_t* cur_sequence;
	adaptation_set_t* adaptation_set;
	media_track_t* reference_track = NULL;
	media_track_t** cur_track_ptr;
	media_track_t* cur_track;
	media_set_t* media_set = context->media_set;
	const vod_str_t* lang_str;
	vod_str_t representation_id;
	vod_str_t cur_base_url;
	vod_str_t frame_rate;
	u_char representation_id_buffer[MAX_TRACK_SPEC_LENGTH];
	u_char frame_rate_buffer[VOD_DASH_MAX_FRAME_RATE_LEN];
	u_char clip_spec[MAX_CLIP_SPEC_LENGTH];
	uint64_t clip_start_offset;
	uint32_t clip_duration;
	uint32_t filtered_clip_offset;
	uint32_t max_width = 0;
	uint32_t max_height = 0;
	uint32_t max_framerate_duration = 0;
	uint32_t segment_count = 0;
	uint32_t start_number;
	uint32_t adapt_id = 1;
	uint32_t subtitle_adapt_id = 0;

	frame_rate.data = frame_rate_buffer;
	representation_id.data = representation_id_buffer;

	if (media_set->use_discontinuity) {
		clip_duration = media_set->timing.durations[context->clip_index];
		switch (media_set->type) {
		case MEDIA_SET_VOD:
			p = vod_sprintf(
				p,
				mpd_period_header_duration,
				media_set->initial_clip_index + context->clip_index,
				clip_duration / 1000,
				clip_duration % 1000
			);
			break;

		case MEDIA_SET_LIVE:
			clip_start_offset = context->clip_start_time - context->segment_base_time;

			if (context->clip_index + 1 < media_set->timing.total_count
			    && media_set->timing.times[context->clip_index] + clip_duration
			           != media_set->timing.times[context->clip_index + 1]) {
				// there is a gap after this clip, output start time and duration
				clip_duration +=
					media_set->timing.times[context->clip_index] - context->clip_start_time;

				p = vod_sprintf(
					p,
					mpd_period_header_start_duration,
					media_set->initial_clip_index + context->clip_index,
					clip_start_offset / 1000,
					clip_start_offset % 1000,
					clip_duration / 1000,
					clip_duration % 1000
				);
			} else {
				// last clip / no gap, output only the start time
				p = vod_sprintf(
					p,
					mpd_period_header_start,
					media_set->initial_clip_index + context->clip_index,
					clip_start_offset / 1000,
					clip_start_offset % 1000
				);
			}
			break;
		}
	} else {
		switch (media_set->type) {
		case MEDIA_SET_VOD:
			p = vod_copy(p, mpd_period_header, sizeof(mpd_period_header) - 1);
			break;

		case MEDIA_SET_LIVE:
			p = vod_copy(p, mpd_period_header_start_zero, sizeof(mpd_period_header_start_zero) - 1);
			break;
		}
	}

	// NOTE: clip_index can be greater than clip count when consistentSequenceMediaInfo is true
	filtered_clip_offset = context->clip_index < media_set->clip_count
	                         ? context->clip_index * media_set->total_track_count
	                         : 0;

	dash_packager_get_clip_spec(clip_spec, media_set, context->clip_index);

	// print the adaptation sets
	for (
		adaptation_set = context->adaptation_sets.first, cur_duration_items = context->cur_duration_items;
		adaptation_set < context->adaptation_sets.last;
		adaptation_set++, cur_duration_items++
	) {
		reference_track = adaptation_set->last[-1] + filtered_clip_offset;

		lang_str = &reference_track->media_info.tags.lang_str;
		if (lang_str->len == 0) {
			lang_str = &unknown_lang;
		}

		switch (adaptation_set->type) {
		case MEDIA_TYPE_VIDEO:
			// get the max width, height and frame rate
			for (cur_track_ptr = adaptation_set->first; cur_track_ptr < adaptation_set->last; cur_track_ptr++) {
				cur_track = (*cur_track_ptr) + filtered_clip_offset;

				if (cur_track->media_info.u.video.width > max_width) {
					max_width = cur_track->media_info.u.video.width;
				}

				if (cur_track->media_info.u.video.height > max_height) {
					max_height = cur_track->media_info.u.video.height;
				}

				if (max_framerate_duration == 0
				    || max_framerate_duration > cur_track->media_info.min_frame_duration) {
					max_framerate_duration = cur_track->media_info.min_frame_duration;
				}
			}

			// print the header
			dash_packager_write_frame_rate(max_framerate_duration, DASH_TIMESCALE, &frame_rate);

			p = vod_sprintf(
				p, mpd_adaptation_header_video, adapt_id++, lang_str, max_width, max_height, &frame_rate
			);

			if (reference_track->media_info.tags.label.len > 0) {
				p = vod_sprintf(p, mpd_label, &reference_track->media_info.tags.label);
			}
			break;

		case MEDIA_TYPE_AUDIO:
			p = vod_sprintf(p, mpd_adaptation_header_audio, adapt_id++, lang_str);

			if (reference_track->media_info.tags.label.len > 0) {
				p = vod_sprintf(p, mpd_label, &reference_track->media_info.tags.label);
			}

			if (reference_track->media_info.codec_id == VOD_CODEC_ID_EAC3) {
				p = vod_sprintf(
					p,
					mpd_audio_channel_config_eac3,
					dash_packager_get_eac3_channel_config(&reference_track->media_info)
				);
			} else {
				p = vod_sprintf(
					p, mpd_audio_channel_config, (uint32_t)reference_track->media_info.u.audio.channels
				);
			}
			break;

		case MEDIA_TYPE_SUBTITLE:
			if (context->conf->subtitle_format == SUBTITLE_FORMAT_SMPTE_TT) {
				p = vod_sprintf(p, mpd_adaptation_header_subtitle_smpte_tt, adapt_id++, lang_str);

				if (reference_track->media_info.tags.label.len > 0) {
					p = vod_sprintf(p, mpd_label, &reference_track->media_info.tags.label);
				}
				break;
			}

			cur_sequence = reference_track->file_info.source->sequence;
			if (context->conf->manifest_format == FORMAT_SEGMENT_LIST) {
				dash_packager_get_segment_list_base_url(
					context, reference_track, &cur_base_url, &cur_sequence->index
				);
			} else {
				cur_base_url = context->base_url;
			}

			dash_packager_get_track_spec(&representation_id, media_set, cur_sequence, reference_track);

			p = vod_sprintf(p, mpd_adaptation_header_subtitle_vtt, adapt_id++, lang_str);

			if (reference_track->media_info.tags.label.len > 0) {
				p = vod_sprintf(p, mpd_label, &reference_track->media_info.tags.label);
			}

			p = dash_packager_write_roles(p, &reference_track->media_info);

			p = vod_sprintf(
				p,
				mpd_representation_subtitle_vtt,
				subtitle_adapt_id++,
				&cur_base_url,
				&context->conf->subtitle_file_name_prefix,
				clip_spec,
				&representation_id
			);

			p = vod_copy(p, mpd_adaptation_footer, sizeof(mpd_adaptation_footer) - 1);

			continue;
		}

		if (context->extensions.adaptation_set.write != NULL) {
			p = context->extensions.adaptation_set.write(
				context->extensions.adaptation_set.context, p, reference_track
			);
		}

		p = dash_packager_write_roles(p, &reference_track->media_info);

		// get the segment index start number
		start_number = (*cur_duration_items)[0].segment_index;

		// print the segment template
		switch (context->conf->manifest_format) {
		case FORMAT_SEGMENT_TEMPLATE:
			// increment cur_duration_items (don't really need the count)
			dash_packager_get_cur_clip_segment_count(
				&context->segment_durations[adaptation_set->type], cur_duration_items
			);

			p = dash_packager_write_segment_template(
				p,
				context->conf,
				start_number,
				context->clip_index == 0 ? media_set->initial_segment_clip_relative_index : 0,
				clip_spec,
				media_set,
				reference_track,
				&context->base_url
			);
			break;

		case FORMAT_SEGMENT_TIMELINE:
			p = dash_packager_write_segment_timeline(
				p,
				context->conf,
				start_number,
				context->clip_start_time,
				clip_spec,
				reference_track,
				&context->segment_durations[adaptation_set->type],
				cur_duration_items,
				&context->base_url
			);
			break;

		case FORMAT_SEGMENT_LIST:
			if (media_set->use_discontinuity) {
				segment_count = dash_packager_get_cur_clip_segment_count(
					&context->segment_durations[adaptation_set->type], cur_duration_items
				);
			} else {
				segment_count = context->segment_durations[adaptation_set->type].segment_count;
			}
			break;
		}

		// print the representations
		for (cur_track_ptr = adaptation_set->first; cur_track_ptr < adaptation_set->last; cur_track_ptr++) {
			cur_track = (*cur_track_ptr) + filtered_clip_offset;
			cur_sequence = cur_track->file_info.source->sequence;

			dash_packager_get_track_spec(&representation_id, media_set, cur_sequence, cur_track);

			switch (adaptation_set->type) {
			case MEDIA_TYPE_VIDEO:
				dash_packager_write_frame_rate(
					cur_track->media_info.min_frame_duration, DASH_TIMESCALE, &frame_rate
				);

				p = vod_sprintf(
					p,
					mpd_representation_header_video,
					&representation_id,
					&dash_codecs[cur_track->media_info.codec_id].mime_type,
					&cur_track->media_info.codec_name,
					(uint32_t)cur_track->media_info.u.video.width,
					(uint32_t)cur_track->media_info.u.video.height,
					&frame_rate,
					cur_track->media_info.bitrate
				);
				break;

			case MEDIA_TYPE_AUDIO:
				p = vod_sprintf(
					p,
					mpd_representation_header_audio,
					&representation_id,
					&dash_codecs[cur_track->media_info.codec_id].mime_type,
					&cur_track->media_info.codec_name,
					cur_track->media_info.u.audio.sample_rate,
					cur_track->media_info.bitrate
				);
				break;

			case MEDIA_TYPE_SUBTITLE:
				p = vod_sprintf(p, mpd_representation_header_subtitle_smpte_tt, &representation_id);
				break;
			}

			if (context->conf->manifest_format == FORMAT_SEGMENT_LIST) {
				p = dash_packager_write_segment_list(
					p, context, start_number, clip_spec, cur_sequence, cur_track, segment_count
				);
			}

			// write any additional tags
			if (context->extensions.representation.write != NULL) {
				p = context->extensions.representation.write(
					context->extensions.representation.context, p, cur_track
				);
			}

			p = vod_copy(p, mpd_representation_footer, sizeof(mpd_representation_footer) - 1);
		}

		// print the footer
		p = vod_copy(p, mpd_adaptation_footer, sizeof(mpd_adaptation_footer) - 1);
	}

	p = vod_copy(p, mpd_period_footer, sizeof(mpd_period_footer) - 1);

	return p;
}

static size_t
dash_packager_get_segment_list_total_size(
	dash_manifest_config_t* conf,
	media_set_t* media_set,
	segment_durations_t* segment_durations,
	vod_str_t* base_url,
	size_t* base_url_temp_buffer_size
) {
	segment_duration_item_t* cur_duration_item;
	media_track_t* last_track;
	media_track_t* cur_track;
	uint32_t filtered_clip_offset;
	uint32_t max_media_type;
	uint32_t period_count = media_set->use_discontinuity ? media_set->timing.total_count : 1;
	uint32_t segment_count;
	uint32_t clip_index;
	uint32_t media_type;
	size_t base_url_len = 0;
	size_t result = 0;

	switch (conf->subtitle_format) {
	case SUBTITLE_FORMAT_WEBVTT:
		max_media_type = MEDIA_TYPE_SUBTITLE;
		break;

	default: // SUBTITLE_FORMAT_SMPTE_TT
		max_media_type = MEDIA_TYPE_COUNT;
		break;
	}

	for (media_type = 0; media_type < max_media_type; media_type++) {
		if (media_set->track_count[media_type] == 0) {
			continue;
		}

		cur_duration_item = segment_durations[media_type].items;

		for (clip_index = 0; clip_index < period_count; clip_index++) {
			filtered_clip_offset =
				clip_index < media_set->clip_count ? clip_index * media_set->total_track_count : 0;

			if (media_set->use_discontinuity) {
				segment_count = dash_packager_get_cur_clip_segment_count(
					&segment_durations[media_type], &cur_duration_item
				);
			} else {
				segment_count = segment_durations[media_type].segment_count;
			}

			cur_track = media_set->filtered_tracks + filtered_clip_offset;
			last_track = cur_track + media_set->total_track_count;
			for (; cur_track < last_track; cur_track++) {
				if (cur_track->media_info.media_type != media_type) {
					continue;
				}

				if (base_url->len != 0) {
					base_url_len = base_url->len + 1;
					if (cur_track->file_info.uri.len != 0) {
						base_url_len += cur_track->file_info.uri.len;
					} else {
						base_url_len += media_set->uri.len;
					}

					if (base_url_len > *base_url_temp_buffer_size) {
						*base_url_temp_buffer_size = base_url_len;
					}
				}

				result += ((sizeof(mpd_segment_list_header) - 1)
				           + VOD_INT64_LEN
				           + VOD_INT32_LEN
				           + base_url_len
				           + conf->init_file_name_prefix.len
				           + MAX_CLIP_SPEC_LENGTH
				           + MAX_TRACK_SPEC_LENGTH
				           + MAX_FILE_EXT_SIZE)
				        + ((sizeof(mpd_segment_url) - 1)
				           + base_url_len
				           + conf->fragment_file_name_prefix.len
				           + VOD_INT32_LEN
				           + MAX_TRACK_SPEC_LENGTH
				           + MAX_FILE_EXT_SIZE)
				              * segment_count
				        + (sizeof(mpd_segment_list_footer) - 1);
			}
		}
	}

	return result;
}

static void
dash_packager_remove_redundant_tracks(vod_uint_t duplicate_bitrate_threshold, media_set_t* media_set) {
	media_track_t* track1;
	media_track_t* track2;
	media_track_t* remove;
	media_track_t* last_track;
	uint32_t clip_index;

	last_track = media_set->filtered_tracks + media_set->total_track_count;
	for (track1 = media_set->filtered_tracks + 1; track1 < last_track; track1++) {
		for (track2 = media_set->filtered_tracks; track2 < track1; track2++) {
			if (track1->media_info.media_type != track2->media_info.media_type) {
				continue;
			}

			if (!dash_packager_compare_tracks(
					duplicate_bitrate_threshold, &track1->media_info, &track2->media_info
				)) {
				continue;
			}

			// prefer to remove a track that doesn't have a label, so that we won't lose a language
			// in case of multi language manifest
			if (track1->media_info.tags.label.len == 0 || track2->media_info.tags.label.len != 0) {
				remove = track1;
			} else {
				remove = track2;
			}
			// remove the track from all clips
			media_set->track_count[remove->media_info.media_type]--;

			for (clip_index = 0; clip_index < media_set->clip_count; clip_index++) {
				remove[clip_index * media_set->total_track_count].media_info.media_type =
					MEDIA_TYPE_NONE;
			}

			if (remove == track1) {
				break;
			}
		}
	}
}

static uint64_t
dash_packager_get_segment_time(segment_durations_t* segment_durations, uint32_t skip_count) {
	segment_duration_item_t* cur_item = segment_durations->items + segment_durations->item_count - 1;

	for (;;) {
		if (cur_item->repeat_count >= skip_count) {
			return cur_item->time + (cur_item->repeat_count - skip_count) * cur_item->duration;
		}

		if (cur_item->discontinuity || cur_item <= segment_durations->items) {
			break;
		}

		skip_count -= cur_item->repeat_count;
		cur_item--;
	}

	return cur_item->time;
}

static uint32_t
dash_packager_get_presentation_delay(uint64_t current_time, segment_durations_t* segment_durations) {
	uint64_t reference_time;

	if (segment_durations->item_count <= 0) {
		return 0;
	}

	reference_time = dash_packager_get_segment_time(segment_durations, 3);

	if (current_time > reference_time) {
		return current_time - reference_time;
	}

	return 0;
}

vod_status_t
dash_packager_build_mpd(
	request_context_t* request_context,
	dash_manifest_config_t* conf,
	vod_str_t* base_url,
	media_set_t* media_set,
	dash_manifest_extensions_t* extensions,
	vod_str_t* result
) {
	segment_duration_item_t** cur_duration_items;
	write_period_context_t context;
	adaptation_set_t* adaptation_set;
	segmenter_conf_t* segmenter_conf = media_set->segmenter_conf;
	media_track_t* cur_track;
	vod_tm_t publish_time_gmt;
	vod_tm_t avail_time_gmt;
	vod_tm_t cur_time_gmt;
	time_t current_time;
	size_t base_url_temp_buffer_size = 0;
	size_t base_period_size;
	size_t result_size = 0;
	size_t urls_length;
	uint32_t filtered_clip_offset;
	uint32_t presentation_delay;
	uint32_t min_update_period;
	uint32_t adaptation_count;
	uint32_t max_media_type;
	uint32_t period_count = media_set->use_discontinuity ? media_set->timing.total_count : 1;
	uint32_t window_size;
	uint32_t media_type;
	uint32_t clip_index;
	uint32_t role_index;
	vod_str_t* role;
	vod_status_t rc;
	u_char* p = NULL;

	// remove redundant tracks
	dash_packager_remove_redundant_tracks(conf->duplicate_bitrate_threshold, media_set);

	// get the adaptation sets
	rc = manifest_utils_get_adaptation_sets(
		request_context, media_set, ADAPTATION_SETS_FLAG_MULTI_CODEC, &context.adaptation_sets
	);
	if (rc != VOD_OK) {
		return rc;
	}

	// get segment durations and count for each media type
	for (media_type = 0; media_type < MEDIA_TYPE_COUNT; media_type++) {
		if (media_set->track_count[media_type] == 0) {
			continue;
		}

		rc = segmenter_conf->get_segment_durations(
			request_context, segmenter_conf, media_set, NULL, media_type, &context.segment_durations[media_type]
		);
		if (rc != VOD_OK) {
			return rc;
		}
	}

	// get the base url
	if (base_url->len != 0) {
		result_size += (sizeof(mpd_baseurl) - 1) + base_url->len;
		context.base_url.data = NULL;
		context.base_url.len = 0;
	} else {
		context.base_url.data = NULL;
		context.base_url.len = 0;
	}

	// calculate the total size
	urls_length = 2 * context.base_url.len
	            + 2 * MAX_FILE_EXT_SIZE
	            + conf->init_file_name_prefix.len
	            + MAX_CLIP_SPEC_LENGTH
	            + conf->fragment_file_name_prefix.len;

	base_period_size =
		((sizeof(mpd_period_header_start_duration) - 1) + 5 * VOD_INT32_LEN)
		// video adaptations
		+ (((sizeof(mpd_adaptation_header_video) - 1) + 3 * VOD_INT32_LEN + VOD_DASH_MAX_FRAME_RATE_LEN)
	       + (sizeof(mpd_label) - 1)
	       + (sizeof(mpd_adaptation_footer) - 1))
			  * context.adaptation_sets.count[ADAPTATION_TYPE_VIDEO]
		// video representations
		+ (((sizeof(mpd_representation_header_video) - 1)
	        + MAX_TRACK_SPEC_LENGTH
	        + MAX_MIME_TYPE_SIZE
	        + MAX_CODEC_NAME_SIZE
	        + 3 * VOD_INT32_LEN
	        + VOD_DASH_MAX_FRAME_RATE_LEN)
	       + (sizeof(mpd_representation_footer) - 1))
			  * media_set->track_count[MEDIA_TYPE_VIDEO]
		// audio adaptations
		+ (((sizeof(mpd_adaptation_header_audio) - 1) + VOD_INT32_LEN)
	       + ((sizeof(mpd_audio_channel_config_eac3) - 1) + VOD_INT32_LEN)
	       + (sizeof(mpd_label) - 1)
	       + (sizeof(mpd_adaptation_footer) - 1))
			  * context.adaptation_sets.count[ADAPTATION_TYPE_AUDIO]
		// audio representations
		+ (((sizeof(mpd_representation_header_audio) - 1)
	        + MAX_TRACK_SPEC_LENGTH
	        + MAX_MIME_TYPE_SIZE
	        + MAX_CODEC_NAME_SIZE
	        + 2 * VOD_INT32_LEN)
	       + (sizeof(mpd_representation_footer) - 1))
			  * media_set->track_count[MEDIA_TYPE_AUDIO]
		+ (sizeof(mpd_period_footer) - 1)
		+ extensions->representation.size
		+ extensions->adaptation_set.size;

	switch (conf->subtitle_format) {
	case SUBTITLE_FORMAT_WEBVTT:
		base_period_size +=
			// subtitle adaptations
			(((sizeof(mpd_adaptation_header_subtitle_vtt) - 1) + VOD_INT32_LEN)
		     + (sizeof(mpd_label) - 1)
		     // subtitle representation
		     + ((sizeof(mpd_representation_subtitle_vtt) - 1)
		        + VOD_INT32_LEN
		        + context.base_url.len
		        + conf->subtitle_file_name_prefix.len
		        + MAX_CLIP_SPEC_LENGTH
		        + MAX_TRACK_SPEC_LENGTH)
		     + (sizeof(mpd_adaptation_footer) - 1))
			* context.adaptation_sets.count[ADAPTATION_TYPE_SUBTITLE];
		break;

	default: // SUBTITLE_FORMAT_SMPTE_TT
		base_period_size +=
			// subtitle adaptations
			(((sizeof(mpd_adaptation_header_subtitle_smpte_tt) - 1) + VOD_INT32_LEN)
		     + (sizeof(mpd_label) - 1)
		     + (sizeof(mpd_adaptation_footer) - 1))
				* context.adaptation_sets.count[ADAPTATION_TYPE_SUBTITLE]
			// subtitle representations
			+ (((sizeof(mpd_representation_header_subtitle_smpte_tt) - 1) + MAX_TRACK_SPEC_LENGTH)
		       + (sizeof(mpd_representation_footer) - 1))
				  * media_set->track_count[MEDIA_TYPE_SUBTITLE];
		break;
	}

	switch (media_set->type) {
	case MEDIA_SET_VOD:
		result_size += (sizeof(mpd_header_vod) - 1) + 3 * VOD_INT32_LEN + conf->profiles.len;
		break;

	case MEDIA_SET_LIVE:
		result_size +=
			(sizeof(mpd_header_live) - 1) + 8 * VOD_INT32_LEN + 18 * VOD_INT64_LEN + conf->profiles.len;
		break;
	}

	result_size += base_period_size * period_count + sizeof(mpd_footer);

	for (clip_index = 0; clip_index < period_count; clip_index++) {
		filtered_clip_offset =
			clip_index < media_set->clip_count ? clip_index * media_set->total_track_count : 0;
		for (
			adaptation_set = context.adaptation_sets.first;
			adaptation_set < context.adaptation_sets.last;
			adaptation_set++
		) {
			cur_track = (*adaptation_set->first) + filtered_clip_offset;
			result_size +=
				cur_track->media_info.tags.lang_str.len + cur_track->media_info.tags.label.len;

			for (role_index = 0; role_index < cur_track->media_info.tags.roles.nelts; role_index++) {
				role = (vod_str_t*)cur_track->media_info.tags.roles.elts + role_index;
				result_size += (sizeof(mpd_role) - 1) + role->len;
			}
		}
	}

	switch (conf->manifest_format) {
	case FORMAT_SEGMENT_TEMPLATE:

		switch (conf->subtitle_format) {
		case SUBTITLE_FORMAT_WEBVTT:
			adaptation_count = context.adaptation_sets.count[MEDIA_TYPE_VIDEO]
			                 + context.adaptation_sets.count[MEDIA_TYPE_AUDIO];
			break;

		default: // SUBTITLE_FORMAT_SMPTE_TT
			adaptation_count = context.adaptation_sets.total_count;
			break;
		}

		result_size += ((sizeof(mpd_segment_template_fixed) - 1)
		                + VOD_INT32_LEN
		                + VOD_INT64_LEN
		                + MAX_INDEX_SHIFT_LENGTH
		                + urls_length)
		             * adaptation_count
		             * period_count;
		break;

	case FORMAT_SEGMENT_TIMELINE:

		switch (conf->subtitle_format) {
		case SUBTITLE_FORMAT_WEBVTT:
			max_media_type = MEDIA_TYPE_SUBTITLE;
			break;

		default: // SUBTITLE_FORMAT_SMPTE_TT
			max_media_type = MEDIA_TYPE_COUNT;
			break;
		}

		for (media_type = 0; media_type < max_media_type; media_type++) {
			if (context.adaptation_sets.count[media_type] == 0) {
				continue;
			}

			result_size += ((((sizeof(mpd_segment_template_header) - 1) + VOD_INT32_LEN + urls_length)
			                 + (sizeof(mpd_segment_template_footer) - 1)
			                 + ((sizeof(mpd_segment_repeat_time) - 1) + VOD_INT64_LEN))
			                    * period_count
			                + ((sizeof(mpd_segment_repeat) - 1) + 2 * VOD_INT32_LEN)
			                      * context.segment_durations[media_type].item_count)
			             * context.adaptation_sets.count[media_type];
		}
		break;

	case FORMAT_SEGMENT_LIST:
		result_size += dash_packager_get_segment_list_total_size(
			conf, media_set, context.segment_durations, &context.base_url, &base_url_temp_buffer_size
		);
		break;
	}

	// allocate the buffer
	result->data = vod_alloc(request_context->pool, result_size);
	if (result->data == NULL) {
		vod_log_debug0(
			VOD_LOG_DEBUG_LEVEL, request_context->log, 0, "dash_packager_build_mpd: vod_alloc failed (1)"
		);
		return VOD_ALLOC_FAILED;
	}

	context.base_url_temp_buffer = vod_alloc(
		request_context->pool,
		base_url_temp_buffer_size + sizeof(context.cur_duration_items[0]) * context.adaptation_sets.total_count
	);
	if (context.base_url_temp_buffer == NULL) {
		vod_log_debug0(
			VOD_LOG_DEBUG_LEVEL, request_context->log, 0, "dash_packager_build_mpd: vod_alloc failed (2)"
		);
		return VOD_ALLOC_FAILED;
	}

	// initialize the duration items pointers to the beginning (according to the media type)
	context.cur_duration_items = (void*)(context.base_url_temp_buffer + base_url_temp_buffer_size);

	for (
		adaptation_set = context.adaptation_sets.first, cur_duration_items = context.cur_duration_items;
		adaptation_set < context.adaptation_sets.last;
		adaptation_set++, cur_duration_items++
	) {
		*cur_duration_items = context.segment_durations[adaptation_set->type].items;
	}

	// initialize the context
	if (media_set->timing.segment_base_time != SEGMENT_BASE_TIME_RELATIVE) {
		context.segment_base_time = media_set->timing.segment_base_time;
	} else {
		context.segment_base_time = 0;
	}

	if (media_set->use_discontinuity) {
		context.clip_start_time = media_set->timing.original_first_time;
	} else {
		context.clip_start_time = context.segment_base_time;
	}

	context.clip_index = 0;
	context.conf = conf;
	context.media_set = media_set;
	context.extensions = *extensions;

	// print the manifest header
	switch (media_set->type) {
	case MEDIA_SET_VOD:
		p = vod_sprintf(
			result->data,
			mpd_header_vod,
			(uint32_t)(media_set->timing.total_duration / 1000),
			(uint32_t)(media_set->timing.total_duration % 1000),
			(uint32_t)(segmenter_conf->max_segment_duration / 1000),
			&conf->profiles
		);
		break;

	case MEDIA_SET_LIVE:
		media_type =
			media_set->track_count[MEDIA_TYPE_VIDEO] != 0 ? MEDIA_TYPE_VIDEO : MEDIA_TYPE_AUDIO;

		window_size = context.segment_durations[media_type].duration;
		min_update_period = segmenter_conf->segment_duration / 2;

		vod_gmtime(context.segment_base_time / 1000, &avail_time_gmt);

		vod_gmtime(context.segment_durations[media_type].end_time / 1000, &publish_time_gmt);

		current_time = vod_time(request_context);
		vod_gmtime(current_time, &cur_time_gmt);

		presentation_delay = dash_packager_get_presentation_delay(
			(uint64_t)current_time * 1000, &context.segment_durations[media_type]
		);

		p = vod_sprintf(
			result->data,
			mpd_header_live,
			(uint32_t)(min_update_period / 1000),
			(uint32_t)(min_update_period % 1000),
			avail_time_gmt.vod_tm_year,
			avail_time_gmt.vod_tm_mon,
			avail_time_gmt.vod_tm_mday,
			avail_time_gmt.vod_tm_hour,
			avail_time_gmt.vod_tm_min,
			avail_time_gmt.vod_tm_sec,
			publish_time_gmt.vod_tm_year,
			publish_time_gmt.vod_tm_mon,
			publish_time_gmt.vod_tm_mday,
			publish_time_gmt.vod_tm_hour,
			publish_time_gmt.vod_tm_min,
			publish_time_gmt.vod_tm_sec,
			(uint32_t)(window_size / 1000),
			(uint32_t)(window_size % 1000),
			(uint32_t)(segmenter_conf->max_segment_duration / 1000),
			(uint32_t)(segmenter_conf->max_segment_duration % 1000),
			(uint32_t)(presentation_delay / 1000),
			(uint32_t)(presentation_delay % 1000),
			&conf->profiles,
			cur_time_gmt.vod_tm_year,
			cur_time_gmt.vod_tm_mon,
			cur_time_gmt.vod_tm_mday,
			cur_time_gmt.vod_tm_hour,
			cur_time_gmt.vod_tm_min,
			cur_time_gmt.vod_tm_sec
		);
		break;
	}

	if (base_url->len != 0) {
		p = vod_sprintf(p, mpd_baseurl, base_url);
	}

	for (;;) {
		p = dash_packager_write_mpd_period(p, &context);

		context.clip_index++;
		if (context.clip_index >= period_count) {
			break;
		}

		context.clip_start_time = media_set->timing.times[context.clip_index];
	}

	p = vod_copy(p, mpd_footer, sizeof(mpd_footer) - 1);

	result->len = p - result->data;

	if (result->len > result_size) {
		vod_log_error(
			VOD_LOG_ERR,
			request_context->log,
			0,
			"dash_packager_build_mpd: result length %uz exceeded allocated length %uz",
			result->len,
			result_size
		);
		return VOD_UNEXPECTED;
	}

	return VOD_OK;
}

// fragment writing code

static uint64_t
dash_packager_get_earliest_pres_time(media_set_t* media_set, media_track_t* track) {
	uint64_t result;
	uint64_t clip_start_time;

	if (media_set->use_discontinuity) {
		clip_start_time = media_set->timing.original_first_time;
	} else {
		clip_start_time = media_set->timing.segment_base_time;
		if (clip_start_time == SEGMENT_BASE_TIME_RELATIVE) {
			clip_start_time = 0;
		}
	}

	result = dash_rescale_millis(track->clip_start_time - clip_start_time)
	       + track->first_frame_time_offset;

	if (track->frame_count > 0) {
		result += track->frames.first_frame[0].pts_delay;

#ifndef DISABLE_PTS_DELAY_COMPENSATION
		if (track->media_info.media_type == MEDIA_TYPE_VIDEO) {
			result -= track->media_info.u.video.initial_pts_delay;
		}
#endif
	}

	return result;
}

static u_char*
dash_packager_write_sidx_atom(u_char* p, sidx_params_t* sidx_params, uint32_t reference_size) {
	size_t atom_size = ATOM_HEADER_SIZE + sizeof(sidx_atom_t);

	write_atom_header(p, atom_size, 's', 'i', 'd', 'x');
	write_fullbox_header(p, 0, 0);
	write_be32(p, 1);                                  // reference_ID
	write_be32(p, sidx_params->timescale);             // timescale
	write_be32(p, sidx_params->earliest_pres_time);    // earliest_presentation_time
	write_be32(p, 0);                                  // first_offset
	write_be16(p, 0);                                  // reserved
	write_be16(p, 1);                                  // reference_count
	write_be32(p, reference_size);                     // reference_type(1), referenced_size(31)
	write_be32(p, sidx_params->total_frames_duration); // subsegment_duration
	write_be32(p, 0x90000000); // starts_with_SAP(1) = 1, SAP_type(3) = 1, SAP_delta_time(28) = 0
	return p;
}

static u_char*
dash_packager_write_sidx64_atom(u_char* p, sidx_params_t* sidx_params, uint32_t reference_size) {
	size_t atom_size = ATOM_HEADER_SIZE + sizeof(sidx64_atom_t);

	write_atom_header(p, atom_size, 's', 'i', 'd', 'x');
	write_fullbox_header(p, 1, 0);
	write_be32(p, 1);                                  // reference_ID
	write_be32(p, sidx_params->timescale);             // timescale
	write_be64(p, sidx_params->earliest_pres_time);    // earliest_presentation_time
	write_be64(p, 0LL);                                // first_offset
	write_be16(p, 0);                                  // reserved
	write_be32(p, 1);                                  // reference_count
	write_be32(p, reference_size);                     // reference_type(1), referenced_size(31)
	write_be32(p, sidx_params->total_frames_duration); // subsegment_duration
	write_be32(p, 0x90000000); // starts_with_SAP(1) = 1, SAP_type(3) = 1, SAP_delta_time(28) = 0
	return p;
}

static void
dash_packager_init_sidx_params(media_set_t* media_set, media_sequence_t* sequence, sidx_params_t* result) {
	media_clip_filtered_t* cur_clip;
	media_track_t* track;
	uint64_t earliest_pres_time;
	uint64_t total_frames_duration;
	bool_t frame_found = FALSE;

	if (sequence->media_type == MEDIA_TYPE_SUBTITLE) {
		result->timescale = DASH_TIMESCALE;
		result->earliest_pres_time = rescale_time(media_set->segment_start_time, 1000, DASH_TIMESCALE);
		result->total_frames_duration =
			rescale_time(media_set->segment_duration, 1000, DASH_TIMESCALE);
		return;
	}

	// initialize according to the first clip
	cur_clip = sequence->filtered_clips;
	track = cur_clip->first_track;
	total_frames_duration = track->total_frames_duration;
	earliest_pres_time = dash_packager_get_earliest_pres_time(media_set, track);
	frame_found = track->frame_count > 0;
	cur_clip++;

	for (; cur_clip < sequence->filtered_clips_end; cur_clip++) {
		track = cur_clip->first_track;
		total_frames_duration += track->total_frames_duration;
		if (!frame_found && track->frame_count > 0) {
			earliest_pres_time = dash_packager_get_earliest_pres_time(media_set, track);
			frame_found = TRUE;
		}
	}

	result->total_frames_duration = total_frames_duration;
	result->earliest_pres_time = earliest_pres_time;
	result->timescale = DASH_TIMESCALE;
}

vod_status_t
dash_packager_build_fragment_header(
	request_context_t* request_context,
	media_set_t* media_set,
	uint32_t segment_index,
	uint32_t sample_description_index,
	dash_fragment_header_extensions_t* extensions,
	bool_t size_only,
	vod_str_t* result,
	size_t* total_fragment_size
) {
	media_sequence_t* sequence = &media_set->sequences[0];
	media_track_t* first_track = sequence->filtered_clips[0].first_track;
	sidx_params_t sidx_params;
	uint32_t duration;
	size_t first_frame_offset;
	size_t mdat_atom_size;
	size_t trun_atom_size;
	size_t tfhd_atom_size;
	size_t moof_atom_size;
	size_t traf_atom_size;
	size_t result_size;
	u_char* mdat_start;
	u_char* p;
	u_char* sample_size;

	// calculate sizes
	dash_packager_init_sidx_params(media_set, sequence, &sidx_params);

	mdat_atom_size = ATOM_HEADER_SIZE + sequence->total_frame_size;
	trun_atom_size =
		mp4_fragment_get_trun_atom_size(first_track->media_info.media_type, sequence->total_frame_count);

	tfhd_atom_size = ATOM_HEADER_SIZE + sizeof(tfhd_atom_t);
	if (sample_description_index > 0) {
		tfhd_atom_size += sizeof(uint32_t);
	}

	traf_atom_size =
		ATOM_HEADER_SIZE
		+ tfhd_atom_size
		+ ATOM_HEADER_SIZE
		+ (sidx_params.earliest_pres_time > UINT_MAX ? sizeof(tfdt64_atom_t) : sizeof(tfdt_atom_t))
		+ trun_atom_size
		+ extensions->extra_traf_atoms_size;

	moof_atom_size = ATOM_HEADER_SIZE + ATOM_HEADER_SIZE + sizeof(mfhd_atom_t) + traf_atom_size;

	*total_fragment_size =
		sizeof(styp_atom)
		+ ATOM_HEADER_SIZE
		+ (sidx_params.earliest_pres_time > UINT_MAX ? sizeof(sidx64_atom_t) : sizeof(sidx_atom_t))
		+ moof_atom_size
		+ mdat_atom_size;

	result_size = *total_fragment_size - sequence->total_frame_size + extensions->mdat_atom_max_size;

	// head request optimization
	if (size_only) {
		return VOD_OK;
	}

	// allocate the buffer
	p = vod_alloc(request_context->pool, result_size);
	if (p == NULL) {
		vod_log_debug0(
			VOD_LOG_DEBUG_LEVEL, request_context->log, 0, "dash_packager_build_fragment_header: vod_alloc failed"
		);
		return VOD_ALLOC_FAILED;
	}

	result->data = p;

	// styp
	p = vod_copy(p, styp_atom, sizeof(styp_atom));

	// sidx
	if (sidx_params.earliest_pres_time > UINT_MAX) {
		p = dash_packager_write_sidx64_atom(p, &sidx_params, moof_atom_size + mdat_atom_size);
	} else {
		p = dash_packager_write_sidx_atom(p, &sidx_params, moof_atom_size + mdat_atom_size);
	}

	// moof
	write_atom_header(p, moof_atom_size, 'm', 'o', 'o', 'f');

	// moof.mfhd
	p = mp4_fragment_write_mfhd_atom(p, segment_index + 1);

	// moof.traf
	write_atom_header(p, traf_atom_size, 't', 'r', 'a', 'f');

	// moof.traf.tfhd
	p = mp4_fragment_write_tfhd_atom(p, 1, sample_description_index);

	// moof.traf.tfdt
	if (sidx_params.earliest_pres_time > UINT_MAX) {
		p = mp4_fragment_write_tfdt64_atom(p, sidx_params.earliest_pres_time);
	} else {
		p = mp4_fragment_write_tfdt_atom(p, (uint32_t)sidx_params.earliest_pres_time);
	}

	// moof.traf.trun
	first_frame_offset = moof_atom_size + ATOM_HEADER_SIZE;

	sample_size = NULL;

	switch (sequence->media_type) {
	case MEDIA_TYPE_VIDEO:
		p = mp4_fragment_write_video_trun_atom(p, sequence, first_frame_offset);
		break;

	case MEDIA_TYPE_AUDIO:
		p = mp4_fragment_write_audio_trun_atom(p, sequence, first_frame_offset);
		break;

	case MEDIA_TYPE_SUBTITLE:
		duration = rescale_time(media_set->segment_duration, 1000, DASH_TIMESCALE);
		p = mp4_fragment_write_subtitle_trun_atom(p, first_frame_offset, duration, &sample_size);
		break;
	}

	// moof.traf.xxx
	if (extensions->write_extra_traf_atoms_callback != NULL) {
		p = extensions->write_extra_traf_atoms_callback(
			extensions->write_extra_traf_atoms_context, p, moof_atom_size
		);
	}

	// mdat
	mdat_start = p;
	write_atom_header(p, mdat_atom_size, 'm', 'd', 'a', 't');

	if (extensions->write_mdat_atom_callback != NULL) {
		p = extensions->write_mdat_atom_callback(extensions->write_mdat_atom_context, p);
		mdat_atom_size = p - mdat_start;
		write_be32(mdat_start, mdat_atom_size);

		if (sample_size != NULL) {
			write_be32(sample_size, mdat_atom_size - ATOM_HEADER_SIZE); // HACK: write subtitle size
		}
	}

	result->len = p - result->data;

	if (result->len > result_size) {
		vod_log_error(
			VOD_LOG_ERR,
			request_context->log,
			0,
			"dash_packager_build_fragment_header: result length %uz exceeded allocated length %uz",
			result->len,
			result_size
		);
		return VOD_UNEXPECTED;
	}

	return VOD_OK;
}
