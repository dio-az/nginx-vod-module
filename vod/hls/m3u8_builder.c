#include "m3u8_builder.h"
#include "../manifest_utils.h"
#include "../mp4/mp4_defs.h"

#if (NGX_HAVE_OPENSSL_EVP)
#include "../mp4/mp4_pssh.h"
#include "../mp4/mp4_cenc_encrypt.h"
#endif // NGX_HAVE_OPENSSL_EVP

// constants
static const char m3u8_header_base[] = "#EXTM3U\n#EXT-X-VERSION:%uD\n";
static const char m3u8_header_index[] = "#EXT-X-TARGETDURATION:%uL\n#EXT-X-MEDIA-SEQUENCE:%uD\n";
static const u_char m3u8_playlist_vod[] = "#EXT-X-PLAYLIST-TYPE:VOD\n";
static const u_char m3u8_playlist_event[] = "#EXT-X-PLAYLIST-TYPE:EVENT\n";
static const u_char m3u8_endlist[] = "#EXT-X-ENDLIST\n";
static const u_char m3u8_independent_segments[] = "#EXT-X-INDEPENDENT-SEGMENTS\n";
static const u_char m3u8_discontinuity[] = "#EXT-X-DISCONTINUITY\n";
static const char m3u8_byterange[] = "#EXT-X-BYTERANGE:%uD@%uD\n";
static const u_char m3u8_url_suffix[] = ".m3u8";
static const u_char m3u8_map_prefix[] = "#EXT-X-MAP:URI=\"";
static const u_char m3u8_map_suffix[] = ".mp4\"\n";
static const char m3u8_clip_index[] = "-c%uD";

static const u_char m3u8_group_id_audio[] = "audio";
static const u_char m3u8_group_id_subtitles[] = "subs";
static const u_char m3u8_group_id_closed_captions[] = "cc";

static const char m3u8_media_base[] = "#EXT-X-MEDIA:TYPE=%s,GROUP-ID=\"%s%uD\",NAME=\"%V\"";
static const u_char m3u8_media_type_audio[] = "AUDIO";
static const u_char m3u8_media_type_subtitles[] = "SUBTITLES";
static const u_char m3u8_media_type_closed_captions[] = "CLOSED-CAPTIONS";
static const char m3u8_media_channels[] = ",CHANNELS=\"%uD\"";
static const char m3u8_media_language[] = ",LANGUAGE=\"%V\"";
static const u_char m3u8_media_default[] = ",DEFAULT=YES";
static const u_char m3u8_media_non_default[] = ",DEFAULT=NO";
static const u_char m3u8_media_autoselect[] = ",AUTOSELECT=YES";
static const u_char m3u8_media_forced[] = ",FORCED=YES";
static const char m3u8_media_characteristics[] = ",CHARACTERISTICS=\"%V\"";
static const char m3u8_media_instream_id[] = ",INSTREAM-ID=\"%V\"";
static const u_char m3u8_media_uri[] = ",URI=\"";

static const char m3u8_stream_inf_video[] =
	"#EXT-X-STREAM-INF:BANDWIDTH=%uD,RESOLUTION=%uDx%uD,FRAME-RATE=%uD.%03uD,CODECS=\"%V";
static const char m3u8_stream_inf_audio[] = "#EXT-X-STREAM-INF:BANDWIDTH=%uD,CODECS=\"%V";
static const char m3u8_stream_inf_average_bandwidth[] = ",AVERAGE-BANDWIDTH=%uD";
static const char m3u8_stream_inf_audio_group[] = ",AUDIO=\"audio%uD\"";
static const char m3u8_stream_inf_subtitles_group[] = ",SUBTITLES=\"subs%uD\"";
static const char m3u8_stream_inf_closed_captions_group[] = ",CLOSED-CAPTIONS=\"cc%uD\"";
static const u_char m3u8_stream_inf_no_closed_captions[] = ",CLOSED-CAPTIONS=NONE";
static const u_char m3u8_stream_inf_video_range_sdr[] = ",VIDEO-RANGE=SDR";
static const u_char m3u8_stream_inf_video_range_hlg[] = ",VIDEO-RANGE=HLG";
static const u_char m3u8_stream_inf_video_range_pq[] = ",VIDEO-RANGE=PQ";

static const char m3u8_iframe_stream_inf[] =
	"#EXT-X-I-FRAME-STREAM-INF:BANDWIDTH=%uD,RESOLUTION=%uDx%uD,CODECS=\"%V\",URI=\"";

#if (NGX_HAVE_OPENSSL_EVP)
static const char m3u8_key[] = "#EXT-X-KEY:METHOD=%s";
static const char m3u8_session_key[] = "#EXT-X-SESSION-KEY:METHOD=%s";

static const u_char m3u8_key_sample_aes[] = "SAMPLE-AES";
static const u_char m3u8_key_sample_aes_ctr[] = "SAMPLE-AES-CTR";

static const u_char m3u8_key_uri[] = ",URI=\"";
static const u_char m3u8_key_iv[] = ",IV=0x";
static const char m3u8_key_keyformat[] = ",KEYFORMAT=\"";
static const char m3u8_key_keyformatversions[] = ",KEYFORMATVERSIONS=\"%V\"";

static const u_char m3u8_keyformat_playready[] = "com.microsoft.playready";
static const u_char m3u8_keyformat_uuid_prefix[] = "urn:uuid:";

static const u_char m3u8_key_uri_pssh_prefix[] = "data:text/plain;base64,";
static const u_char m3u8_key_uri_playready_prefix[] = "data:text/plain;charset=UTF-16;base64,";

static vod_str_t keyformatversions_1 = vod_string("1");
#endif // NGX_HAVE_OPENSSL_EVP

static vod_str_t m3u8_ts_suffix = vod_string(".ts\n");
static vod_str_t m3u8_m4s_suffix = vod_string(".m4s\n");
static vod_str_t m3u8_vtt_suffix = vod_string(".vtt\n");

static vod_str_t default_label = vod_string("default");

// typedefs
typedef struct {
	u_char* p;
	vod_str_t name_suffix;
	vod_str_t* base_url;
	vod_str_t* segment_file_name_prefix;
} write_segment_context_t;

// NOTE:
// 1. not using vod_sprintf in order to avoid the use of floats
// 2. scale must be a power of 10

static u_char*
m3u8_builder_format_double(u_char* p, uint32_t n, uint32_t scale) {
	int cur_digit;
	int int_n = n / scale;
	int fraction = n % scale;

	p = vod_sprintf(p, "%d", int_n);

	if (scale == 1) {
		return p;
	}

	*p++ = '.';
	for (;;) {
		scale /= 10;
		if (scale == 0) {
			break;
		}
		cur_digit = fraction / scale;
		*p++ = cur_digit + '0';
		fraction -= cur_digit * scale;
	}
	return p;
}

static u_char*
m3u8_builder_append_segment_name(
	u_char* p, vod_str_t* base_url, vod_str_t* segment_file_name_prefix, uint32_t segment_index, vod_str_t* suffix
) {
	p = vod_copy(p, base_url->data, base_url->len);
	p = vod_copy(p, segment_file_name_prefix->data, segment_file_name_prefix->len);
	*p++ = '-';
	p = vod_sprintf(p, "%uD", segment_index + 1);
	p = vod_copy(p, suffix->data, suffix->len);
	return p;
}

