# syntax=docker/dockerfile:1

FROM alpine:3.24 AS base

FROM base AS fetch

COPY scripts/fetch_latest.sh /usr/local/bin/

FROM fetch AS ffmpeg

ARG FFMPEG_VERSION=9.0

RUN fetch_latest.sh ffmpeg.org/releases ffmpeg-${FFMPEG_VERSION}

FROM fetch AS nginx

ARG NGINX_VERSION=1.31

RUN fetch_latest.sh nginx.org/download nginx-${NGINX_VERSION}

FROM base AS build

RUN apk --no-cache add \
		build-base \
		linux-headers \
		zlib-dev \
		pcre2-dev \
		libxml2-dev \
		openssl-dev \
		nasm \
		fdk-aac-dev

COPY scripts/build_ffmpeg.sh /usr/local/bin/
COPY --from=ffmpeg /ffmpeg /ffmpeg

WORKDIR /ffmpeg

RUN build_ffmpeg.sh --prefix=/opt/ffmpeg && make install

COPY --from=nginx /nginx /nginx
COPY --exclude=sample --exclude=static . /nginx-vod-module

WORKDIR /nginx

ARG TARGETARCH
ARG RELEASE
ARG CC_OPT

RUN case $TARGETARCH in amd64) : ${CC_OPT:=-mpopcnt}; esac \
	&& /nginx-vod-module/scripts/build_basic.sh \
		$([ -n "$RELEASE" ] || echo --with-debug) \
		--with-compat \
		--with-http_auth_request_module \
		--with-http_realip_module \
		--with-http_secure_link_module \
		--with-http_slice_module \
		--with-http_stub_status_module \
		--with-http_v2_module \
		--with-http_v3_module \
		--add-module=/nginx-vod-module \
		--with-cc-opt="-O0 ${RELEASE:+-O3} -fstack-clash-protection -Wformat -Werror=format-security $CC_OPT -I/opt/ffmpeg/include" \
		--with-ld-opt="-L/opt/ffmpeg/lib -Wl,-rpath,/opt/ffmpeg/lib,--as-needed ${RELEASE:+-s}" \
	&& make install \
	&& rm /opt/nginx/conf/*.default /opt/nginx/conf/koi-* /opt/nginx/conf/win-utf

FROM base AS runtime

RUN apk --no-cache add \
		zlib \
		pcre2 \
		libxml2 \
		libssl3 \
		libcrypto3 \
		fdk-aac \
		fdk-aac-doc \
		ca-certificates \
	&& mkdir /var/spool/nginx

COPY --from=build --exclude=pkgconfig --exclude=*.so /opt/ffmpeg/lib /opt/ffmpeg/lib
COPY --from=build /opt/nginx /opt/nginx
COPY --from=build --parents /nginx/LICENSE /ffmpeg/COPYING.LGPLv2.1 /nginx-vod-module/LICENSE /usr/share/licenses/

STOPSIGNAL SIGQUIT

ENTRYPOINT ["/opt/nginx/sbin/nginx"]

CMD ["-g", "daemon off;"]

FROM runtime AS quickstart

COPY static/* /opt/nginx/html/
COPY sample/* /opt/nginx/conf/

EXPOSE 8000
