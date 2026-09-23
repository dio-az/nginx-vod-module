#include "mp4_pssh.h"
#include "../udrm.h"
#include "mp4_defs.h"
#include "mp4_init_segment.h"
#include "mp4_write_stream.h"

// constants
const u_char common_system_id[] = {
	0x10, 0x77, 0xEF, 0xEC, 0xC0, 0xB2, 0x4D, 0x02, 0xAC, 0xE3, 0x3C, 0x1E, 0x52, 0xE2, 0xFB, 0x4B
};

const u_char playready_system_id[] = {
	0x9A, 0x04, 0xF0, 0x79, 0x98, 0x40, 0x42, 0x86, 0xAB, 0x92, 0xE6, 0x5B, 0xE0, 0x88, 0x5F, 0x95
};

u_char*
mp4_pssh_write_box(u_char* p, drm_system_info_t* info) {
	bool_t is_pssh_v1 = mp4_pssh_is_common(info); // W3C common PSSH box follows `v1` format
	size_t atom_size = ATOM_HEADER_SIZE + sizeof(pssh_atom_t) + info->data.len;

	if (is_pssh_v1) {
		atom_size -= sizeof(uint32_t);
	}

	write_atom_header(p, atom_size, 'p', 's', 's', 'h');
	write_fullbox_header(p, is_pssh_v1 ? 1 : 0, 0);

	p = vod_copy(p, info->system_id, DRM_SYSTEM_ID_SIZE); // system_ID

	if (!is_pssh_v1) {
		write_be32(p, info->data.len); // data_size
	}

	p = vod_copy(p, info->data.data, info->data.len); // data

	return p;
}

u_char*
mp4_pssh_write_boxes(void* context, u_char* p) {
	drm_system_info_array_t* pssh_array = (drm_system_info_array_t*)context;
	drm_system_info_t* info;

	for (info = pssh_array->first; info < pssh_array->last; info++) {
		p = mp4_pssh_write_box(p, info);
	}

	return p;
}

void
mp4_pssh_init_atom_writer(drm_info_t* drm_info, atom_writer_t* atom_writer) {
	drm_system_info_t* cur_info;

	atom_writer->atom_size = 0;

	for (cur_info = drm_info->pssh_array.first; cur_info < drm_info->pssh_array.last; cur_info++) {
		atom_writer->atom_size += ATOM_HEADER_SIZE + sizeof(pssh_atom_t) + cur_info->data.len;
		if (mp4_pssh_is_common(cur_info)) {
			atom_writer->atom_size -= sizeof(uint32_t);
		}
	}

	atom_writer->write = mp4_pssh_write_boxes;
	atom_writer->context = &drm_info->pssh_array;
}