static u_char*
m3u8_builder_append_extinf_tag(u_char* p, uint32_t duration, uint32_t scale) {
	p = vod_copy(p, "#EXTINF:", sizeof("#EXTINF:") - 1);
	p = m3u8_builder_format_double(p, duration, scale);
	*p++ = ',';
	*p++ = '\n';
	return p;
}

static void
m3u8_builder_append_iframe_string(
	void* context, uint32_t segment_index, uint32_t frame_duration, uint32_t frame_start, uint32_t frame_size
) {
	write_segment_context_t* ctx = (write_segment_context_t*)context;

	ctx->p = m3u8_builder_append_extinf_tag(ctx->p, frame_duration, 1000);
	ctx->p = vod_sprintf(ctx->p, m3u8_byterange, frame_size, frame_start);
	ctx->p = m3u8_builder_append_segment_name(
		ctx->p, ctx->base_url, ctx->segment_file_name_prefix, segment_index, &ctx->name_suffix
	);
}

static vod_status_t
m3u8_builder_build_track_spec(
	request_context_t* request_context, media_set_t* media_set, vod_str_t* suffix, vod_str_t* result
) {
	media_track_t* track = media_set->filtered_tracks;
	u_char* p;
	size_t result_size =
		suffix->len + ((sizeof("-x") - 1) + VOD_INT32_LEN) + MANIFEST_UTILS_TRACK_SPEC_MAX_SIZE;

	if (media_set->has_multi_sequences) {
		result_size += (sizeof("-f") - 1) + VOD_INT32_LEN;
	}

	// allocate the result buffer
	p = vod_alloc(request_context->pool, result_size);
	if (p == NULL) {
		vod_log_debug0(
			VOD_LOG_DEBUG_LEVEL, request_context->log, 0, "m3u8_builder_build_tracks_spec: vod_alloc failed"
		);
		return VOD_ALLOC_FAILED;
	}

	// write the result
	result->data = p;

	*p++ = '-'; // NOTE: assume has_multi_sequences is always `TRUE` for subtitles.
	p = manifest_utils_append_track_spec(
		p, track->file_info.source->sequence, track, media_set->has_multi_sequences
	);

	p = vod_copy(p, suffix->data, suffix->len);

	result->len = p - result->data;

	if (result->len > result_size) {
		vod_log_error(
			VOD_LOG_ERR,
			request_context->log,
			0,
			"m3u8_builder_build_tracks_spec: result length %uz exceeded allocated length %uz",
			result->len,
			result_size
		);
		return VOD_UNEXPECTED;
	}

	return VOD_OK;
}

vod_status_t
m3u8_builder_build_iframe_playlist(
	request_context_t* request_context,
	m3u8_config_t* conf,
	hls_mpegts_muxer_conf_t* muxer_conf,
	vod_str_t* base_url,
	media_set_t* media_set,
	vod_str_t* result
) {
	hls_encryption_params_t encryption_params;
	write_segment_context_t ctx;
	segment_durations_t segment_durations;
	segmenter_conf_t* segmenter_conf = media_set->segmenter_conf;
	size_t iframe_length;
	size_t result_size;
	uint64_t duration_millis;
	vod_status_t rc;

	// iframes list is not supported with encryption, since:
	// 1. AES-128 - the IV of each key frame is not known in advance
	// 2. SAMPLE-AES - the layout of the TS files is not known in advance due to emulation prevention
	encryption_params.type = HLS_ENC_NONE;
	encryption_params.key = NULL;
	encryption_params.iv = NULL;

	// build the required tracks string
	rc = m3u8_builder_build_track_spec(request_context, media_set, &m3u8_ts_suffix, &ctx.name_suffix);
	if (rc != VOD_OK) {
		return rc;
	}

	// get segment durations
	if (segmenter_conf->align_to_key_frames) {
		rc = segmenter_get_segment_durations_accurate(
			request_context, segmenter_conf, media_set, NULL, MEDIA_TYPE_NONE, &segment_durations
		);
	} else {
		rc = segmenter_get_segment_durations_estimate(
			request_context, segmenter_conf, media_set, NULL, MEDIA_TYPE_NONE, &segment_durations
		);
	}

	if (rc != VOD_OK) {
		return rc;
	}

	duration_millis = segment_durations.duration;
	iframe_length =
		((sizeof("#EXTINF:.000,\n") - 1) + vod_get_int_print_len(vod_div_ceil(duration_millis, 1000)))
		+ ((sizeof(m3u8_byterange) - 1)
	       + VOD_INT32_LEN
	       + vod_get_int_print_len(MAX_FRAME_SIZE)
	       - (sizeof("%uD%uD") - 1))
		+ base_url->len
		+ conf->segment_file_name_prefix.len
		+ 1 // '-'
		+ vod_get_int_print_len(segment_durations.segment_count)
		+ ctx.name_suffix.len;

	result_size = conf->iframes_m3u8_header_len
	            + iframe_length * media_set->sequences[0].video_key_frame_count
	            + (sizeof(m3u8_endlist) - 1);

	// allocate the buffer
	result->data = vod_alloc(request_context->pool, result_size);
	if (result->data == NULL) {
		vod_log_debug0(
			VOD_LOG_DEBUG_LEVEL, request_context->log, 0, "m3u8_builder_build_iframe_playlist: vod_alloc failed"
		);
		return VOD_ALLOC_FAILED;
	}

	// fill out the buffer
	ctx.p = vod_copy(result->data, conf->iframes_m3u8_header, conf->iframes_m3u8_header_len);

	if (media_set->sequences[0].video_key_frame_count > 0) {
		ctx.base_url = base_url;
		ctx.segment_file_name_prefix = &conf->segment_file_name_prefix;

		rc = hls_muxer_simulate_get_iframes(
			request_context,
			&segment_durations,
			muxer_conf,
			&encryption_params,
			media_set,
			m3u8_builder_append_iframe_string,
			&ctx
		);
		if (rc != VOD_OK) {
			return rc;
		}
	}

	ctx.p = vod_copy(ctx.p, m3u8_endlist, sizeof(m3u8_endlist) - 1);
	result->len = ctx.p - result->data;

	if (result->len > result_size) {
		vod_log_error(
			VOD_LOG_ERR,
			request_context->log,
			0,
			"m3u8_builder_build_iframe_playlist: result length %uz exceeded allocated length %uz",
			result->len,
			result_size
		);
		return VOD_UNEXPECTED;
	}

	return VOD_OK;
}

