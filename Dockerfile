# syntax=docker/dockerfile:1

FROM alpine:3.24 AS build

ARG FFMPEG_VERSION=9.0
ARG NGINX_VERSION=1.31

RUN apk --no-cache add \
		build-base \
		linux-headers \
		zlib-dev \
		pcre2-dev \
		libxml2-dev \
		openssl-dev \
		nasm \
		fdk-aac-dev

COPY scripts/fetch_latest.sh scripts/build_ffmpeg.sh /usr/local/bin/

RUN fetch_latest.sh ffmpeg.org/releases ffmpeg-${FFMPEG_VERSION} \
	&& fetch_latest.sh nginx.org/download nginx-${NGINX_VERSION}

WORKDIR /ffmpeg

RUN build_ffmpeg.sh --prefix=/opt/ffmpeg && make install

COPY --exclude=sample --exclude=static . /nginx-vod-module

WORKDIR /nginx

ARG TARGETARCH
ARG RELEASE
ARG CC_OPT

RUN case $TARGETARCH in amd64) : ${CC_OPT:=-mpopcnt}; esac \
	&& /nginx-vod-module/scripts/build_basic.sh \
		$([ -n "$RELEASE" ] || echo --with-debug) \
		--with-compat \
		--with-http_v2_module \
		--with-http_v3_module \
		--with-http_realip_module \
		--with-http_stub_status_module \
		--add-module=/nginx-vod-module \
		--with-cc-opt="-O0 ${RELEASE:+-O3} -fstack-clash-protection -Wformat -Werror=format-security $CC_OPT -I/opt/ffmpeg/include" \
		--with-ld-opt="-L/opt/ffmpeg/lib -Wl,-rpath,/opt/ffmpeg/lib,--as-needed ${RELEASE:+-s}" \
	&& make install

FROM alpine:3.24 AS runtime

LABEL maintainer="Diogo Azevedo <hi@dio-az.dev>"

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

COPY --from=build /opt/ffmpeg/lib /opt/ffmpeg/lib
COPY --from=build /opt/nginx /opt/nginx
COPY --from=build --parents /nginx/LICENSE /ffmpeg/COPYING.LGPLv2.1 /nginx-vod-module/LICENSE /usr/share/licenses/

STOPSIGNAL SIGQUIT

ENTRYPOINT ["/opt/nginx/sbin/nginx"]

CMD ["-g", "daemon off;"]

FROM runtime AS quickstart

COPY static/* /opt/nginx/html/
COPY sample/* /opt/nginx/conf/

EXPOSE 8000
