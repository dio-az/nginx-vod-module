#!/bin/sh

./configure \
	--disable-programs \
	--disable-doc \
	--disable-static \
	--disable-avdevice \
	--disable-avformat \
	--enable-shared \
	--enable-gpl \
	--enable-nonfree \
	--enable-libfdk-aac \
	--disable-everything \
	--enable-filter=volume,amix,atempo \
	--enable-decoder=h264,hevc,vp8,vp9,av1,aac \
	--enable-encoder=libfdk_aac,mjpeg \
	"$@"

make -j$(nproc)
