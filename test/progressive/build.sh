#!/bin/bash

if [ -z "$NGINX_SOURCE_DIR" ]; then
	echo "NGINX_SOURCE_DIR not set"
	exit 1
fi

if [ -z "$NGINX_VOD_MODULE_SOURCE_DIR" ]; then
	echo "NGINX_VOD_MODULE_SOURCE_DIR not set"
	exit 1
fi

for test in test_stbl_builder test_offsets test_mdat_writer; do
	cc -Wall -g -o${test} -DNGX_HAVE_LIB_AV_CODEC=0 \
		$NGINX_VOD_MODULE_SOURCE_DIR/vod/mp4/mp4_progressive_builder.c \
		$NGINX_VOD_MODULE_SOURCE_DIR/test/progressive/stubs.c \
		$NGINX_VOD_MODULE_SOURCE_DIR/test/progressive/${test}.c \
		-I $NGINX_SOURCE_DIR/src/core \
		-I $NGINX_SOURCE_DIR/src/event \
		-I $NGINX_SOURCE_DIR/src/event/modules \
		-I $NGINX_SOURCE_DIR/src/os/unix \
		-I $NGINX_SOURCE_DIR/objs \
		-I $NGINX_VOD_MODULE_SOURCE_DIR || exit 1
done
