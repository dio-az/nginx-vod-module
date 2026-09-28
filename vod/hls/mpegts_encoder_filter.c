#include "mpegts_encoder_filter.h"
#include "../common.h"
#include "bit_fields.h"

#define THIS_FILTER (MEDIA_FILTER_MPEGTS)

#define member_size(type, member) sizeof(((type*)0)->member)
#define get_context(ctx) ((mpegts_encoder_state_t*)ctx->context[THIS_FILTER])

#define PCR_PID (0x100)
#define PRIVATE_STREAM_1_SID (0xBD)
#define FIRST_AUDIO_SID (0xC0)
#define FIRST_VIDEO_SID (0xE0)

#define SIZEOF_MPEGTS_HEADER (4)
#define MPEGTS_PACKET_USABLE_SIZE (MPEGTS_PACKET_SIZE - SIZEOF_MPEGTS_HEADER)
#define SIZEOF_MPEGTS_ADAPTATION_FIELD (2)
#define SIZEOF_PCR (6)
#define PMT_LENGTH_END_OFFSET (4)
#define SIZEOF_PES_HEADER (6)
#define SIZEOF_PES_OPTIONAL_HEADER (3)
#define SIZEOF_PES_PTS (5)

#define NO_TIMESTAMP ((uint64_t)-1)

#ifndef FF_PROFILE_AAC_HE
#define FF_PROFILE_AAC_HE (4)
#endif

#ifndef FF_PROFILE_AAC_HE_V2
#define FF_PROFILE_AAC_HE_V2 (28)
#endif

#define SAMPLE_AES_AC3_EXTRA_DATA_SIZE (10)

// sample aes structs
typedef struct {
	u_char descriptor_tag[1];
	u_char descriptor_length[1];
	u_char format_identifier[4];
} registration_descriptor_t;

typedef struct {
	u_char audio_type[4];
	u_char priming[2];
	u_char version[1];
	u_char setup_data_length[1];
} audio_setup_information_t;

// clang-format off
static const u_char pat_packet[] = {
	0x47, 0x40, 0x00, 0x10, 0x00,                   // TS
	0x00, 0xB0, 0x0D, 0x00, 0x01, 0xC1, 0x00, 0x00, // PSI
	0x00, 0x01, 0xEF, 0xFF,                         // PAT
	0x36, 0x90, 0xE2, 0x3D,                         // CRC
};

static const u_char pmt_header_template[] = {
	0x47, 0x4F, 0xFF, 0x10,                                     // TS
	0x00, 0x02, 0xB0, 0x00, 0x00, 0x01, 0xC1, 0x00, 0x00,       // PSI
	0xE1, 0x00, 0xF0, 0x11,                                     // PMT
	0x25, 0x0F, 0xFF, 0xFF, 0x49, 0x44, 0x33, 0x20, 0xFF, 0x49, // Program descriptors
	0x44, 0x33, 0x20, 0x00, 0x1F, 0x00, 0x01,
};

static const u_char pmt_entry_template_hevc[] = {
	0x06, 0xE0, 0x00, 0xF0, 0x06,
	0x05, 0x04, 0x48, 0x45, 0x56, 0x43, // registration_descriptor('HEVC')
};

static const u_char pmt_entry_template_avc[] = {
	0x1B, 0xE0, 0x00, 0xF0, 0x00,
};

static const u_char pmt_entry_template_aac[] = {
	0x0F, 0xE0, 0x00, 0xF0, 0x00,
};

static const u_char pmt_entry_template_mp3[] = {
	0x03, 0xE0, 0x00, 0xF0, 0x00,
};

static const u_char pmt_entry_template_dts[] = {
	0x82, 0xE0, 0x00, 0xF0, 0x00,
};

static const u_char pmt_entry_template_ac3[] = {
	0x81, 0xE0, 0x00, 0xF0, 0x00,
};

static const u_char pmt_entry_template_sample_aes_avc[] = {
	0xDB, 0xE0, 0x00, 0xF0, 0x06,
	0x0F, 0x04, 0x7A, 0x61, 0x76, 0x63, // private_data_indicator_descriptor('zavc')
};

static const u_char pmt_entry_template_sample_aes_aac[] = {
	0xCF, 0xE1, 0x00, 0xF0, 0x00,
	0x0F, 0x04, 0x61, 0x61, 0x63, 0x64, // private_data_indicator_descriptor('aacd')
};

static const u_char pmt_entry_template_sample_aes_ac3[] = {
	0xC1, 0xE1, 0x00, 0xF0, 0x00,
	0x0F, 0x04, 0x61, 0x63, 0x33, 0x64, // private_data_indicator_descriptor('ac3d')
};

static const u_char pmt_entry_template_sample_aes_eac3[] = {
	0xC2, 0xE1, 0x00, 0xF0, 0x00,
	0x0F, 0x04, 0x65, 0x63, 0x33, 0x64, // private_data_indicator_descriptor('ec3d')
};

static const u_char pmt_entry_template_id3[] = {
	0x15, 0xE0, 0x00, 0xF0, 0x0F, 0x26, 0x0D, 0xFF, 0xFF, 0x49,
	0x44, 0x33, 0x20, 0xFF, 0x49, 0x44, 0x33, 0x20, 0x00, 0x0F,
};