#if (NGX_HAVE_OPENSSL_EVP)
static size_t
m3u8_builder_get_keys_size(const char* key_tag, drm_info_t* drm_info, size_t* max_pssh_size) {
	drm_system_info_t* cur_info;
	size_t result = 0;
	size_t cur_pssh_size = 0;

	for (cur_info = drm_info->pssh_array.first; cur_info < drm_info->pssh_array.last; cur_info++) {
		result += (sizeof(key_tag) - 1)
		        + (sizeof(m3u8_key_sample_aes_ctr) - 1)
		        + ((sizeof(m3u8_key_uri) - 1) + 1)       // '"'
		        + ((sizeof(m3u8_key_keyformat) - 1) + 1) // '"'
		        + (sizeof(m3u8_key_keyformatversions) - 1)
		        + keyformatversions_1.len
		        + 1; // '\n'

		if (mp4_pssh_is_playready(cur_info)) {
			result += (sizeof(m3u8_keyformat_playready) - 1)
			        + ((sizeof(m3u8_key_uri_playready_prefix) - 1)
			           + vod_base64_encoded_length(cur_info->data.len));
		} else {
			cur_pssh_size = ATOM_HEADER_SIZE + sizeof(pssh_atom_t) + cur_info->data.len;
			if (cur_pssh_size > *max_pssh_size) {
				*max_pssh_size = cur_pssh_size;
			}

			result +=
				((sizeof(m3u8_keyformat_uuid_prefix) - 1) + VOD_GUID_LENGTH)
				+ ((sizeof(m3u8_key_uri_pssh_prefix) - 1) + vod_base64_encoded_length(cur_pssh_size));
		}
	}

	return result;
}

static u_char*
m3u8_builder_write_keys(u_char* p, const char* key_tag, drm_info_t* drm_info, u_char* temp_buffer) {
	drm_system_info_t* cur_info;
	vod_str_t pssh;
	vod_str_t base64;
	bool_t is_playready;

	for (cur_info = drm_info->pssh_array.first; cur_info < drm_info->pssh_array.last; cur_info++) {
		is_playready = mp4_pssh_is_playready(cur_info);

		p = vod_sprintf(p, key_tag, m3u8_key_sample_aes_ctr);

		p = vod_copy(p, m3u8_key_uri, sizeof(m3u8_key_uri) - 1);
		if (is_playready) {
			p = vod_copy(p, m3u8_key_uri_playready_prefix, sizeof(m3u8_key_uri_playready_prefix) - 1);

			base64.data = p;
			vod_encode_base64(&base64, &cur_info->data);
		} else {
			p = vod_copy(p, m3u8_key_uri_pssh_prefix, sizeof(m3u8_key_uri_pssh_prefix) - 1);

			pssh.data = temp_buffer;
			pssh.len = mp4_pssh_write_box(pssh.data, cur_info) - temp_buffer;

			base64.data = p;
			vod_encode_base64(&base64, &pssh);
		}
		p += base64.len;
		*p++ = '"';

		p = vod_copy(p, m3u8_key_keyformat, sizeof(m3u8_key_keyformat) - 1);
		if (is_playready) {
			p = vod_copy(p, m3u8_keyformat_playready, sizeof(m3u8_keyformat_playready) - 1);
		} else {
			p = vod_copy(p, m3u8_keyformat_uuid_prefix, sizeof(m3u8_keyformat_uuid_prefix) - 1);
			p = mp4_cenc_encrypt_write_guid(p, cur_info->system_id);
		}
		*p++ = '"';

		p = vod_sprintf(p, m3u8_key_keyformatversions, &keyformatversions_1);

		*p++ = '\n';
	}

	return p;
}

static size_t
m3u8_builder_get_encryption_size(
	const char* key_tag,
	m3u8_config_t* conf,
	drm_info_t* drm_info,
	hls_encryption_params_t* encryption_params,
	size_t* max_pssh_size
) {
	size_t result = 0;

	if (encryption_params->type == HLS_ENC_SAMPLE_AES_CTR) {
		return m3u8_builder_get_keys_size(key_tag, drm_info, max_pssh_size);
	}

	result +=
		(sizeof(key_tag) - 1) + (sizeof(m3u8_key_sample_aes) - 1) + (sizeof(m3u8_key_uri) - 1) + 2; // '"', '\n'

	if (encryption_params->key_uri.len != 0) {
		result += encryption_params->key_uri.len;
	}

	if (encryption_params->return_iv) {
		result += (sizeof(m3u8_key_iv) - 1) + sizeof(encryption_params->iv_buf) * 2;
	}

	if (conf->encryption_key_format.len != 0) {
		result += (sizeof(m3u8_key_keyformat) - 1) + conf->encryption_key_format.len + 1; // '"'
	}

	if (conf->encryption_key_format_versions.len != 0) {
		result += (sizeof(m3u8_key_keyformatversions) - 1) + conf->encryption_key_format_versions.len;
	}

	return result;
}

static u_char*
m3u8_builder_write_encryption(
	u_char* p,
	const char* key_tag,
	m3u8_config_t* conf,
	media_set_t* media_set,
	hls_encryption_params_t* encryption_params,
	u_char* temp_buffer
) {
	if (encryption_params->type == HLS_ENC_SAMPLE_AES_CTR) {
		return m3u8_builder_write_keys(p, key_tag, media_set->sequences[0].drm_info, temp_buffer);
	}

	p = vod_sprintf(p, key_tag, m3u8_key_sample_aes);

	p = vod_copy(p, m3u8_key_uri, sizeof(m3u8_key_uri) - 1);
	if (encryption_params->key_uri.len != 0) {
		p = vod_copy(p, encryption_params->key_uri.data, encryption_params->key_uri.len);
	}
	*p++ = '"';

	if (encryption_params->return_iv) {
		p = vod_copy(p, m3u8_key_iv, sizeof(m3u8_key_iv) - 1);
		p = vod_append_hex_string(p, encryption_params->iv, sizeof(encryption_params->iv_buf));
	}

	if (conf->encryption_key_format.len != 0) {
		p = vod_copy(p, m3u8_key_keyformat, sizeof(m3u8_key_keyformat) - 1);
		p = vod_copy(p, conf->encryption_key_format.data, conf->encryption_key_format.len);
		*p++ = '"';
	}

	if (conf->encryption_key_format_versions.len != 0) {
		p = vod_sprintf(p, m3u8_key_keyformatversions, &conf->encryption_key_format_versions);
	}

	*p++ = '\n';

	return p;
}
#endif // NGX_HAVE_OPENSSL_EVP

