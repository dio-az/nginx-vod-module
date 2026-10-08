#!/bin/bash

if [ -z "$NGINX_SOURCE_DIR" ]; then
	echo "NGINX_SOURCE_DIR not set"
	exit 1
fi

if [ -z "$NGINX_VOD_MODULE_SOURCE_DIR" ]; then
	echo "NGINX_VOD_MODULE_SOURCE_DIR not set"
	exit 1
fi

cc -Wall -g -oparse_utils_test \
	$NGINX_VOD_MODULE_SOURCE_DIR/vod/parse_utils.c \
	$NGINX_VOD_MODULE_SOURCE_DIR/test/parse_utils/main.c \
	$NGINX_VOD_MODULE_SOURCE_DIR/test/bare_stubs.c \
	$NGINX_SOURCE_DIR/src/core/ngx_string.c \
	$NGINX_SOURCE_DIR/src/core/ngx_hash.c \
	$NGINX_SOURCE_DIR/src/core/ngx_palloc.c \
	$NGINX_SOURCE_DIR/src/os/unix/ngx_alloc.c \
	-I $NGINX_SOURCE_DIR/src/core \
	-I $NGINX_SOURCE_DIR/src/event \
	-I $NGINX_SOURCE_DIR/src/event/modules \
	-I $NGINX_SOURCE_DIR/src/os/unix \
	-I $NGINX_SOURCE_DIR/objs \
	-I $NGINX_VOD_MODULE_SOURCE_DIR
