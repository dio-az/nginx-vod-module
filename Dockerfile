# syntax=docker/dockerfile:1

FROM alpine:3.23 AS build

ARG FFMPEG_VERSION=8.1
ARG NGINX_VERSION=1.30

RUN apk --no-cache add \
		build-base \
		linux-headers \
		zlib-dev \
		pcre2-dev \
		libxml2-dev \
		nasm \
		fdk-aac-dev \
		openssl-dev

COPY scripts/fetch_latest.sh /usr/local/bin/fetch_latest

RUN fetch_latest https://ffmpeg.org/releases ffmpeg-${FFMPEG_VERSION} ffmpeg \
	&& fetch_latest https://nginx.org/download nginx-${NGINX_VERSION} nginx

WORKDIR /ffmpeg

RUN ./configure \
		--prefix=/opt/ffmpeg \
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
		--enable-filters \
		--enable-decoder=h264 \
		--enable-decoder=hevc \
		--enable-decoder=vp8 \
		--enable-decoder=vp9 \
		--enable-decoder=av1 \
		--enable-decoder=aac --enable-encoder=libfdk_aac \
		--enable-encoder=mjpeg \
	&& make -j$(nproc) \
	&& make install

COPY --exclude=sample --exclude=static . /nginx-vod-module

WORKDIR /nginx

RUN /nginx-vod-module/scripts/build_basic.sh \
		--without-http_charset_module \
		--without-http_ssi_module \
		--without-http_userid_module \
		--without-http_auth_basic_module \
		--without-http_mirror_module \
		--without-http_autoindex_module \
		--without-http_geo_module \
		--without-http_split_clients_module \
		--without-http_referer_module \
		--without-http_fastcgi_module \
		--without-http_uwsgi_module \
		--without-http_scgi_module \
		--without-http_grpc_module \
		--without-http_memcached_module \
		--without-http_limit_conn_module \
		--without-http_limit_req_module \
		--without-http_empty_gif_module \
		--without-http_browser_module \
		--without-http_upstream_hash_module \
		--without-http_upstream_ip_hash_module \
		--without-http_upstream_random_module \
		--with-debug \
		--with-http_stub_status_module \
		--add-module=/nginx-vod-module \
		--with-cc-opt='-O0 -I/opt/ffmpeg/include' \
		--with-ld-opt='-L/opt/ffmpeg/lib -Wl,-rpath,/opt/ffmpeg/lib' \
	&& make install

FROM alpine:3.23

LABEL maintainer="Diogo Azevedo <diogoazevedos@gmail.com>"

RUN apk --no-cache add \
		zlib \
		pcre2 \
		libxml2 \
		fdk-aac \
		openssl \
		ca-certificates \
	&& mkdir /var/spool/nginx

COPY --from=build /opt/ffmpeg/lib /opt/ffmpeg/lib
COPY --from=build /opt/nginx /opt/nginx
COPY static/* /opt/nginx/html/
COPY sample/* /opt/nginx/conf/

EXPOSE 8000

ENTRYPOINT ["/opt/nginx/sbin/nginx"]

CMD ["-g", "daemon off;"]