// NOTE: according to the sample-aes spec, this should be the first 10 bytes of the audio data in
// practice, sending only the AC-3 syncframe magic is good enough (without the magic it doesn't play)
static u_char ac3_extra_data[SAMPLE_AES_AC3_EXTRA_DATA_SIZE] = {
	0x0B, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// from ffmpeg
static const uint32_t crc_table[256] = {
	0x00000000, 0x04C11DB7, 0x09823B6E, 0x0D4326D9, 0x130476DC, 0x17C56B6B,
	0x1A864DB2, 0x1E475005, 0x2608EDB8, 0x22C9F00F, 0x2F8AD6D6, 0x2B4BCB61,
	0x350C9B64, 0x31CD86D3, 0x3C8EA00A, 0x384FBDBD, 0x4C11DB70, 0x48D0C6C7,
	0x4593E01E, 0x4152FDA9, 0x5F15ADAC, 0x5BD4B01B, 0x569796C2, 0x52568B75,
	0x6A1936C8, 0x6ED82B7F, 0x639B0DA6, 0x675A1011, 0x791D4014, 0x7DDC5DA3,
	0x709F7B7A, 0x745E66CD, 0x9823B6E0, 0x9CE2AB57, 0x91A18D8E, 0x95609039,
	0x8B27C03C, 0x8FE6DD8B, 0x82A5FB52, 0x8664E6E5, 0xBE2B5B58, 0xBAEA46EF,
	0xB7A96036, 0xB3687D81, 0xAD2F2D84, 0xA9EE3033, 0xA4AD16EA, 0xA06C0B5D,
	0xD4326D90, 0xD0F37027, 0xDDB056FE, 0xD9714B49, 0xC7361B4C, 0xC3F706FB,
	0xCEB42022, 0xCA753D95, 0xF23A8028, 0xF6FB9D9F, 0xFBB8BB46, 0xFF79A6F1,
	0xE13EF6F4, 0xE5FFEB43, 0xE8BCCD9A, 0xEC7DD02D, 0x34867077, 0x30476DC0,
	0x3D044B19, 0x39C556AE, 0x278206AB, 0x23431B1C, 0x2E003DC5, 0x2AC12072,
	0x128E9DCF, 0x164F8078, 0x1B0CA6A1, 0x1FCDBB16, 0x018AEB13, 0x054BF6A4,
	0x0808D07D, 0x0CC9CDCA, 0x7897AB07, 0x7C56B6B0, 0x71159069, 0x75D48DDE,
	0x6B93DDDB, 0x6F52C06C, 0x6211E6B5, 0x66D0FB02, 0x5E9F46BF, 0x5A5E5B08,
	0x571D7DD1, 0x53DC6066, 0x4D9B3063, 0x495A2DD4, 0x44190B0D, 0x40D816BA,
	0xACA5C697, 0xA864DB20, 0xA527FDF9, 0xA1E6E04E, 0xBFA1B04B, 0xBB60ADFC,
	0xB6238B25, 0xB2E29692, 0x8AAD2B2F, 0x8E6C3698, 0x832F1041, 0x87EE0DF6,
	0x99A95DF3, 0x9D684044, 0x902B669D, 0x94EA7B2A, 0xE0B41DE7, 0xE4750050,
	0xE9362689, 0xEDF73B3E, 0xF3B06B3B, 0xF771768C, 0xFA325055, 0xFEF34DE2,
	0xC6BCF05F, 0xC27DEDE8, 0xCF3ECB31, 0xCBFFD686, 0xD5B88683, 0xD1799B34,
	0xDC3ABDED, 0xD8FBA05A, 0x690CE0EE, 0x6DCDFD59, 0x608EDB80, 0x644FC637,
	0x7A089632, 0x7EC98B85, 0x738AAD5C, 0x774BB0EB, 0x4F040D56, 0x4BC510E1,
	0x46863638, 0x42472B8F, 0x5C007B8A, 0x58C1663D, 0x558240E4, 0x51435D53,
	0x251D3B9E, 0x21DC2629, 0x2C9F00F0, 0x285E1D47, 0x36194D42, 0x32D850F5,
	0x3F9B762C, 0x3B5A6B9B, 0x0315D626, 0x07D4CB91, 0x0A97ED48, 0x0E56F0FF,
	0x1011A0FA, 0x14D0BD4D, 0x19939B94, 0x1D528623, 0xF12F560E, 0xF5EE4BB9,
	0xF8AD6D60, 0xFC6C70D7, 0xE22B20D2, 0xE6EA3D65, 0xEBA91BBC, 0xEF68060B,
	0xD727BBB6, 0xD3E6A601, 0xDEA580D8, 0xDA649D6F, 0xC423CD6A, 0xC0E2D0DD,
	0xCDA1F604, 0xC960EBB3, 0xBD3E8D7E, 0xB9FF90C9, 0xB4BCB610, 0xB07DABA7,
	0xAE3AFBA2, 0xAAFBE615, 0xA7B8C0CC, 0xA379DD7B, 0x9B3660C6, 0x9FF77D71,
	0x92B45BA8, 0x9675461F, 0x8832161A, 0x8CF30BAD, 0x81B02D74, 0x857130C3,
	0x5D8A9099, 0x594B8D2E, 0x5408ABF7, 0x50C9B640, 0x4E8EE645, 0x4A4FFBF2,
	0x470CDD2B, 0x43CDC09C, 0x7B827D21, 0x7F436096, 0x7200464F, 0x76C15BF8,
	0x68860BFD, 0x6C47164A, 0x61043093, 0x65C52D24, 0x119B4BE9, 0x155A565E,
	0x18197087, 0x1CD86D30, 0x029F3D35, 0x065E2082, 0x0B1D065B, 0x0FDC1BEC,
	0x3793A651, 0x3352BBE6, 0x3E119D3F, 0x3AD08088, 0x2497D08D, 0x2056CD3A,
	0x2D15EBE3, 0x29D4F654, 0xC5A92679, 0xC1683BCE, 0xCC2B1D17, 0xC8EA00A0,
	0xD6AD50A5, 0xD26C4D12, 0xDF2F6BCB, 0xDBEE767C, 0xE3A1CBC1, 0xE760D676,
	0xEA23F0AF, 0xEEE2ED18, 0xF0A5BD1D, 0xF464A0AA, 0xF9278673, 0xFDE69BC4,
	0x89B8FD09, 0x8D79E0BE, 0x803AC667, 0x84FBDBD0, 0x9ABC8BD5, 0x9E7D9662,
	0x933EB0BB, 0x97FFAD0C, 0xAFB010B1, 0xAB710D06, 0xA6322BDF, 0xA2F33668,
	0xBCB4666D, 0xB8757BDA, 0xB5365D03, 0xB1F740B4,
};
// clang-format on

static uint32_t
mpegts_crc32(const u_char* data, int len) {
	const u_char* end = data + len;
	uint32_t crc = 0xFFFFFFFF;

	for (; data < end; data++) {
		crc = (crc << 8) ^ crc_table[((crc >> 24) ^ *data) & 0xFF];
	}

	return crc;
}

// stateless writing functions - copied from ngx_hls_mpegts with some refactoring
static u_char*
mpegts_write_pcr(u_char* p, uint64_t pcr) {
	*p++ = (u_char)(pcr >> 25);
	*p++ = (u_char)(pcr >> 17);
	*p++ = (u_char)(pcr >> 9);
	*p++ = (u_char)(pcr >> 1);
	*p++ = (u_char)(pcr << 7 | 0x7E);
	*p++ = 0;

	return p;
}

static u_char*
mpegts_write_pts(u_char* p, unsigned fb, uint64_t pts) {
	unsigned val;

	val = fb << 4 | (((pts >> 30) & 0x07) << 1) | 1;
	*p++ = (u_char)val;

	val = (((pts >> 15) & 0x7FFF) << 1) | 1;
	*p++ = (u_char)(val >> 8);
	*p++ = (u_char)val;

	val = (((pts) & 0x7FFF) << 1) | 1;
	*p++ = (u_char)(val >> 8);
	*p++ = (u_char)val;

	return p;
}

static vod_inline u_char*
mpegts_write_packet_header(u_char* p, unsigned pid, unsigned cc) {
	*p++ = 0x47;
	*p++ = (u_char)(pid >> 8);
	*p++ = (u_char)pid;
	*p++ = 0x10 | (cc & 0x0F); // payload

	return p;
}

static size_t
mpegts_get_pes_header_size(mpegts_stream_info_t* stream_info) {
	size_t result;
	bool_t write_dts = stream_info->media_type == MEDIA_TYPE_VIDEO;

	result = SIZEOF_PES_HEADER + SIZEOF_PES_OPTIONAL_HEADER + SIZEOF_PES_PTS;

	if (stream_info->pid == PCR_PID) {
		result += SIZEOF_MPEGTS_ADAPTATION_FIELD + SIZEOF_PCR;
	}
	if (write_dts) {
		result += SIZEOF_PES_PTS;
	}

	return result;
}

static u_char*
mpegts_write_pes_header(
	u_char* cur_packet_start,
	mpegts_stream_info_t* stream_info,
	output_frame_t* f,
	u_char** pes_size_ptr,
	bool_t data_aligned
) {
	unsigned header_size;
	unsigned flags;
	u_char* p = cur_packet_start + SIZEOF_MPEGTS_HEADER;
	bool_t write_dts = stream_info->media_type == MEDIA_TYPE_VIDEO;

	cur_packet_start[1] |= 0x40; // payload start indicator

	if (stream_info->pid == PCR_PID) {
		cur_packet_start[3] |= 0x20; // adaptation

		*p++ = 1 + SIZEOF_PCR; // size
		*p++ = 0x10;           // PCR

		p = mpegts_write_pcr(p, f->dts + INITIAL_PCR);
	}

	// PES header

	*p++ = 0x00;
	*p++ = 0x00;
	*p++ = 0x01;
	*p++ = (u_char)stream_info->sid;

	header_size = SIZEOF_PES_PTS;
	flags = 0x80; // PTS

	if (write_dts) {
		header_size += SIZEOF_PES_PTS;
		flags |= 0x40; // DTS
	}

	*pes_size_ptr = p;
	p += 2;                            // skip pes_size, updated later
	*p++ = data_aligned ? 0x84 : 0x80; // H222
	*p++ = (u_char)flags;
	*p++ = (u_char)header_size;

	p = mpegts_write_pts(p, flags >> 6, f->pts + INITIAL_DTS);

	if (write_dts) {
		p = mpegts_write_pts(p, 1, f->dts + INITIAL_DTS);
	}

	return p;
}

static void
mpegts_add_stuffing(u_char* packet, u_char* p, unsigned stuff_size) {
	u_char* base;

	if (packet[3] & 0x20) {
		// has adaptation
		base = &packet[5] + packet[4];
		vod_memmove(base + stuff_size, base, p - base);
		vod_memset(base, 0xFF, stuff_size);
		packet[4] += (u_char)stuff_size;
	} else {
		// no adaptation
		packet[3] |= 0x20;
		vod_memmove(&packet[4] + stuff_size, &packet[4], p - &packet[4]);

		packet[4] = (u_char)(stuff_size - 1);
		if (stuff_size >= 2) {
			packet[5] = 0;
			vod_memset(&packet[6], 0xFF, stuff_size - 2);
		}
	}
}

static void
mpegts_copy_and_stuff(u_char* dest_packet, u_char* src_packet, u_char* src_pos, unsigned stuff_size) {
	u_char* base;
	u_char* p;

	if (src_packet[3] & 0x20) {
		// has adaptation
		base = &src_packet[5] + src_packet[4];

		p = vod_copy(dest_packet, src_packet, base - src_packet);
		dest_packet[4] += (u_char)stuff_size;

		vod_memset(p, 0xFF, stuff_size);
		p += stuff_size;
	} else {
		// no adaptation
		base = &src_packet[4];

		p = vod_copy(dest_packet, src_packet, 4);
		dest_packet[3] |= 0x20;

		*p++ = (u_char)(stuff_size - 1);
		if (stuff_size >= 2) {
			*p++ = 0;
			vod_memset(p, 0xFF, stuff_size - 2);
			p += stuff_size - 2;
		}
	}

	vod_memcpy(p, base, src_pos - base);
}

////////////////////////////////////

// PAT/PMT write functions
vod_status_t
mpegts_encoder_init_streams(
	request_context_t* request_context,
	hls_encryption_params_t* encryption_params,
	mpegts_encoder_init_streams_state_t* stream_state,
	uint32_t segment_index
) {
	u_char* cur_packet;

	stream_state->request_context = request_context;
	stream_state->encryption_params = encryption_params;
	stream_state->segment_index = segment_index;
	stream_state->cur_pid = PCR_PID;
	stream_state->cur_video_sid = FIRST_VIDEO_SID;
	stream_state->cur_audio_sid = FIRST_AUDIO_SID;

	if (request_context->simulation_only) {
		stream_state->pmt_packet_start = NULL;
		return VOD_OK;
	}

	// append PAT packet
	cur_packet = vod_alloc(request_context->pool, MPEGTS_PACKET_SIZE * 2);
	if (cur_packet == NULL) {
		vod_log_debug0(
			VOD_LOG_DEBUG_LEVEL, request_context->log, 0, "mpegts_encoder_init_streams: vod_alloc failed"
		);
		return VOD_ALLOC_FAILED;
	}

	stream_state->pat_packet_start = cur_packet;

	vod_memcpy(cur_packet, pat_packet, sizeof(pat_packet));
	vod_memset(cur_packet + sizeof(pat_packet), 0xFF, MPEGTS_PACKET_SIZE - sizeof(pat_packet));

	// make sure the continuity counters of the PAT/PMT are continous between segments
	cur_packet[3] |= (segment_index & 0x0F);

	// append PMT packet
	cur_packet += MPEGTS_PACKET_SIZE;
	stream_state->pmt_packet_start = cur_packet;
	stream_state->pmt_packet_end = cur_packet + MPEGTS_PACKET_SIZE;

	vod_memcpy(cur_packet, pmt_header_template, sizeof(pmt_header_template));
	cur_packet[3] |= (segment_index & 0x0F);
	stream_state->pmt_packet_pos = cur_packet + sizeof(pmt_header_template);

	return VOD_OK;
}

static void
mpegts_encoder_write_sample_aes_audio_pmt_entry(
	request_context_t* request_context, u_char* start, int entry_size, media_info_t* media_info
) {
	vod_str_t extra_data;
	u_char* p;

	switch (media_info->codec_id) {
	case VOD_CODEC_ID_AC3:
		extra_data.data = ac3_extra_data;
		extra_data.len = sizeof(ac3_extra_data);
		p = vod_copy(start, pmt_entry_template_sample_aes_ac3, sizeof(pmt_entry_template_sample_aes_ac3));
		break;

	case VOD_CODEC_ID_EAC3:
		extra_data = media_info->extra_data;
		p = vod_copy(
			start, pmt_entry_template_sample_aes_eac3, sizeof(pmt_entry_template_sample_aes_eac3)
		);
		break;

	default:
		extra_data = media_info->extra_data;
		p = vod_copy(start, pmt_entry_template_sample_aes_aac, sizeof(pmt_entry_template_sample_aes_aac));
		break;
	}
	pmt_entry_set_es_info_length(start, entry_size - sizeof_pmt_entry);

	// registration_descriptor
	*p++ = 0x05;                                                     // descriptor tag
	*p++ = member_size(registration_descriptor_t, format_identifier) // descriptor length
	     + sizeof(audio_setup_information_t)
	     + extra_data.len;

	*p++ = 'a';
	*p++ = 'p';
	*p++ = 'a';
	*p++ = 'd';

	// audio_setup_information
	switch (media_info->codec_id) {
	case VOD_CODEC_ID_AC3:
		*p++ = 'z';
		*p++ = 'a';
		*p++ = 'c';
		*p++ = '3';
		break;

	case VOD_CODEC_ID_EAC3:
		*p++ = 'z';
		*p++ = 'e';
		*p++ = 'c';
		*p++ = '3';
		break;

	default:
		switch (media_info->u.audio.codec_config.object_type - 1) {
		case FF_PROFILE_AAC_HE:
			*p++ = 'z';
			*p++ = 'a';
			*p++ = 'c';
			*p++ = 'h';
			break;

		case FF_PROFILE_AAC_HE_V2:
			*p++ = 'z';
			*p++ = 'a';
			*p++ = 'c';
			*p++ = 'p';
			break;

		default:
			*p++ = 'z';
			*p++ = 'a';
			*p++ = 'a';
			*p++ = 'c';
			break;
		}
		break;
	}

	// priming
	*p++ = 0;
	*p++ = 0;

	*p++ = 1; // version

	*p++ = extra_data.len;
	vod_memcpy(p, extra_data.data, extra_data.len);
}

static vod_status_t
mpegts_encoder_add_stream(
	mpegts_encoder_init_streams_state_t* stream_state, media_track_t* track, mpegts_stream_info_t* stream_info
) {
	const u_char* pmt_entry;
	int pmt_entry_size;

	stream_info->pid = stream_state->cur_pid++;

	if (stream_state->pmt_packet_start == NULL) { // simulation only
		return VOD_OK;
	}

	switch (stream_info->media_type) {
	case MEDIA_TYPE_VIDEO:
		stream_info->sid = stream_state->cur_video_sid++;
		if (stream_state->encryption_params->type == HLS_ENC_SAMPLE_AES) {
			pmt_entry = pmt_entry_template_sample_aes_avc;
			pmt_entry_size = sizeof(pmt_entry_template_sample_aes_avc);
		} else {
			switch (track->media_info.codec_id) {
			case VOD_CODEC_ID_HEVC:
				pmt_entry = pmt_entry_template_hevc;
				pmt_entry_size = sizeof(pmt_entry_template_hevc);
				break;

			default:
				pmt_entry = pmt_entry_template_avc;
				pmt_entry_size = sizeof(pmt_entry_template_avc);
				break;
			}
		}
		break;

	case MEDIA_TYPE_AUDIO:
		stream_info->sid = stream_state->cur_audio_sid++;
		if (stream_state->encryption_params->type == HLS_ENC_SAMPLE_AES) {
			switch (track->media_info.codec_id) {
			case VOD_CODEC_ID_AC3:
				pmt_entry = pmt_entry_template_sample_aes_ac3;
				pmt_entry_size = sizeof(pmt_entry_template_sample_aes_ac3)
				               + sizeof(registration_descriptor_t)
				               + sizeof(audio_setup_information_t)
				               + SAMPLE_AES_AC3_EXTRA_DATA_SIZE;
				break;

			case VOD_CODEC_ID_EAC3:
				pmt_entry = pmt_entry_template_sample_aes_eac3;
				pmt_entry_size = sizeof(pmt_entry_template_sample_aes_eac3)
				               + sizeof(registration_descriptor_t)
				               + sizeof(audio_setup_information_t)
				               + track->media_info.extra_data.len;
				break;

			default:
				pmt_entry = pmt_entry_template_sample_aes_aac;
				pmt_entry_size = sizeof(pmt_entry_template_sample_aes_aac)
				               + sizeof(registration_descriptor_t)
				               + sizeof(audio_setup_information_t)
				               + track->media_info.extra_data.len;
				break;
			}
		} else {
			switch (track->media_info.codec_id) {
			case VOD_CODEC_ID_MP3:
				pmt_entry = pmt_entry_template_mp3;
				pmt_entry_size = sizeof(pmt_entry_template_mp3);
				break;

			case VOD_CODEC_ID_DTS:
				pmt_entry = pmt_entry_template_dts;
				pmt_entry_size = sizeof(pmt_entry_template_dts);
				break;

			case VOD_CODEC_ID_AC3:
			case VOD_CODEC_ID_EAC3:
				pmt_entry = pmt_entry_template_ac3;
				pmt_entry_size = sizeof(pmt_entry_template_ac3);
				break;

			default:
				pmt_entry = pmt_entry_template_aac;
				pmt_entry_size = sizeof(pmt_entry_template_aac);
				break;
			}
		}
		break;

	case MEDIA_TYPE_NONE:
		stream_info->sid = PRIVATE_STREAM_1_SID;
		pmt_entry = pmt_entry_template_id3;
		pmt_entry_size = sizeof(pmt_entry_template_id3);
		break;

	default:
		vod_log_error(
			VOD_LOG_ERR,
			stream_state->request_context->log,
			0,
			"mpegts_encoder_add_stream: invalid media type %d",
			stream_info->media_type
		);
		return VOD_BAD_REQUEST;
	}

	if (stream_state->pmt_packet_pos + pmt_entry_size + sizeof(uint32_t)
	    >= stream_state->pmt_packet_end) {
		vod_log_error(
			VOD_LOG_ERR,
			stream_state->request_context->log,
			0,
			"mpegts_encoder_add_stream: stream definitions overflow PMT size"
		);
		return VOD_BAD_DATA;
	}

	if (stream_info->media_type == MEDIA_TYPE_AUDIO
	    && stream_state->encryption_params->type == HLS_ENC_SAMPLE_AES) {
		mpegts_encoder_write_sample_aes_audio_pmt_entry(
			stream_state->request_context, stream_state->pmt_packet_pos, pmt_entry_size, &track->media_info
		);
	} else {
		vod_memcpy(stream_state->pmt_packet_pos, pmt_entry, pmt_entry_size);
	}
	pmt_entry_set_elementary_pid(stream_state->pmt_packet_pos, stream_info->pid);
	stream_state->pmt_packet_pos += pmt_entry_size;
	return VOD_OK;
}

void
mpegts_encoder_finalize_streams(mpegts_encoder_init_streams_state_t* stream_state, vod_str_t* ts_header) {
	u_char* p = stream_state->pmt_packet_pos;
	u_char* crc_start_offset;
	uint32_t crc;

	if (stream_state->pmt_packet_start == NULL) { // simulation only
		return;
	}

	// update the length in the PMT header
	pmt_set_section_length(
		stream_state->pmt_packet_start + SIZEOF_MPEGTS_HEADER,
		p - (stream_state->pmt_packet_start + SIZEOF_MPEGTS_HEADER + PMT_LENGTH_END_OFFSET) + sizeof(crc)
	);

	// append the CRC
	// NOTE: the PMT pointer field is not part of the CRC
	crc_start_offset = stream_state->pmt_packet_start + SIZEOF_MPEGTS_HEADER + 1;
	crc = mpegts_crc32(crc_start_offset, p - crc_start_offset);
	*p++ = (u_char)(crc >> 24);
	*p++ = (u_char)(crc >> 16);
	*p++ = (u_char)(crc >> 8);
	*p++ = (u_char)(crc);

	// set the padding
	vod_memset(p, 0xFF, stream_state->pmt_packet_end - p);

	ts_header->data = stream_state->pat_packet_start;
	ts_header->len = MPEGTS_PACKET_SIZE * 2;
}

// stateful functions
static vod_inline vod_status_t
mpegts_encoder_init_packet(mpegts_encoder_state_t* state, bool_t write_direct) {
	if (write_direct || !state->interleave_frames) {
		state->last_queue_offset = state->queue->cur_offset;

		state->cur_packet_start =
			write_buffer_queue_get_buffer(state->queue, MPEGTS_PACKET_SIZE, state);
		if (state->cur_packet_start == NULL) {
			vod_log_debug0(
				VOD_LOG_DEBUG_LEVEL,
				state->request_context->log,
				0,
				"mpegts_encoder_init_packet: write_buffer_queue_get_buffer failed"
			);
			return VOD_ALLOC_FAILED;
		}
	} else {
		state->cur_packet_start = state->temp_packet;
	}

	state->last_frame_pts = NO_TIMESTAMP;
	state->cur_packet_end = state->cur_packet_start + MPEGTS_PACKET_SIZE;
	state->cur_pos =
		mpegts_write_packet_header(state->cur_packet_start, state->stream_info.pid, state->cc);
	state->cc++;

	return VOD_OK;
}

static vod_status_t
mpegts_encoder_stuff_cur_packet(mpegts_encoder_state_t* state) {
	unsigned stuff_size = state->cur_packet_end - state->cur_pos;
	unsigned pes_size;
	u_char* cur_packet;

	if (state->pes_bytes_written != 0 && state->stream_info.media_type != MEDIA_TYPE_VIDEO) {
		// the trailing part of the last pes was not counted in its size, add it now
		pes_size = ((uint16_t)(state->cur_pes_size_ptr[0]) << 8) | state->cur_pes_size_ptr[1];
		pes_size += state->pes_bytes_written;
		if (pes_size > 0xFFFF) {
			pes_size = 0;
		}
		state->cur_pes_size_ptr[0] = (u_char)(pes_size >> 8);
		state->cur_pes_size_ptr[1] = (u_char)pes_size;

		state->pes_bytes_written = 0;
	}

	if (state->cur_packet_start == state->temp_packet && state->interleave_frames) {
		// allocate a packet from the queue
		state->last_queue_offset = state->queue->cur_offset;

		cur_packet = write_buffer_queue_get_buffer(state->queue, MPEGTS_PACKET_SIZE, state);
		if (cur_packet == NULL) {
			vod_log_debug0(
				VOD_LOG_DEBUG_LEVEL,
				state->request_context->log,
				0,
				"mpegts_encoder_stuff_cur_packet: write_buffer_queue_get_buffer failed"
			);
			return VOD_ALLOC_FAILED;
		}

		state->cur_packet_start = NULL;

		// copy the temp packet and stuff it
		if (stuff_size > 0) {
			mpegts_copy_and_stuff(cur_packet, state->temp_packet, state->cur_pos, stuff_size);
		} else {
			vod_memcpy(cur_packet, state->temp_packet, MPEGTS_PACKET_SIZE);
		}
	} else {
		// stuff the current packet in place
		if (stuff_size > 0) {
			mpegts_add_stuffing(state->cur_packet_start, state->cur_pos, stuff_size);
		}
	}

	state->cur_pos = state->cur_packet_end;
	state->send_queue_offset = VOD_MAX_OFF_T_VALUE;

	return VOD_OK;
}

static vod_status_t
mpegts_encoder_start_frame(media_filter_context_t* context, output_frame_t* frame) {
	mpegts_encoder_state_t* state = get_context(context);
	mpegts_encoder_state_t* last_writer_state;
	vod_status_t rc;
	size_t pes_header_size;
	u_char* excess_pos;
	u_char* pes_packet_start;
	u_char* pes_start;
	u_char* p;
	size_t excess_size;
	bool_t write_direct;

	last_writer_state = state->queue->last_writer_context;
	if (!state->interleave_frames && last_writer_state != state && last_writer_state != NULL) {
		// frame interleaving is disabled and the last packet that was written belongs to a different stream, close it
		rc = mpegts_encoder_stuff_cur_packet(last_writer_state);
		if (rc != VOD_OK) {
			return rc;
		}
	}

	state->flushed_frame_bytes = 0;
	state->header_size = frame->header_size;

	state->send_queue_offset = state->last_queue_offset;

	pes_header_size = mpegts_get_pes_header_size(&state->stream_info);

	if (state->cur_pos >= state->cur_packet_end) {
		// current packet is full, start a new packet
		write_direct = pes_header_size + frame->size >= MPEGTS_PACKET_USABLE_SIZE;

		rc = mpegts_encoder_init_packet(state, write_direct);
		if (rc != VOD_OK) {
			return rc;
		}

		state->cur_pos = mpegts_write_pes_header(
			state->cur_packet_start, &state->stream_info, frame, &state->cur_pes_size_ptr, TRUE
		);

		state->packet_bytes_left = state->cur_packet_end - state->cur_pos;

		return VOD_OK;
	}

	if (state->last_frame_pts != NO_TIMESTAMP) {
		frame->pts = state->last_frame_pts;
	}

	if (state->cur_pos + pes_header_size < state->cur_packet_end) {
		// current packet has enough room to push the pes without getting full
		pes_packet_start = state->cur_packet_start;
		pes_start = pes_packet_start + SIZEOF_MPEGTS_HEADER;

		vod_memmove(pes_start + pes_header_size, pes_start, state->cur_pos - pes_start);

		state->cur_pos += pes_header_size;

		// write the PES
		mpegts_write_pes_header(
			pes_packet_start, &state->stream_info, frame, &state->cur_pes_size_ptr, FALSE
		);

		state->packet_bytes_left = state->cur_packet_end - state->cur_pos;

		return VOD_OK;
	}

	// find the excess that has to be pushed to a new packet
	excess_size = state->cur_pos + pes_header_size - state->cur_packet_end;
	excess_pos = state->cur_pos - excess_size;

	if (state->cur_packet_start == state->temp_packet && state->interleave_frames) {
		// allocate packet from the queue
		state->last_queue_offset = state->queue->cur_offset;

		pes_packet_start = write_buffer_queue_get_buffer(state->queue, MPEGTS_PACKET_SIZE, state);
		if (pes_packet_start == NULL) {
			vod_log_debug0(
				VOD_LOG_DEBUG_LEVEL,
				state->request_context->log,
				0,
				"mpegts_encoder_start_frame: write_buffer_queue_get_buffer failed"
			);
			return VOD_ALLOC_FAILED;
		}

		// copy the packet and push in the pes
		vod_memcpy(pes_packet_start, state->temp_packet, SIZEOF_MPEGTS_HEADER);

		p = mpegts_write_pes_header(
			pes_packet_start, &state->stream_info, frame, &state->cur_pes_size_ptr, FALSE
		);

		vod_memcpy(
			p, state->temp_packet + SIZEOF_MPEGTS_HEADER, MPEGTS_PACKET_USABLE_SIZE - pes_header_size
		);

		pes_packet_start = NULL;
	} else {
		pes_packet_start = state->cur_packet_start;
	}

	if (excess_size > 0) {
		// copy the excess to a new packet
		write_direct = excess_size + frame->size >= MPEGTS_PACKET_USABLE_SIZE;

		rc = mpegts_encoder_init_packet(state, write_direct);
		if (rc != VOD_OK) {
			return rc;
		}

		vod_memmove(state->cur_pos, excess_pos, excess_size);
		state->cur_pos += excess_size;

		state->packet_bytes_left = state->cur_packet_end - state->cur_pos;
	} else {
		state->cur_pos = state->cur_packet_end;
		state->cur_packet_start = NULL;

		state->packet_bytes_left = MPEGTS_PACKET_USABLE_SIZE;
	}

	if (pes_packet_start != NULL) {
		// make room for the PES
		pes_start = pes_packet_start + SIZEOF_MPEGTS_HEADER;

		vod_memmove(pes_start + pes_header_size, pes_start, MPEGTS_PACKET_USABLE_SIZE - pes_header_size);

		// write the PES
		mpegts_write_pes_header(
			pes_packet_start, &state->stream_info, frame, &state->cur_pes_size_ptr, FALSE
		);
	}

	return VOD_OK;
}

static vod_status_t
mpegts_encoder_write(media_filter_context_t* context, const u_char* buffer, uint32_t size) {
	mpegts_encoder_state_t* state = get_context(context);
	uint32_t packet_used_size;
	uint32_t cur_size;
	uint32_t initial_size;
	u_char* cur_packet;
	vod_status_t rc;
	bool_t write_direct;

	state->pes_bytes_written += size;

	// make sure we have a packet
	if (state->cur_pos >= state->cur_packet_end) {
		write_direct = size >= MPEGTS_PACKET_USABLE_SIZE;

		rc = mpegts_encoder_init_packet(state, write_direct);
		if (rc != VOD_OK) {
			return rc;
		}
	}

	// if current packet has enough room for the whole buffer, just add it
	if (state->cur_pos + size < state->cur_packet_end) {
		state->cur_pos = vod_copy(state->cur_pos, buffer, size);
		return VOD_OK;
	}

	// fill the current packet
	cur_size = state->cur_packet_end - state->cur_pos;

	if (state->cur_packet_start == state->temp_packet && state->interleave_frames) {
		// flush the temp packet
		state->last_queue_offset = state->queue->cur_offset;

		cur_packet = write_buffer_queue_get_buffer(state->queue, MPEGTS_PACKET_SIZE, state);
		if (cur_packet == NULL) {
			vod_log_debug0(
				VOD_LOG_DEBUG_LEVEL,
				state->request_context->log,
				0,
				"mpegts_encoder_write: write_buffer_queue_get_buffer failed"
			);
			return VOD_ALLOC_FAILED;
		}

		state->cur_packet_start = NULL;

		// update the PES size ptr if needed
		if (state->cur_pes_size_ptr >= state->temp_packet
		    && state->cur_pes_size_ptr < state->temp_packet + MPEGTS_PACKET_SIZE) {
			state->cur_pes_size_ptr = cur_packet + (state->cur_pes_size_ptr - state->temp_packet);
		}

		// write the packet
		packet_used_size = state->cur_pos - state->temp_packet;
		vod_memcpy(cur_packet, state->temp_packet, packet_used_size);
		vod_memcpy(cur_packet + packet_used_size, buffer, cur_size);
	} else {
		vod_memcpy(state->cur_pos, buffer, cur_size);
	}

	state->flushed_frame_bytes += state->packet_bytes_left;
	state->packet_bytes_left = MPEGTS_PACKET_USABLE_SIZE;

	buffer += cur_size;
	size -= cur_size;

	// write full packets
	initial_size = size;

	while (size >= MPEGTS_PACKET_USABLE_SIZE) {
		rc = mpegts_encoder_init_packet(state, TRUE);
		if (rc != VOD_OK) {
			return rc;
		}

		vod_memcpy(state->cur_pos, buffer, MPEGTS_PACKET_USABLE_SIZE);
		buffer += MPEGTS_PACKET_USABLE_SIZE;
		size -= MPEGTS_PACKET_USABLE_SIZE;
	}

	state->flushed_frame_bytes += initial_size - size;

	// write any residue
	if (size > 0) {
		rc = mpegts_encoder_init_packet(state, FALSE);
		if (rc != VOD_OK) {
			return rc;
		}

		state->cur_pos = vod_copy(state->cur_pos, buffer, size);
	} else {
		state->cur_pos = state->cur_packet_end;
	}

	return VOD_OK;
}

static vod_status_t
mpegts_append_null_packet(mpegts_encoder_state_t* state) {
	u_char* packet;
	vod_status_t rc;

	rc = mpegts_encoder_init_packet(state, TRUE);
	if (rc != VOD_OK) {
		return rc;
	}

	packet = state->cur_packet_start;
	packet[3] |= 0x20;
	packet[4] = (u_char)MPEGTS_PACKET_USABLE_SIZE - 1;
	packet[5] = 0;
	vod_memset(&packet[6], 0xFF, MPEGTS_PACKET_USABLE_SIZE - 2);
	return VOD_OK;
}

static vod_status_t
mpegts_encoder_flush_frame(media_filter_context_t* context, bool_t last_stream_frame) {
	mpegts_encoder_state_t* state = get_context(context);
	unsigned pes_size;
	vod_status_t rc;
	bool_t stuff_packet;

	stuff_packet = state->align_frames
	            || state->cur_pos >= state->cur_packet_end
	            || state->flushed_frame_bytes < state->header_size
	            || last_stream_frame;

	// update the size in the PES header
	if (state->stream_info.media_type == MEDIA_TYPE_VIDEO && !state->align_frames) {
		pes_size = 0;
	} else {
		pes_size = SIZEOF_PES_OPTIONAL_HEADER + SIZEOF_PES_PTS + state->pes_bytes_written;
		if (state->stream_info.media_type == MEDIA_TYPE_VIDEO) {
			pes_size += SIZEOF_PES_PTS; // dts
		}

		if (pes_size > 0xFFFF) {
			pes_size = 0;
		}

		if (!stuff_packet) {
			// the last ts packet was not closed, its size should be counted in the next PES packet
			state->pes_bytes_written = state->cur_pos - state->cur_packet_start - SIZEOF_MPEGTS_HEADER;
			pes_size -= state->pes_bytes_written;
		} else {
			state->pes_bytes_written = 0;
		}
	}

	state->cur_pes_size_ptr[0] = (u_char)(pes_size >> 8);
	state->cur_pes_size_ptr[1] = (u_char)pes_size;

	// stuff the packet if needed and update the send offset
	if (stuff_packet) {
		rc = mpegts_encoder_stuff_cur_packet(state);
		if (rc != VOD_OK) {
			return rc;
		}
	}

	// on the last frame, add null packets to set the continuity counters
	if (last_stream_frame
	    && state->stream_info.media_type != MEDIA_TYPE_NONE) // don't output null packets in id3
	{
		while (state->cc & 0x0F) {
			rc = mpegts_append_null_packet(state);
			if (rc != VOD_OK) {
				return rc;
			}
		}

		state->cur_pos = state->cur_packet_end;
	}

	return VOD_OK;
}

vod_status_t
mpegts_encoder_start_sub_frame(media_filter_context_t* context, output_frame_t* frame) {
	mpegts_encoder_state_t* state = get_context(context);
	vod_status_t rc;
	bool_t write_direct;

	if (state->cur_pos >= state->cur_packet_end) {
		write_direct = frame->size >= MPEGTS_PACKET_USABLE_SIZE;

		rc = mpegts_encoder_init_packet(state, write_direct);
		if (rc != VOD_OK) {
			return rc;
		}

		state->last_frame_pts = frame->pts;

		return VOD_OK;
	}

	if (state->last_frame_pts == NO_TIMESTAMP) {
		state->last_frame_pts = frame->pts;
	}

	return VOD_OK;
}

void
mpegts_encoder_simulated_start_segment(write_buffer_queue_t* queue) {
	queue->cur_offset = 2 * MPEGTS_PACKET_SIZE; // PAT & PMT
	queue->last_writer_context = NULL;
}

static void
mpegts_encoder_simulated_stuff_cur_packet(mpegts_encoder_state_t* state) {
	write_buffer_queue_t* queue = state->queue;

	if (state->cur_frame_start_pos == -1) {
		state->cur_frame_start_pos = queue->cur_offset;
	}

	if (state->temp_packet_size > 0) {
		queue->cur_offset += MPEGTS_PACKET_SIZE;
		queue->last_writer_context = state;
		state->cc++;
		state->temp_packet_size = 0;
	}

	if (state->last_frame_end_pos == -1) {
		state->last_frame_end_pos = queue->cur_offset;
	}
	state->cur_frame_end_pos = queue->cur_offset;
}

static void
mpegts_encoder_simulated_start_frame(media_filter_context_t* context, output_frame_t* frame) {
	mpegts_encoder_state_t* state = get_context(context);
	write_buffer_queue_t* queue = state->queue;
	mpegts_encoder_state_t* last_writer_state = queue->last_writer_context;

	state->last_frame_start_pos = state->cur_frame_start_pos;
	state->last_frame_end_pos = state->cur_frame_end_pos;
	state->cur_frame_start_pos = -1;
	state->cur_frame_end_pos = -1;

	if (!state->interleave_frames
	    && last_writer_state != state
	    && last_writer_state != NULL
	    && last_writer_state->temp_packet_size > 0) {
		mpegts_encoder_simulated_stuff_cur_packet(last_writer_state);
	}

	state->flushed_frame_bytes = 0;
	state->header_size = frame->header_size;

	state->temp_packet_size += mpegts_get_pes_header_size(&state->stream_info);

	if (state->temp_packet_size >= MPEGTS_PACKET_USABLE_SIZE) {
		state->cur_frame_start_pos = queue->cur_offset;

		queue->cur_offset += MPEGTS_PACKET_SIZE;
		queue->last_writer_context = state;
		state->cc++;
		state->temp_packet_size -= MPEGTS_PACKET_USABLE_SIZE;

		if (state->temp_packet_size == 0) {
			state->last_frame_end_pos = queue->cur_offset;
		}
	}

	state->packet_bytes_left = MPEGTS_PACKET_USABLE_SIZE - state->temp_packet_size;
}

static void
mpegts_encoder_simulated_write(media_filter_context_t* context, uint32_t size) {
	mpegts_encoder_state_t* state = get_context(context);
	write_buffer_queue_t* queue;
	uint32_t packet_count;

	size += state->temp_packet_size;

	packet_count = size / MPEGTS_PACKET_USABLE_SIZE;
	state->temp_packet_size = size - packet_count * MPEGTS_PACKET_USABLE_SIZE;

	if (packet_count <= 0) {
		return;
	}

	state->flushed_frame_bytes +=
		state->packet_bytes_left + (packet_count - 1) * MPEGTS_PACKET_USABLE_SIZE;
	state->packet_bytes_left = MPEGTS_PACKET_USABLE_SIZE;

	queue = state->queue;

	if (state->cur_frame_start_pos == -1) {
		state->cur_frame_start_pos = queue->cur_offset;
	}

	if (state->last_frame_end_pos == -1) {
		state->last_frame_end_pos = queue->cur_offset + MPEGTS_PACKET_SIZE;
	}

	queue->cur_offset += packet_count * MPEGTS_PACKET_SIZE;
	queue->last_writer_context = state;
	state->cc += packet_count;
}

static void
mpegts_encoder_simulated_flush_frame(media_filter_context_t* context, bool_t last_stream_frame) {
	mpegts_encoder_state_t* state = get_context(context);
	write_buffer_queue_t* queue = state->queue;

	if (state->align_frames
	    || state->temp_packet_size == 0
	    || state->flushed_frame_bytes < state->header_size
	    || last_stream_frame) {
		mpegts_encoder_simulated_stuff_cur_packet(state);
	}

	// on the last frame, add null packets to set the continuity counters
	if (last_stream_frame) {
		if ((state->cc & 0x0F) != 0
		    && state->stream_info.media_type != MEDIA_TYPE_NONE) { // don't output null packets in id3
			queue->cur_offset += (0x10 - (state->cc & 0x0F)) * MPEGTS_PACKET_SIZE;
			queue->last_writer_context = state;
		}
		state->cc = state->initial_cc;
	}
}

static const media_filter_t mpegts_encoder = {
	mpegts_encoder_start_frame,
	mpegts_encoder_write,
	mpegts_encoder_flush_frame,
	mpegts_encoder_simulated_start_frame,
	mpegts_encoder_simulated_write,
	mpegts_encoder_simulated_flush_frame,
};

vod_status_t
mpegts_encoder_init(
	media_filter_t* filter,
	mpegts_encoder_state_t* state,
	mpegts_encoder_init_streams_state_t* stream_state,
	media_track_t* track,
	write_buffer_queue_t* queue,
	bool_t interleave_frames,
	bool_t align_frames
) {
	request_context_t* request_context = stream_state->request_context;
	vod_status_t rc;

	vod_memzero(state, sizeof(*state));
	state->request_context = request_context;
	state->queue = queue;
	state->interleave_frames = interleave_frames;
	state->align_frames = align_frames;

	if (track != NULL) {
		state->stream_info.media_type = track->media_info.media_type;
	} else {
		// id3 track
		state->stream_info.media_type = MEDIA_TYPE_NONE;
		state->initial_cc = state->cc = stream_state->segment_index & 0x0F;
	}

	rc = mpegts_encoder_add_stream(stream_state, track, &state->stream_info);
	if (rc != VOD_OK) {
		return rc;
	}

	*filter = mpegts_encoder;

	if (request_context->simulation_only || !interleave_frames) {
		return VOD_OK;
	}

	state->temp_packet = vod_alloc(request_context->pool, MPEGTS_PACKET_SIZE);
	if (state->temp_packet == NULL) {
		vod_log_debug0(
			VOD_LOG_DEBUG_LEVEL, request_context->log, 0, "mpegts_encoder_init: vod_alloc failed"
		);
		return VOD_ALLOC_FAILED;
	}

	return VOD_OK;
}
