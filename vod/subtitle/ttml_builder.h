#ifndef __TTML_BUILDER_H__
#define __TTML_BUILDER_H__

// includes
#include "../media_set.h"

// constants
#define TTML_TIMESCALE (1000)

// functions
size_t ttml_builder_get_max_size(media_set_t* media_set);

u_char* ttml_builder_write(media_set_t* media_set, u_char* p);

#endif //__TTML_BUILDER_H__