vod_status_t
m3u8_builder_build_index_playlist(
	request_context_t* request_context,
	m3u8_config_t* conf,
	vod_str_t* base_url,
	vod_str_t* segments_base_url,
	hls_encryption_params_t* encryption_params,
	vod_uint_t container_format,
	media_set_t* media_set,
	vod_str_t* result
) {
	segment_durations_t segment_durations;
	segment_duration_item_t* cur_item;
	segment_duration_item_t* last_item;
	segmenter_conf_t* segmenter_conf = media_set->segmenter_conf;
	vod_str_t name_suffix;
	vod_str_t extinf;
	vod_str_t* suffix = &m3u8_ts_suffix;
	uint32_t conf_max_segment_duration;
	uint64_t max_segment_duration;
	uint64_t duration_millis;
	uint32_t segment_index;
	uint32_t last_segment_index;
	uint32_t clip_index = 0;
	uint32_t scale = 1000;
	size_t segment_length;
	size_t result_size;
	vod_status_t rc;
	u_char* p;

#if (NGX_HAVE_OPENSSL_EVP)
	size_t max_pssh_size = 0;
	u_char* temp_buffer = NULL;
#endif // NGX_HAVE_OPENSSL_EVP

	// build the required tracks string
	if (media_set->track_count[MEDIA_TYPE_VIDEO] != 0 || media_set->track_count[MEDIA_TYPE_AUDIO] != 0) {
		if (container_format == HLS_CONTAINER_FMP4) {
			suffix = &m3u8_m4s_suffix;
		}
	} else {
		container_format = HLS_CONTAINER_MPEGTS; // do not output any fmp4-specific tags
		suffix = &m3u8_vtt_suffix;
	}

	rc = m3u8_builder_build_track_spec(request_context, media_set, suffix, &name_suffix);
	if (rc != VOD_OK) {
		return rc;
	}

	// get the segment durations
	rc = segmenter_conf->get_segment_durations(
		request_context, segmenter_conf, media_set, NULL, MEDIA_TYPE_NONE, &segment_durations
	);
	if (rc != VOD_OK) {
		return rc;
	}
	last_item = segment_durations.items + segment_durations.item_count;

	// get the required buffer length
	duration_millis = segment_durations.duration;
	last_segment_index = last_item[-1].segment_index + last_item[-1].repeat_count;
	segment_length =
		(sizeof("#EXTINF:.000,\n") - 1 + vod_get_int_print_len(vod_div_ceil(duration_millis, 1000)))
		+ (segments_base_url->len
	       + conf->segment_file_name_prefix.len
	       + 1 // '-'
	       + vod_get_int_print_len(last_segment_index)
	       + name_suffix.len);

	result_size = ((sizeof(m3u8_header_base) - 1) + VOD_INT32_LEN)
	            + ((sizeof(m3u8_header_index) - 1) + 2 * VOD_INT64_LEN)
	            + (sizeof(m3u8_playlist_event) - 1)
	            + segment_length * segment_durations.segment_count
	            + segment_durations.discontinuities * (sizeof(m3u8_discontinuity) - 1)
	            + ((sizeof(m3u8_map_prefix) - 1)
	               + base_url->len
	               + conf->init_file_name_prefix.len
	               + ((sizeof(m3u8_clip_index) - 1) + VOD_INT32_LEN)
	               + name_suffix.len
	               + (sizeof(m3u8_map_suffix) - 1))
	                  * (segment_durations.discontinuities + 1)
	            + (sizeof(m3u8_endlist) - 1);

	if (media_set->segmenter_conf->align_to_key_frames
	    && (media_set->track_count[MEDIA_TYPE_VIDEO] != 0
	        || media_set->track_count[MEDIA_TYPE_AUDIO] != 0)) {
		result_size += sizeof(m3u8_independent_segments) - 1;
	}

#if (NGX_HAVE_OPENSSL_EVP)
	if (encryption_params->type != HLS_ENC_NONE && suffix != &m3u8_vtt_suffix) {
		result_size += m3u8_builder_get_encryption_size(
			m3u8_key, conf, media_set->sequences[0].drm_info, encryption_params, &max_pssh_size
		);
	}
#endif // NGX_HAVE_OPENSSL_EVP

	// allocate the buffer
	result->data = vod_alloc(request_context->pool, result_size);
	if (result->data == NULL) {
		vod_log_debug0(
			VOD_LOG_DEBUG_LEVEL, request_context->log, 0, "m3u8_builder_build_index_playlist: vod_alloc failed"
		);
		return VOD_ALLOC_FAILED;
	}

	// find the max segment duration
	max_segment_duration = 0;
	for (cur_item = segment_durations.items; cur_item < last_item; cur_item++) {
		if (cur_item->duration > max_segment_duration) {
			max_segment_duration = cur_item->duration;
		}
	}

	// NOTE: scaling first to 'scale' so that target duration will always be round(max(manifest durations))
	max_segment_duration = rescale_time(max_segment_duration, segment_durations.timescale, scale);
	max_segment_duration = rescale_time(max_segment_duration, scale, 1);

	// make sure segment duration is not lower than the value set in the conf
	conf_max_segment_duration = (segmenter_conf->max_segment_duration + 500) / 1000;
	if (conf_max_segment_duration > max_segment_duration) {
		max_segment_duration = conf_max_segment_duration;
	}

	// write the header
	p = vod_sprintf(result->data, m3u8_header_base, conf->m3u8_version);
	p = vod_sprintf(
		p, m3u8_header_index, max_segment_duration, segment_durations.items[0].segment_index + 1
	);

	if (media_set->type == MEDIA_SET_VOD) {
		p = vod_copy(p, m3u8_playlist_vod, sizeof(m3u8_playlist_vod) - 1);
	} else if (media_set->is_live_event) {
		p = vod_copy(p, m3u8_playlist_event, sizeof(m3u8_playlist_event) - 1);
	}

	if (media_set->segmenter_conf->align_to_key_frames
	    && (media_set->track_count[MEDIA_TYPE_VIDEO] != 0
	        || media_set->track_count[MEDIA_TYPE_AUDIO] != 0)) {
		p = vod_copy(p, m3u8_independent_segments, sizeof(m3u8_independent_segments) - 1);
	}

#if (NGX_HAVE_OPENSSL_EVP)
	if (max_pssh_size > 0) {
		temp_buffer = vod_alloc(request_context->pool, max_pssh_size);
		if (temp_buffer == NULL) {
			vod_log_debug0(
				VOD_LOG_DEBUG_LEVEL, request_context->log, 0, "m3u8_builder_build_index_playlist: vod_alloc failed"
			);
			return VOD_ALLOC_FAILED;
		}
	}

	if (encryption_params->type != HLS_ENC_NONE && suffix != &m3u8_vtt_suffix) {
		p = m3u8_builder_write_encryption(p, m3u8_key, conf, media_set, encryption_params, temp_buffer);
	}
#endif // NGX_HAVE_OPENSSL_EVP

	if (container_format == HLS_CONTAINER_FMP4) {
		p = vod_copy(p, m3u8_map_prefix, sizeof(m3u8_map_prefix) - 1);
		p = vod_copy(p, base_url->data, base_url->len);
		p = vod_copy(p, conf->init_file_name_prefix.data, conf->init_file_name_prefix.len);
		if (media_set->use_discontinuity && media_set->initial_clip_index != INVALID_CLIP_INDEX) {
			clip_index = media_set->initial_clip_index + 1;
			p = vod_sprintf(p, m3u8_clip_index, clip_index++);
		}
		p = vod_copy(p, name_suffix.data, name_suffix.len - suffix->len);
		p = vod_copy(p, m3u8_map_suffix, sizeof(m3u8_map_suffix) - 1);
	}

	// write the segments
	for (cur_item = segment_durations.items; cur_item < last_item; cur_item++) {
		segment_index = cur_item->segment_index;
		last_segment_index = segment_index + cur_item->repeat_count;

		if (cur_item->discontinuity) {
			p = vod_copy(p, m3u8_discontinuity, sizeof(m3u8_discontinuity) - 1);
			if (container_format == HLS_CONTAINER_FMP4
			    && cur_item > segment_durations.items
			    && media_set->initial_clip_index != INVALID_CLIP_INDEX) {
				p = vod_copy(p, m3u8_map_prefix, sizeof(m3u8_map_prefix) - 1);
				p = vod_copy(p, base_url->data, base_url->len);
				p = vod_copy(p, conf->init_file_name_prefix.data, conf->init_file_name_prefix.len);
				p = vod_sprintf(p, m3u8_clip_index, clip_index++);
				p = vod_copy(p, name_suffix.data, name_suffix.len - suffix->len);
				p = vod_copy(p, m3u8_map_suffix, sizeof(m3u8_map_suffix) - 1);
			}
		}

		// ignore zero duration segments (caused by alignment to keyframes)
		if (cur_item->duration == 0) {
			continue;
		}

		// write the first segment
		extinf.data = p;
		p = m3u8_builder_append_extinf_tag(
			p, rescale_time(cur_item->duration, segment_durations.timescale, scale), scale
		);
		extinf.len = p - extinf.data;
		p = m3u8_builder_append_segment_name(
			p, segments_base_url, &conf->segment_file_name_prefix, segment_index, &name_suffix
		);
		segment_index++;

		// write any additional segments
		for (; segment_index < last_segment_index; segment_index++) {
			p = vod_copy(p, extinf.data, extinf.len);
			p = m3u8_builder_append_segment_name(
				p, segments_base_url, &conf->segment_file_name_prefix, segment_index, &name_suffix
			);
		}
	}

	// write the footer
	if (media_set->presentation_end) {
		p = vod_copy(p, m3u8_endlist, sizeof(m3u8_endlist) - 1);
	}

	result->len = p - result->data;

	if (result->len > result_size) {
		vod_log_error(
			VOD_LOG_ERR,
			request_context->log,
			0,
			"m3u8_builder_build_index_playlist: result length %uz exceeded allocated length %uz",
			result->len,
			result_size
		);
		return VOD_UNEXPECTED;
	}

	return VOD_OK;
}

