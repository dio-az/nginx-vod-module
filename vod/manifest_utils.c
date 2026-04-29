#include "manifest_utils.h"

// internal flags
#define ADAPTATION_SETS_FLAG_MULTI_AUDIO (0x1000)

// typedefs
typedef struct {
	uint32_t codec_id;
	media_tags_t tags;
} track_group_key_t;

typedef struct {
	track_group_key_t key;
	media_track_t* head;
	media_track_t* tail;
	uint32_t count;
	vod_queue_t list_node;
	vod_rbtree_node_t rbtree_node;
} track_group_t;

typedef struct {
	vod_rbtree_t rbtree;
	vod_rbtree_node_t sentinel;
	vod_queue_t list;
	uint32_t count;
} track_groups_t;

////// request params formatting functions

static u_char*
manifest_utils_write_bitmask32(u_char* p, uint32_t bitmask, u_char letter) {
	uint32_t i;

	for (i = 0; i < 32; i++) {
		if ((bitmask & (1 << i)) == 0) {
			continue;
		}

		*p++ = '-';
		*p++ = letter;
		p = vod_sprintf(p, "%uD", i + 1);
	}

	return p;
}

static u_char*
manifest_utils_write_bitmask(
	u_char* p, uint64_t* bitmask, uint64_t* mask_temp, uint32_t max_bits, u_char letter
) {
	int32_t i;

	vod_memcpy(mask_temp, bitmask, sizeof(bitmask[0]) * vod_array_length_for_bits(max_bits));

	for (;;) {
		i = vod_get_lowest_bit_set(mask_temp, max_bits);
		if (i < 0) {
			break;
		}

		*p++ = '-';
		*p++ = letter;
		p = vod_sprintf(p, "%uD", i + 1);

		vod_reset_bit(mask_temp, i);
	}

	return p;
}

static u_char*
manifest_utils_write_track_mask(u_char* p, track_mask_t bitmask, u_char letter) {
	track_mask_t mask_temp;
	return manifest_utils_write_bitmask(p, bitmask, mask_temp, MAX_TRACK_COUNT, letter);
}

static track_mask_t*
manifest_utils_get_tracks_mask(
	uint32_t index,
	sequence_tracks_mask_t* sequence_tracks_mask,
	sequence_tracks_mask_t* sequence_tracks_mask_end,
	track_mask_t* default_tracks_mask
) {
	sequence_tracks_mask_t* sequence_tracks_mask_cur;

	for (sequence_tracks_mask_cur = sequence_tracks_mask; sequence_tracks_mask_cur < sequence_tracks_mask_end; sequence_tracks_mask_cur++) {
		if (sequence_tracks_mask_cur->index == (int32_t)index) {
			return sequence_tracks_mask_cur->tracks_mask;
		}
	}

	return default_tracks_mask;
}

