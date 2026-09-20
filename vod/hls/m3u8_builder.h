#ifndef __M3U8_BUILDER_H__
#define __M3U8_BUILDER_H__

// includes
#include "../media_format.h"
#include "hls_encryption.h"
#include "hls_muxer.h"

// typedefs
enum {
	HLS_CONTAINER_AUTO,
	HLS_CONTAINER_MPEGTS,
	HLS_CONTAINER_FMP4,
};

typedef struct {
	vod_uint_t m3u8_version;
	vod_uint_t container_format;
	bool_t output_iframes_playlist;
	vod_str_t index_file_name_prefix;
	vod_str_t iframes_file_name_prefix;
	vod_str_t segment_file_name_prefix;
	vod_str_t init_file_name_prefix;
	vod_str_t encryption_key_format;
	vod_str_t encryption_key_format_versions;
} m3u8_config_t;

// functions
static vod_inline bool_t
m3u8_builder_is_fmp4_container(
	vod_uint_t container_format, vod_uint_t encryption_method, uint32_t video_codec_id
) {
	if (container_format != HLS_CONTAINER_AUTO) {
		return container_format == HLS_CONTAINER_FMP4;
	}

	return encryption_method == HLS_ENC_SAMPLE_AES_CTR || video_codec_id != VOD_CODEC_ID_AVC;
}

vod_status_t m3u8_builder_build_master_playlist(
	request_context_t* request_context,
	m3u8_config_t* conf,
	hls_encryption_params_t* encryption_params,
	vod_str_t* base_url,
	media_set_t* media_set,
	vod_str_t* result
);

vod_status_t m3u8_builder_build_index_playlist(
	request_context_t* request_context,
	m3u8_config_t* conf,
	vod_str_t* base_url,
	vod_str_t* segments_base_url,
	hls_encryption_params_t* encryption_params,
	vod_uint_t container_format,
	media_set_t* media_set,
	vod_str_t* result
);

vod_status_t m3u8_builder_build_iframe_playlist(
	request_context_t* request_context,
	m3u8_config_t* conf,
	hls_mpegts_muxer_conf_t* muxer_conf,
	vod_uint_t container_format,
	vod_str_t* base_url,
	media_set_t* media_set,
	vod_str_t* result
);

#endif // __M3U8_BUILDER_H__