static uint32_t
m3u8_builder_get_audio_codec_count(adaptation_sets_t* adaptation_sets, media_track_t** audio_codec_tracks) {
	adaptation_set_t* last_adaptation_set;
	adaptation_set_t* cur_adaptation_set;
	media_track_t* cur_track;
	uint32_t seen_codecs = 0;
	uint32_t codec_flag;
	uint32_t count = 0;

	cur_adaptation_set = adaptation_sets->first_by_type[MEDIA_TYPE_AUDIO];
	last_adaptation_set = cur_adaptation_set + adaptation_sets->count[MEDIA_TYPE_AUDIO];
	for (; cur_adaptation_set < last_adaptation_set; cur_adaptation_set++) {
		cur_track = cur_adaptation_set->first[0];
		codec_flag = 1 << (cur_track->media_info.codec_id - VOD_CODEC_ID_AUDIO);
		if ((seen_codecs & codec_flag) != 0) {
			continue;
		}

		seen_codecs |= codec_flag;
		*audio_codec_tracks++ = cur_track;
		count++;
	}

	return count;
}

static u_char*
m3u8_builder_append_index_url(
	u_char* p, vod_str_t* prefix, media_set_t* media_set, media_track_t* track, vod_str_t* base_url
) {
	bool_t write_sequence = media_set->has_multi_sequences;

	if (base_url->len != 0) {
		// absolute url only
		p = vod_copy(p, base_url->data, base_url->len);
		if (p[-1] != '/') {
			if (track->file_info.uri.len != 0) {
				p = vod_copy(p, track->file_info.uri.data, track->file_info.uri.len);

				// no need to pass the sequence index since we have a direct uri
				write_sequence = FALSE;
			} else {
				p = vod_copy(p, media_set->uri.data, media_set->uri.len);
			}
			*p++ = '/';
		}
	}

	p = vod_copy(p, prefix->data, prefix->len);
	*p++ = '-'; // NOTE: assume has_multi_sequences is always `TRUE` for subtitles.
	p = manifest_utils_append_track_spec(p, track->file_info.source->sequence, track, write_sequence);
	p = vod_copy(p, m3u8_url_suffix, sizeof(m3u8_url_suffix) - 1);

	return p;
}

static size_t
m3u8_builder_get_closed_captions_size(media_set_t* media_set, request_context_t* request_context) {
	media_closed_captions_t* closed_captions;
	size_t result = 0;
	size_t base = (sizeof(m3u8_media_base) - 1)
	            + (sizeof(m3u8_media_type_closed_captions) - 1)
	            + ((sizeof(m3u8_group_id_closed_captions) - 1) + VOD_INT32_LEN)
	            + (sizeof(m3u8_media_language) - 1)
	            + (sizeof(m3u8_media_instream_id) - 1)
	            + (sizeof(m3u8_media_default) - 1);

	for (closed_captions = media_set->closed_captions; closed_captions < media_set->closed_captions_end; closed_captions++) {
		result += base
		        + closed_captions->id.len
		        + closed_captions->label.len
		        + closed_captions->language.len
		        + (sizeof("\n") - 1);
	}

	return result + sizeof("\n") - 1;
}

static u_char*
m3u8_builder_write_closed_captions(u_char* p, media_set_t* media_set) {
	media_closed_captions_t* closed_captions;
	uint32_t index = 0;
	bool_t is_default;

	for (closed_captions = media_set->closed_captions; closed_captions < media_set->closed_captions_end; closed_captions++) {
		p = vod_sprintf(
			p,
			m3u8_media_base,
			m3u8_media_type_closed_captions,
			m3u8_group_id_closed_captions,
			index,
			&closed_captions->label
		);

		if (closed_captions->language.len != 0) {
			p = vod_sprintf(p, m3u8_media_language, &closed_captions->language);
		}

		is_default = closed_captions->is_default;
		if (is_default < 0) {
			is_default = closed_captions == media_set->closed_captions;
		}

		if (is_default) {
			p = vod_copy(p, m3u8_media_default, sizeof(m3u8_media_default) - 1);
		} else {
			p = vod_copy(p, m3u8_media_non_default, sizeof(m3u8_media_non_default) - 1);
		}

		p = vod_sprintf(p, m3u8_media_instream_id, (vod_str_t*)&closed_captions->id);

		*p++ = '\n';
	}

	*p++ = '\n';

	return p;
}