static vod_status_t
manifest_utils_build_request_params_string_per_sequence_tracks(
	request_context_t* request_context,
	uint32_t segment_index,
	uint32_t sequences_mask,
	sequence_tracks_mask_t* sequence_tracks_mask,
	sequence_tracks_mask_t* sequence_tracks_mask_end,
	track_mask_t* default_tracks_mask,
	vod_str_t* result
) {
	track_mask_t* tracks_mask;
	uint32_t i;
	size_t result_size;
	u_char* p;

	result_size = 0;

	// segment index
	if (segment_index != INVALID_SEGMENT_INDEX) {
		result_size += 1 + vod_get_int_print_len(segment_index + 1);
	}

	for (i = 0; i < MAX_SEQUENCES; i++) {
		if ((sequences_mask & (1 << i)) == 0) {
			continue;
		}

		// get tracks mask
		tracks_mask = manifest_utils_get_tracks_mask(
			i, sequence_tracks_mask, sequence_tracks_mask_end, default_tracks_mask
		);

		// sequence
		result_size += sizeof("-f32") - 1;

		// video tracks
		if (vod_track_mask_are_all_bits_set(tracks_mask[MEDIA_TYPE_VIDEO])) {
			result_size += sizeof("-v0") - 1;
		} else {
			result_size += vod_track_mask_get_number_of_set_bits(tracks_mask[MEDIA_TYPE_VIDEO])
			             * (sizeof("-v") - 1 + MAX_TRACK_INDEX_LEN);
		}

		// audio tracks
		if (vod_track_mask_are_all_bits_set(tracks_mask[MEDIA_TYPE_AUDIO])) {
			result_size += sizeof("-a0") - 1;
		} else {
			result_size += vod_track_mask_get_number_of_set_bits(tracks_mask[MEDIA_TYPE_AUDIO])
			             * (sizeof("-a") - 1 + MAX_TRACK_INDEX_LEN);
		}
	}

	p = vod_alloc(request_context->pool, result_size + 1);
	if (p == NULL) {
		vod_log_debug0(
			VOD_LOG_DEBUG_LEVEL,
			request_context->log,
			0,
			"manifest_utils_build_request_params_string_per_sequence_tracks: vod_alloc failed"
		);
		return VOD_ALLOC_FAILED;
	}
	result->data = p;

	// segment index
	if (segment_index != INVALID_SEGMENT_INDEX) {
		p = vod_sprintf(p, "-%uD", segment_index + 1);
	}

	for (i = 0; i < MAX_SEQUENCES; i++) {
		if ((sequences_mask & (1 << i)) == 0) {
			continue;
		}

		// get tracks mask
		tracks_mask = manifest_utils_get_tracks_mask(
			i, sequence_tracks_mask, sequence_tracks_mask_end, default_tracks_mask
		);

		// sequence
		p = vod_sprintf(p, "-f%uD", i + 1);

		// video tracks
		if (vod_track_mask_are_all_bits_set(tracks_mask[MEDIA_TYPE_VIDEO])) {
			p = vod_copy(p, "-v0", sizeof("-v0") - 1);
		} else if (vod_track_mask_is_any_bit_set(tracks_mask[MEDIA_TYPE_VIDEO])) {
			p = manifest_utils_write_track_mask(p, tracks_mask[MEDIA_TYPE_VIDEO], 'v');
		}

		// audio tracks
		if (vod_track_mask_are_all_bits_set(tracks_mask[MEDIA_TYPE_AUDIO])) {
			p = vod_copy(p, "-a0", sizeof("-a0") - 1);
		} else if (vod_track_mask_is_any_bit_set(tracks_mask[MEDIA_TYPE_AUDIO])) {
			p = manifest_utils_write_track_mask(p, tracks_mask[MEDIA_TYPE_AUDIO], 'a');
		}
	}

	result->len = p - result->data;

	if (result->len > result_size) {
		vod_log_error(
			VOD_LOG_ERR,
			request_context->log,
			0,
			"manifest_utils_build_request_params_string_per_sequence_tracks: result length %uz exceeded allocated length %uz",
			result->len,
			result_size
		);
		return VOD_UNEXPECTED;
	}

	return VOD_OK;
}