static size_t
m3u8_builder_get_media_size(
	adaptation_sets_t* adaptation_sets,
	vod_str_t* base_url,
	size_t base_url_len,
	media_set_t* media_set,
	uint32_t media_type
) {
	adaptation_set_t* first_adaptation_set;
	adaptation_set_t* last_adaptation_set;
	adaptation_set_t* adaptation_set;
	media_track_t* cur_track;
	size_t result = (sizeof("\n\n") - 1)
	              + (((sizeof(m3u8_media_base) - 1) + VOD_INT32_LEN)
	                 + (sizeof(m3u8_media_type_subtitles) - 1)
	                 + (sizeof(m3u8_group_id_audio) - 1)
	                 + (sizeof(m3u8_media_language) - 1)
	                 + (sizeof(m3u8_media_default) - 1)
	                 + ((sizeof(m3u8_media_uri) - 1) + base_url_len)
	                 + (sizeof("\"\n") - 1))
	                    * adaptation_sets->count[media_type];

	first_adaptation_set = adaptation_sets->first_by_type[media_type];
	last_adaptation_set = first_adaptation_set + adaptation_sets->count[media_type];
	for (adaptation_set = first_adaptation_set; adaptation_set < last_adaptation_set; adaptation_set++) {
		cur_track = adaptation_set->first[0];

		result += vod_max(cur_track->media_info.tags.label.len, default_label.len)
		        + cur_track->media_info.tags.lang_str.len;

		if (cur_track->media_info.tags.characteristics.len != 0) {
			result += (sizeof(m3u8_media_characteristics) - 1)
			        + cur_track->media_info.tags.characteristics.len;
		}

		if (cur_track->media_info.tags.is_autoselect > 0) {
			result += sizeof(m3u8_media_autoselect) - 1;
		}

		if (cur_track->media_info.tags.is_forced > 0 && media_type == MEDIA_TYPE_SUBTITLE) {
			result += sizeof(m3u8_media_forced) - 1;
		}

		if (media_type == MEDIA_TYPE_AUDIO) {
			result += (sizeof(m3u8_media_channels) - 1) + VOD_INT32_LEN;
		}

		if (base_url->len != 0) {
			result += vod_max(cur_track->file_info.uri.len, media_set->uri.len);
		}
	}

	return result;
}

static u_char*
m3u8_builder_write_media(
	u_char* p,
	adaptation_sets_t* adaptation_sets,
	m3u8_config_t* conf,
	vod_str_t* base_url,
	media_set_t* media_set,
	uint32_t media_type
) {
	adaptation_set_t* first_adaptation_set;
	adaptation_set_t* last_adaptation_set;
	adaptation_set_t* adaptation_set;
	media_track_t* tracks[MEDIA_TYPE_COUNT];
	vod_str_t* label;
	uint32_t group_index = 0;
	uint32_t last_group_index = UINT_MAX;
	bool_t is_default;
	const u_char* group_id;
	const u_char* type;

	switch (media_type) {
	case MEDIA_TYPE_AUDIO:
		type = m3u8_media_type_audio;
		group_id = m3u8_group_id_audio;
		break;

	case MEDIA_TYPE_SUBTITLE:
		type = m3u8_media_type_subtitles;
		group_id = m3u8_group_id_subtitles;
		break;

	default:
		return p; // can't happen, just to avoid the warning
	}

	vod_memzero(tracks, sizeof(tracks));
	tracks[MEDIA_TYPE_VIDEO] = NULL;
	first_adaptation_set = adaptation_sets->first_by_type[media_type];
	last_adaptation_set = first_adaptation_set + adaptation_sets->count[media_type];
	for (adaptation_set = first_adaptation_set; adaptation_set < last_adaptation_set; adaptation_set++) {
		// take the last track assuming higher bitrate
		tracks[media_type] = adaptation_set->last[-1];

		// output EXT-X-MEDIA
		if (media_type == MEDIA_TYPE_AUDIO) {
			group_index = tracks[media_type]->media_info.codec_id - VOD_CODEC_ID_AUDIO;
		}

		label = &tracks[media_type]->media_info.tags.label;
		if (label->len == 0) {
			label = &default_label;
		}

		p = vod_sprintf(p, m3u8_media_base, type, group_id, group_index, label);

		if (tracks[media_type]->media_info.tags.lang_str.len > 0) {
			p = vod_sprintf(p, m3u8_media_language, &tracks[media_type]->media_info.tags.lang_str);
		}

		is_default = tracks[media_type]->media_info.tags.is_default;
		if (is_default < 0) {
			is_default = adaptation_set == first_adaptation_set || group_index != last_group_index;
			last_group_index = group_index;
		}

		if (is_default) {
			p = vod_copy(p, m3u8_media_default, sizeof(m3u8_media_default) - 1);
		}

		if (tracks[media_type]->media_info.tags.is_autoselect > 0) {
			p = vod_copy(p, m3u8_media_autoselect, sizeof(m3u8_media_autoselect) - 1);
		}

		if (tracks[media_type]->media_info.tags.characteristics.len != 0) {
			p = vod_sprintf(
				p, m3u8_media_characteristics, &tracks[media_type]->media_info.tags.characteristics
			);
		}

		if (tracks[media_type]->media_info.tags.is_forced > 0 && media_type == MEDIA_TYPE_SUBTITLE) {
			p = vod_copy(p, m3u8_media_forced, sizeof(m3u8_media_forced) - 1);
		}

		if (media_type == MEDIA_TYPE_AUDIO) {
			p = vod_sprintf(
				p, m3u8_media_channels, (uint32_t)tracks[media_type]->media_info.u.audio.channels
			);
		}

		p = vod_copy(p, m3u8_media_uri, sizeof(m3u8_media_uri) - 1);
		p = m3u8_builder_append_index_url(
			p, &conf->index_file_name_prefix, media_set, tracks[media_type], base_url
		);
		*p++ = '"';
		*p++ = '\n';
	}

	*p++ = '\n';

	return p;
}

static u_char*
m3u8_builder_write_video_range(u_char* p, media_info_t* media_info) {
	if (media_info->format == FORMAT_DVH1) {
		p = vod_copy(p, m3u8_stream_inf_video_range_pq, sizeof(m3u8_stream_inf_video_range_pq) - 1);
		return p;
	}

	switch (media_info->u.video.transfer_characteristics) {
	case 1:
	case 6:
	case 13:
	case 14:
	case 15:
		p = vod_copy(p, m3u8_stream_inf_video_range_sdr, sizeof(m3u8_stream_inf_video_range_sdr) - 1);
		break;

	case 16:
		p = vod_copy(p, m3u8_stream_inf_video_range_pq, sizeof(m3u8_stream_inf_video_range_pq) - 1);
		break;

	case 18:
		p = vod_copy(p, m3u8_stream_inf_video_range_hlg, sizeof(m3u8_stream_inf_video_range_hlg) - 1);
		break;
	}

	return p;
}

static u_char*
m3u8_builder_write_variants(
	u_char* p,
	adaptation_sets_t* adaptation_sets,
	m3u8_config_t* conf,
	vod_str_t* base_url,
	media_set_t* media_set,
	media_track_t* group_audio_track
) {
	adaptation_set_t* adaptation_set = adaptation_sets->first;
	media_track_t** cur_track_ptr;
	media_track_t* tracks[MEDIA_TYPE_COUNT];
	media_info_t* video = NULL;
	media_info_t* audio = NULL;
	uint32_t bitrate;
	uint32_t avg_bitrate;

	vod_memzero(tracks, sizeof(tracks));

	for (cur_track_ptr = adaptation_set->first; cur_track_ptr < adaptation_set->last; cur_track_ptr++) {
		// get the audio / video tracks
		// NOTE: this is ok because the adaptation types enum is aligned with media types
		tracks[adaptation_set->type] = cur_track_ptr[0];

		// output EXT-X-STREAM-INF
		if (tracks[MEDIA_TYPE_VIDEO] != NULL) {
			video = &tracks[MEDIA_TYPE_VIDEO]->media_info;
			bitrate = video->bitrate;
			avg_bitrate = video->avg_bitrate;
			if (group_audio_track != NULL) {
				audio = &group_audio_track->media_info;
			} else {
				audio = NULL;
			}

			if (audio != NULL) {
				bitrate += audio->bitrate;
				avg_bitrate += audio->avg_bitrate;
			}

			p = vod_sprintf(
				p,
				m3u8_stream_inf_video,
				bitrate,
				(uint32_t)video->u.video.width,
				(uint32_t)video->u.video.height,
				(uint32_t)(video->timescale / video->min_frame_duration),
				(uint32_t)((((uint64_t)video->timescale * 1000) / video->min_frame_duration) % 1000),
				&video->codec_name
			);
			if (audio != NULL) {
				*p++ = ',';
				p = vod_copy(p, audio->codec_name.data, audio->codec_name.len);
			}
		} else {
			if (group_audio_track != NULL) {
				audio = &group_audio_track->media_info;
			} else {
				audio = &tracks[MEDIA_TYPE_AUDIO]->media_info;
			}

			avg_bitrate = audio->avg_bitrate;
			p = vod_sprintf(p, m3u8_stream_inf_audio, audio->bitrate, &audio->codec_name);
		}

		*p++ = '\"';

		if (avg_bitrate != 0) {
			p = vod_sprintf(p, m3u8_stream_inf_average_bandwidth, avg_bitrate);
		}

		if (tracks[MEDIA_TYPE_VIDEO] != NULL) {
			p = m3u8_builder_write_video_range(p, video);
		}

		if (adaptation_sets->count[ADAPTATION_TYPE_AUDIO] > 0 && adaptation_sets->total_count > 1) {
			p = vod_sprintf(
				p, m3u8_stream_inf_audio_group, group_audio_track->media_info.codec_id - VOD_CODEC_ID_AUDIO
			);
		}
		if (adaptation_sets->count[ADAPTATION_TYPE_SUBTITLE] > 0) {
			p = vod_sprintf(p, m3u8_stream_inf_subtitles_group, 0);
		}
		if (media_set->closed_captions < media_set->closed_captions_end) {
			p = vod_sprintf(p, m3u8_stream_inf_closed_captions_group, 0);
		} else if (media_set->closed_captions != NULL) {
			p = vod_copy(
				p, m3u8_stream_inf_no_closed_captions, sizeof(m3u8_stream_inf_no_closed_captions) - 1
			);
		}
		*p++ = '\n';

		// output the url
		p = m3u8_builder_append_index_url(
			p, &conf->index_file_name_prefix, media_set, tracks[adaptation_set->type], base_url
		);
		*p++ = '\n';
	}

	*p++ = '\n';

	return p;
}

static u_char*
m3u8_builder_write_iframe_variants(
	u_char* p, adaptation_set_t* adaptation_set, m3u8_config_t* conf, vod_str_t* base_url, media_set_t* media_set
) {
	media_track_t** cur_track_ptr;
	media_track_t* tracks[MEDIA_TYPE_COUNT];
	media_info_t* video;

	vod_memzero(tracks, sizeof(tracks));

	for (cur_track_ptr = adaptation_set->first; cur_track_ptr < adaptation_set->last; cur_track_ptr++) {
		// get the audio / video tracks
		// NOTE: this is ok because the adaptation types enum is aligned with media types
		tracks[adaptation_set->type] = cur_track_ptr[0];

		if (tracks[MEDIA_TYPE_VIDEO] == NULL) {
			continue;
		}

		video = &tracks[MEDIA_TYPE_VIDEO]->media_info;
		if (conf->container_format == HLS_CONTAINER_AUTO && video->codec_id != VOD_CODEC_ID_AVC) {
			continue;
		}

		if (video->u.video.key_frame_bitrate == 0 || !mp4_to_annexb_simulation_supported(video)) {
			continue;
		}

		p = vod_sprintf(
			p,
			m3u8_iframe_stream_inf,
			video->u.video.key_frame_bitrate,
			(uint32_t)video->u.video.width,
			(uint32_t)video->u.video.height,
			&video->codec_name
		);

		p = m3u8_builder_append_index_url(
			p, &conf->iframes_file_name_prefix, media_set, tracks[adaptation_set->type], base_url
		);
		*p++ = '\"';

		p = m3u8_builder_write_video_range(p, video);

		*p++ = '\n';
	}

	return p;
}