vod_status_t
manifest_utils_build_request_params_string(
	request_context_t* request_context,
	track_mask_t* has_tracks,
	uint32_t segment_index,
	uint32_t sequences_mask,
	sequence_tracks_mask_t* sequence_tracks_mask,
	sequence_tracks_mask_t* sequence_tracks_mask_end,
	track_mask_t* tracks_mask,
	vod_str_t* result
) {
	u_char* p;
	size_t result_size;

	if (sequence_tracks_mask != NULL) {
		return manifest_utils_build_request_params_string_per_sequence_tracks(
			request_context,
			segment_index,
			sequences_mask,
			sequence_tracks_mask,
			sequence_tracks_mask_end,
			tracks_mask,
			result
		);
	}

	result_size = 0;

	// segment index
	if (segment_index != INVALID_SEGMENT_INDEX) {
		result_size += 1 + vod_get_int_print_len(segment_index + 1);
	}

	// sequence mask
	if (sequences_mask != 0xFFFFFFFF) {
		result_size += vod_get_number_of_set_bits32(sequences_mask) * (sizeof("-f32") - 1);
	}

	// video tracks
	if (vod_track_mask_are_all_bits_set(tracks_mask[MEDIA_TYPE_VIDEO])) {
		result_size += sizeof("-v0") - 1;
	} else {
		result_size += vod_track_mask_get_number_of_set_bits(tracks_mask[MEDIA_TYPE_VIDEO])
		             * ((sizeof("-v") - 1) + MAX_TRACK_INDEX_LEN);
	}

	// audio tracks
	if (vod_track_mask_are_all_bits_set(tracks_mask[MEDIA_TYPE_AUDIO])) {
		result_size += sizeof("-a0") - 1;
	} else {
		result_size += vod_track_mask_get_number_of_set_bits(tracks_mask[MEDIA_TYPE_AUDIO])
		             * ((sizeof("-v") - 1) + MAX_TRACK_INDEX_LEN);
	}

	p = vod_alloc(request_context->pool, result_size + 1);
	if (p == NULL) {
		vod_log_debug0(
			VOD_LOG_DEBUG_LEVEL, request_context->log, 0, "manifest_utils_build_request_params_string: vod_alloc failed"
		);
		return VOD_ALLOC_FAILED;
	}
	result->data = p;

	// segment index
	if (segment_index != INVALID_SEGMENT_INDEX) {
		p = vod_sprintf(p, "-%uD", segment_index + 1);
	}

	// sequence mask
	if (sequences_mask != 0xFFFFFFFF) {
		p = manifest_utils_write_bitmask32(p, sequences_mask, 'f');
	}

	// video tracks
	if (vod_track_mask_is_any_bit_set(has_tracks[MEDIA_TYPE_VIDEO])) {
		if (vod_track_mask_are_all_bits_set(tracks_mask[MEDIA_TYPE_VIDEO])) {
			p = vod_copy(p, "-v0", sizeof("-v0") - 1);
		} else {
			p = manifest_utils_write_track_mask(p, tracks_mask[MEDIA_TYPE_VIDEO], 'v');
		}
	}

	// audio tracks
	if (vod_track_mask_is_any_bit_set(has_tracks[MEDIA_TYPE_AUDIO])) {
		if (vod_track_mask_are_all_bits_set(tracks_mask[MEDIA_TYPE_AUDIO])) {
			p = vod_copy(p, "-a0", sizeof("-a0") - 1);
		} else {
			p = manifest_utils_write_track_mask(p, tracks_mask[MEDIA_TYPE_AUDIO], 'a');
		}
	}

	result->len = p - result->data;

	if (result->len > result_size) {
		vod_log_error(
			VOD_LOG_ERR,
			request_context->log,
			0,
			"manifest_utils_build_request_params_string: result length %uz exceeded allocated length %uz",
			result->len,
			result_size
		);
		return VOD_UNEXPECTED;
	}

	return VOD_OK;
}

static const u_char media_type_letter[] = {'v', 'a'}; // must match MEDIA_TYPE_* order

u_char*
manifest_utils_append_track_spec(
	u_char* p, media_sequence_t* sequence, media_track_t* track, bool_t write_sequence
) {
	if (write_sequence) {
		if (sequence->id.len != 0 && sequence->id.len < VOD_INT32_LEN) {
			p = vod_sprintf(p, "s%V", &sequence->id);
		} else {
			p = vod_sprintf(p, "f%uD", sequence->index + 1);
		}
	}

	if (track->media_info.media_type <= MEDIA_TYPE_AUDIO) {
		if (write_sequence) {
			*p++ = '-';
		}
		*p++ = media_type_letter[track->media_info.media_type];
		p = vod_sprintf(p, "%uD", track->index + 1);
	}

	return p;
}

////// track group functions

static bool_t
track_group_key_init(media_track_t* track, uint32_t flags, track_group_key_t* key) {
	key->codec_id = 0;
	key->tags = track->media_info.tags;

	switch (track->media_info.media_type) {
	case MEDIA_TYPE_VIDEO:
		if ((flags & ADAPTATION_SETS_FLAG_MULTI_VIDEO_CODEC) != 0) {
			key->codec_id = track->media_info.codec_id;
		}
		break;

	case MEDIA_TYPE_AUDIO:
		if ((flags & ADAPTATION_SETS_FLAG_MULTI_AUDIO_CODEC) != 0) {
			key->codec_id = track->media_info.codec_id;
		}

		if ((flags & ADAPTATION_SETS_FLAG_MULTI_AUDIO) == 0) {
			break;
		}

		if (track->media_info.tags.label.len == 0) {
			return FALSE;
		}
		break;

	case MEDIA_TYPE_SUBTITLE:
		if (track->media_info.tags.label.len == 0) {
			return FALSE;
		}
		break;

	default: // MEDIA_TYPE_NONE
		return FALSE;
	}

	return TRUE;
}