vod_status_t
m3u8_builder_build_master_playlist(
	request_context_t* request_context,
	m3u8_config_t* conf,
	hls_encryption_params_t* encryption_params,
	vod_str_t* base_url,
	media_set_t* media_set,
	vod_str_t* result
) {
	adaptation_sets_t adaptation_sets;
	media_track_t** last_audio_codec_track;
	media_track_t** cur_track_ptr;
	media_track_t* audio_codec_tracks[VOD_CODEC_ID_SUBTITLE - VOD_CODEC_ID_AUDIO];
	media_track_t* cur_track;
	vod_status_t rc;
	uint32_t variant_set_count = 1;
	uint32_t variant_set_size;
	uint32_t flags;
	bool_t iframe_playlist;
	size_t max_video_stream_inf;
	size_t base_url_len;
	size_t result_size;
	u_char* p;
	bool_t alternative_audio;

#if (NGX_HAVE_OPENSSL_EVP)
	size_t max_pssh_size = 0;
	u_char* temp_buffer = NULL;
#endif // NGX_HAVE_OPENSSL_EVP

	// get the adaptations sets
	flags = ADAPTATION_SETS_FLAG_SINGLE_LANG_TRACK | ADAPTATION_SETS_FLAG_MULTI_AUDIO_CODEC;

	rc = manifest_utils_get_adaptation_sets(request_context, media_set, flags, &adaptation_sets);
	if (rc != VOD_OK) {
		return rc;
	}

	iframe_playlist = conf->output_iframes_playlist
	               && (media_set->type == MEDIA_SET_VOD || media_set->is_live_event)
	               && media_set->timing.total_count <= 1
	               && encryption_params->type == HLS_ENC_NONE
	               && conf->container_format != HLS_CONTAINER_FMP4
	               && !media_set->audio_filtering_needed
	               && adaptation_sets.first->type == ADAPTATION_TYPE_VIDEO;

	// get the response size
	base_url_len = base_url->len
	             + 1 // '/'
	             + conf->index_file_name_prefix.len
	             + MANIFEST_UTILS_TRACK_SPEC_MAX_SIZE
	             + (sizeof(m3u8_url_suffix) - 1);

	result_size = (sizeof(m3u8_header_base) - 1) + VOD_INT32_LEN;

	if (media_set->segmenter_conf->align_to_key_frames) {
		result_size += sizeof(m3u8_independent_segments) - 1;
	}

	max_video_stream_inf =
		((sizeof(m3u8_stream_inf_video) - 1) + 5 * VOD_INT32_LEN + 2 * MAX_CODEC_NAME_SIZE + 1) // ','
		+ ((sizeof(m3u8_stream_inf_average_bandwidth) - 1) + VOD_INT32_LEN)
		+ (sizeof(m3u8_stream_inf_video_range_sdr) - 1)
		+ (sizeof("\"\n\n") - 1);

	alternative_audio =
		adaptation_sets.count[ADAPTATION_TYPE_AUDIO] > 0 && adaptation_sets.total_count > 1;

	if (alternative_audio) {
		// alternative audio
		// NOTE: in case of audio only, the first track is printed twice - once as #EXT-X-STREAM-INF
		// and once as #EXT-X-MEDIA
		result_size += m3u8_builder_get_media_size(
			&adaptation_sets, base_url, base_url_len, media_set, MEDIA_TYPE_AUDIO
		);

		max_video_stream_inf += (sizeof(m3u8_stream_inf_audio_group) - 1) + VOD_INT32_LEN;

		// count the number of audio codecs
		vod_memzero(audio_codec_tracks, sizeof(audio_codec_tracks));
		variant_set_count = m3u8_builder_get_audio_codec_count(&adaptation_sets, audio_codec_tracks);
	}

	if (adaptation_sets.count[ADAPTATION_TYPE_SUBTITLE] > 0) {
		// subtitles
		result_size += m3u8_builder_get_media_size(
			&adaptation_sets, base_url, base_url_len, media_set, MEDIA_TYPE_SUBTITLE
		);

		max_video_stream_inf += (sizeof(m3u8_stream_inf_subtitles_group) - 1) + VOD_INT32_LEN;
	}

	if (media_set->closed_captions < media_set->closed_captions_end) {
		result_size += m3u8_builder_get_closed_captions_size(media_set, request_context);

		max_video_stream_inf += sizeof(m3u8_stream_inf_closed_captions_group) - 1;
	} else if (media_set->closed_captions != NULL) {
		max_video_stream_inf += sizeof(m3u8_stream_inf_no_closed_captions) - 1;
	}

	// variants
	variant_set_size =
		(max_video_stream_inf + base_url_len) // using only video since it's larger than audio
		* adaptation_sets.first->count;

	if (base_url->len != 0) {
		for (cur_track_ptr = adaptation_sets.first->first; cur_track_ptr < adaptation_sets.first->last; cur_track_ptr++) {
			cur_track = cur_track_ptr[0];
			if (cur_track == NULL) {
				cur_track = cur_track_ptr[1];
			}

			variant_set_size += vod_max(cur_track->file_info.uri.len, media_set->uri.len);
		}
	}

	result_size += variant_set_size * variant_set_count;

	// iframe playlist
	if (iframe_playlist) {
		result_size +=
			(((sizeof(m3u8_iframe_stream_inf) - 1)
		      + 3 * VOD_INT32_LEN
		      + MAX_CODEC_NAME_SIZE
		      + (sizeof("\"\n\n") - 1))
		     + (base_url_len - conf->index_file_name_prefix.len + conf->iframes_file_name_prefix.len)
		     + (sizeof(m3u8_stream_inf_video_range_sdr) - 1))
			* adaptation_sets.first->count;
	}

#if (NGX_HAVE_OPENSSL_EVP)
	if (encryption_params->type != HLS_ENC_NONE) {
		result_size += m3u8_builder_get_encryption_size(
			m3u8_session_key, conf, media_set->sequences[0].drm_info, encryption_params, &max_pssh_size
		);
	}
#endif // NGX_HAVE_OPENSSL_EVP

	// allocate the buffer
	result->data = vod_alloc(request_context->pool, result_size);
	if (result->data == NULL) {
		vod_log_debug0(
			VOD_LOG_DEBUG_LEVEL, request_context->log, 0, "m3u8_builder_build_master_playlist: vod_alloc failed (2)"
		);
		return VOD_ALLOC_FAILED;
	}

	// write the header
	p = vod_sprintf(result->data, m3u8_header_base, conf->m3u8_version);

	if (media_set->segmenter_conf->align_to_key_frames) {
		p = vod_copy(p, m3u8_independent_segments, sizeof(m3u8_independent_segments) - 1);
	}

#if (NGX_HAVE_OPENSSL_EVP)
	if (max_pssh_size > 0) {
		temp_buffer = vod_alloc(request_context->pool, max_pssh_size);
		if (temp_buffer == NULL) {
			vod_log_debug0(
				VOD_LOG_DEBUG_LEVEL, request_context->log, 0, "m3u8_builder_build_index_playlist: vod_alloc failed"
			);
			return VOD_ALLOC_FAILED;
		}
	}

	if (encryption_params->type != HLS_ENC_NONE) {
		p = m3u8_builder_write_encryption(
			p, m3u8_session_key, conf, media_set, encryption_params, temp_buffer
		);
	}
#endif // NGX_HAVE_OPENSSL_EVP

	*p++ = '\n';

	if (alternative_audio) {
		// output alternative audio
		p = m3u8_builder_write_media(p, &adaptation_sets, conf, base_url, media_set, MEDIA_TYPE_AUDIO);
	}

	if (adaptation_sets.count[ADAPTATION_TYPE_SUBTITLE] > 0) {
		// output subtitles
		p = m3u8_builder_write_media(p, &adaptation_sets, conf, base_url, media_set, MEDIA_TYPE_SUBTITLE);
	}

	if (media_set->closed_captions < media_set->closed_captions_end) {
		p = m3u8_builder_write_closed_captions(p, media_set);
	}

	// output variants
	if (variant_set_count > 1) {
		last_audio_codec_track = audio_codec_tracks + variant_set_count;
		for (cur_track_ptr = audio_codec_tracks; cur_track_ptr < last_audio_codec_track; cur_track_ptr++) {
			p = m3u8_builder_write_variants(p, &adaptation_sets, conf, base_url, media_set, *cur_track_ptr);
		}
	} else {
		p = m3u8_builder_write_variants(
			p,
			&adaptation_sets,
			conf,
			base_url,
			media_set,
			alternative_audio ? adaptation_sets.first_by_type[ADAPTATION_TYPE_AUDIO]->first[0] : NULL
		);
	}

	// iframes
	if (iframe_playlist) {
		p = m3u8_builder_write_iframe_variants(p, adaptation_sets.first, conf, base_url, media_set);
	}

	result->len = p - result->data;

	if (result->len > result_size) {
		vod_log_error(
			VOD_LOG_ERR,
			request_context->log,
			0,
			"m3u8_builder_build_master_playlist: result length %uz exceeded allocated length %uz",
			result->len,
			result_size
		);
		return VOD_UNEXPECTED;
	}

	return VOD_OK;
}

void
m3u8_builder_init_config(m3u8_config_t* conf, uint32_t max_segment_duration) {
	conf->iframes_m3u8_header_len = vod_snprintf(
										conf->iframes_m3u8_header,
										sizeof(conf->iframes_m3u8_header) - 1,
										iframes_m3u8_header_format,
										vod_div_ceil(max_segment_duration, 1000)
									)
	                              - conf->iframes_m3u8_header;
}