static uint32_t
track_group_key_get_hash(track_group_key_t* key) {
	uint32_t hash = key->codec_id
	              + key->tags.is_forced * 31
	              + vod_crc32_short(key->tags.label.data, key->tags.label.len)
	              + vod_crc32_short(key->tags.characteristics.data, key->tags.characteristics.len);

	vod_str_t* role;
	for (uint32_t role_index = 0; role_index < key->tags.roles.nelts; role_index++) {
		role = (vod_str_t*)key->tags.roles.elts + role_index;
		hash += vod_crc32_short(role->data, role->len);
	}

	return hash;
}

static int8_t
track_group_key_compare(track_group_key_t* key1, track_group_key_t* key2) {
	uint32_t role_index;
	vod_str_t* role1;
	vod_str_t* role2;
	int8_t rc;

	if (key1->codec_id != key2->codec_id) {
		return key1->codec_id < key2->codec_id ? -1 : 1;
	}

	if (key1->tags.label.len != key2->tags.label.len) {
		return key1->tags.label.len < key2->tags.label.len ? -1 : 1;
	}

	if (key1->tags.label.data != key2->tags.label.data) {
		rc = vod_memcmp(key1->tags.label.data, key2->tags.label.data, key1->tags.label.len);
		if (rc != 0) {
			return rc;
		}
	}

	if (key1->tags.is_forced != key2->tags.is_forced) {
		return key1->tags.is_forced < key2->tags.is_forced ? -1 : 1;
	}

	if (key1->tags.characteristics.len != key2->tags.characteristics.len) {
		return key1->tags.characteristics.len < key2->tags.characteristics.len ? -1 : 1;
	}

	if (key1->tags.characteristics.data != key2->tags.characteristics.data) {
		rc = vod_memcmp(
			key1->tags.characteristics.data,
			key2->tags.characteristics.data,
			key1->tags.characteristics.len
		);
		if (rc != 0) {
			return rc;
		}
	}

	if (key1->tags.roles.nelts != key2->tags.roles.nelts) {
		return key1->tags.roles.nelts < key2->tags.roles.nelts ? -1 : 1;
	}

	for (role_index = 0; role_index < key1->tags.roles.nelts; role_index++) {
		role1 = (vod_str_t*)key1->tags.roles.elts + role_index;
		role2 = (vod_str_t*)key2->tags.roles.elts + role_index;

		if (role1->len != role2->len) {
			return role1->len < role2->len ? -1 : 1;
		}

		rc = vod_memcmp(role1->data, role2->data, role1->len);
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}

static void
track_group_rbtree_insert_value(vod_rbtree_node_t* temp, vod_rbtree_node_t* node, vod_rbtree_node_t* sentinel) {
	vod_rbtree_node_t** p;
	track_group_t *n, *t;

	for (;;) {
		n = vod_container_of(node, track_group_t, rbtree_node);
		t = vod_container_of(temp, track_group_t, rbtree_node);

		if (node->key != temp->key) {
			p = (node->key < temp->key) ? &temp->left : &temp->right;
		} else {
			p = (track_group_key_compare(&n->key, &t->key) < 0) ? &temp->left : &temp->right;
		}

		if (*p == sentinel) {
			break;
		}

		temp = *p;
	}

	*p = node;
	node->parent = temp;
	node->left = sentinel;
	node->right = sentinel;
	vod_rbt_red(node);
}

static track_group_t*
track_group_rbtree_lookup(vod_rbtree_t* rbtree, track_group_key_t* key, uint32_t hash) {
	vod_rbtree_node_t *node, *sentinel;
	track_group_t* n;
	vod_int_t rc;

	node = rbtree->root;
	sentinel = rbtree->sentinel;

	while (node != sentinel) {
		n = vod_container_of(node, track_group_t, rbtree_node);

		if (hash != node->key) {
			node = (hash < node->key) ? node->left : node->right;
			continue;
		}

		rc = track_group_key_compare(key, &n->key);
		if (rc < 0) {
			node = node->left;
			continue;
		}

		if (rc > 0) {
			node = node->right;
			continue;
		}

		return n;
	}

	return NULL;
}

static vod_status_t
track_group_create(
	request_context_t* request_context,
	track_group_key_t* key,
	uint32_t hash,
	media_track_t* track,
	track_groups_t* groups
) {
	track_group_t* group;

	group = vod_alloc(request_context->pool, sizeof(*group));
	if (group == NULL) {
		vod_log_debug0(
			VOD_LOG_DEBUG_LEVEL, request_context->log, 0, "track_group_create: vod_alloc failed"
		);
		return VOD_ALLOC_FAILED;
	}

	// initialize the group
	group->key = *key;
	group->rbtree_node.key = hash;
	group->count = 1;
	group->head = track;
	group->tail = track;
	track->next = NULL;

	// add to the groups
	vod_queue_insert_tail(&groups->list, &group->list_node);
	vod_rbtree_insert(&groups->rbtree, &group->rbtree_node);
	groups->count++;

	return VOD_OK;
}

static void
track_group_add_track(track_group_t* group, media_track_t* track, uint32_t flags) {
	// check whether multiple tracks are allowed
	switch (track->media_info.media_type) {
	case MEDIA_TYPE_SUBTITLE:
		return;

	case MEDIA_TYPE_AUDIO:
		if (vod_all_flags_set(
				flags, ADAPTATION_SETS_FLAG_MULTI_AUDIO | ADAPTATION_SETS_FLAG_SINGLE_LANG_TRACK
			)) {
			return;
		}
		break;
	}

	// add to existing group
	group->tail->next = track;
	group->tail = track;
	group->count++;
	track->next = NULL;
}

static media_track_t**
track_group_to_adaptation_set(track_group_t* group, media_track_t** cur_track_ptr, adaptation_set_t* result) {
	media_track_t* cur_track;

	result->first = cur_track_ptr;
	result->count = group->count;
	result->type = group->head->media_info.media_type;

	for (cur_track = group->head; cur_track != NULL; cur_track = cur_track->next) {
		*cur_track_ptr++ = cur_track;
	}
	result->last = cur_track_ptr;

	return cur_track_ptr;
}

static void
track_groups_init(track_groups_t* result) {
	track_groups_t* groups;
	uint32_t media_type;

	for (media_type = 0; media_type < MEDIA_TYPE_COUNT; media_type++) {
		groups = &result[media_type];
		vod_rbtree_init(&groups->rbtree, &groups->sentinel, track_group_rbtree_insert_value);
		vod_queue_init(&groups->list);
		groups->count = 0;
	}
}

static vod_status_t
track_groups_from_media_set(
	request_context_t* request_context,
	media_set_t* media_set,
	uint32_t flags,
	uint32_t media_type,
	track_groups_t* result
) {
	media_track_t* last_track;
	media_track_t* cur_track;
	track_groups_t* groups;
	track_group_key_t key;
	track_group_t* group;
	vod_status_t rc;
	uint32_t cur_media_type;
	uint32_t hash;

	// initialize the groups objects
	track_groups_init(result);

	last_track = media_set->filtered_tracks + media_set->total_track_count;
	for (cur_track = media_set->filtered_tracks; cur_track < last_track; cur_track++) {
		// ignore the track if it doesn't match the requested media type (if there is one)
		cur_media_type = cur_track->media_info.media_type;
		if (media_type != MEDIA_TYPE_NONE && cur_media_type != media_type) {
			continue;
		}

		// get the group key
		if (!track_group_key_init(cur_track, flags, &key)) {
			continue;
		}

		hash = track_group_key_get_hash(&key);

		// look up the group
		groups = &result[cur_media_type];

		group = track_group_rbtree_lookup(&groups->rbtree, &key, hash);
		if (group != NULL) {
			track_group_add_track(group, cur_track, flags);
			continue;
		}

		// create a new group
		rc = track_group_create(request_context, &key, hash, cur_track, groups);
		if (rc != VOD_OK) {
			return rc;
		}
	}

	return VOD_OK;
}

static adaptation_set_t*
track_groups_to_adaptation_sets(
	track_groups_t* groups, media_track_t*** cur_track_ptr_arg, adaptation_set_t* cur_adaptation_set
) {
	media_track_t** cur_track_ptr = *cur_track_ptr_arg;
	track_group_t* group;
	vod_queue_t* list = &groups->list;
	vod_queue_t* node;

	for (node = vod_queue_head(list); node != list; node = node->next) {
		group = vod_container_of(node, track_group_t, list_node);

		cur_track_ptr = track_group_to_adaptation_set(group, cur_track_ptr, cur_adaptation_set);

		cur_adaptation_set++;
	}

	*cur_track_ptr_arg = cur_track_ptr;

	return cur_adaptation_set;
}

////// adaptation sets functions

static bool_t
manifest_utils_is_multi_audio(media_set_t* media_set) {
	media_track_t* last_track;
	media_track_t* cur_track;
	vod_str_t* label = NULL;

	last_track = media_set->filtered_tracks + media_set->total_track_count;
	for (cur_track = media_set->filtered_tracks; cur_track < last_track; cur_track++) {
		if (cur_track->media_info.media_type != MEDIA_TYPE_AUDIO
		    || cur_track->media_info.tags.label.len == 0) {
			continue;
		}

		if (label == NULL) {
			label = &cur_track->media_info.tags.label;
		} else if (!vod_str_equals(cur_track->media_info.tags.label, *label)) {
			return TRUE;
		}
	}

	return FALSE;
}

vod_status_t
manifest_utils_get_adaptation_sets(
	request_context_t* request_context, media_set_t* media_set, uint32_t flags, adaptation_sets_t* output
) {
	adaptation_set_t* cur_adaptation_set;
	adaptation_set_t* adaptation_sets;
	track_groups_t groups[MEDIA_TYPE_COUNT];
	media_track_t** cur_track_ptr;
	vod_status_t rc;
	uint32_t media_type;
	size_t adaptation_sets_count;

	// update flags
	if (manifest_utils_is_multi_audio(media_set)) {
		flags |= ADAPTATION_SETS_FLAG_MULTI_AUDIO;
		output->multi_audio = TRUE;
	} else {
		output->multi_audio = FALSE;
	}

	// initialize the track groups
	rc = track_groups_from_media_set(request_context, media_set, flags, MEDIA_TYPE_NONE, groups);
	if (rc != VOD_OK) {
		return rc;
	}

	// get the number of adaptation sets
	output->count[ADAPTATION_TYPE_VIDEO] = groups[MEDIA_TYPE_VIDEO].count;
	output->count[ADAPTATION_TYPE_AUDIO] = groups[MEDIA_TYPE_AUDIO].count;
	output->count[ADAPTATION_TYPE_SUBTITLE] = groups[MEDIA_TYPE_SUBTITLE].count;

	adaptation_sets_count = output->count[ADAPTATION_TYPE_VIDEO]
	                      + output->count[ADAPTATION_TYPE_AUDIO]
	                      + output->count[ADAPTATION_TYPE_SUBTITLE];

	// allocate the adaptation sets and tracks
	adaptation_sets = vod_alloc(
		request_context->pool,
		sizeof(adaptation_sets[0]) * adaptation_sets_count
			+ sizeof(adaptation_sets[0].first[0]) * media_set->total_track_count
	);
	if (adaptation_sets == NULL) {
		vod_log_debug0(
			VOD_LOG_DEBUG_LEVEL, request_context->log, 0, "manifest_utils_get_adaptation_sets: vod_alloc failed"
		);
		return VOD_ALLOC_FAILED;
	}

	cur_track_ptr = (void*)(adaptation_sets + adaptation_sets_count);
	cur_adaptation_set = adaptation_sets;

	// initialize all other adaptation sets
	for (media_type = MEDIA_TYPE_VIDEO; media_type < MEDIA_TYPE_COUNT; media_type++) {
		if (output->count[media_type] <= 0) {
			continue;
		}

		output->first_by_type[media_type] = cur_adaptation_set;

		cur_adaptation_set =
			track_groups_to_adaptation_sets(&groups[media_type], &cur_track_ptr, cur_adaptation_set);
	}

	output->first = adaptation_sets;
	output->last = adaptation_sets + adaptation_sets_count;
	output->total_count = adaptation_sets_count;

	return VOD_OK;
}
