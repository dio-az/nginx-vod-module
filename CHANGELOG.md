# Change Log

All notable changes to this project will be documented in this file.

## [1.7.0](https://github.com/dio-az/nginx-vod-module/compare/v1.6.0...v1.7.0) (2026-03-25)

### Features

- Improve atom write readability ([#77](https://github.com/dio-az/nginx-vod-module/pull/77))

### Bug Fixes

- Fix bandwidth reporting in HLS/DASH ([#91](https://github.com/dio-az/nginx-vod-module/pull/91))

## [1.6.0](https://github.com/dio-az/nginx-vod-module/compare/v1.5.4...v1.6.0) (2025-10-29)

### Features

- Build against multiple FFmpeg versions ([#71](https://github.com/dio-az/nginx-vod-module/pull/71))

## [1.5.4](https://github.com/dio-az/nginx-vod-module/compare/v1.5.3...v1.5.4) (2025-09-16)

### Bug Fixes

- Ensure that DASH audio channels are correct ([#66](https://github.com/dio-az/nginx-vod-module/pull/66))
- Fix prometheus response `content-type` ([#65](https://github.com/dio-az/nginx-vod-module/pull/65))

## [1.5.3](https://github.com/dio-az/nginx-vod-module/compare/v1.5.2...v1.5.3) (2025-08-28)

### Bug Fixes

- Fix DASH representation ID for single sequence, again ([#60](https://github.com/dio-az/nginx-vod-module/pull/60))

## [1.5.2](https://github.com/dio-az/nginx-vod-module/compare/v1.5.1...v1.5.2) (2025-08-10)

### Bug Fixes

- Ensure C23 standard compatibility ([#53](https://github.com/dio-az/nginx-vod-module/pull/53))

## [1.5.1](https://github.com/dio-az/nginx-vod-module/compare/v1.5.0...v1.5.1) (2025-07-31)

### Bug Fixes

- Avoid unnecessary heap alloc in HLS ([#49](https://github.com/dio-az/nginx-vod-module/pull/49))

## [1.5.0](https://github.com/dio-az/nginx-vod-module/compare/v1.4.0...v1.5.0) (2025-07-17)

### Features

- Add HLS session key support ([#46](https://github.com/dio-az/nginx-vod-module/pull/46))

### Bug Fixes

- Remove `libxml2` deprecation error ([#45](https://github.com/dio-az/nginx-vod-module/pull/45))

## [1.4.0](https://github.com/dio-az/nginx-vod-module/compare/v1.3.0...v1.4.0) (2025-07-08)

### Features

- Add multi-DRM support for HLS SAMPLE-AES-CTR ([#42](https://github.com/dio-az/nginx-vod-module/pull/42))

## [1.3.0](https://github.com/dio-az/nginx-vod-module/compare/v1.2.0...v1.3.0) (2025-06-29)

### Features

- Improve encrypted DASH packager constants ([#41](https://github.com/dio-az/nginx-vod-module/pull/41))
- Extend video range SDR support ([#39](https://github.com/dio-az/nginx-vod-module/pull/39))
- Add video range HLG support ([#38](https://github.com/dio-az/nginx-vod-module/pull/38))
- Improve DASH packager constants ([#37](https://github.com/dio-az/nginx-vod-module/pull/37))

## [1.2.0](https://github.com/dio-az/nginx-vod-module/compare/v1.1.0...v1.2.0) (2025-06-10)

### Features

- Allow HLS version tuning ([#34](https://github.com/dio-az/nginx-vod-module/pull/34))
- Improve HLS builder memory usage and constants ([#33](https://github.com/dio-az/nginx-vod-module/pull/33))
- Set HLS version based on feature ([#32](https://github.com/dio-az/nginx-vod-module/pull/32))

### Bug Fixes

- Fix regression in DRM-enabled HLS builder ([#36](https://github.com/dio-az/nginx-vod-module/pull/36))
- Fix HLS version merge ([#35](https://github.com/dio-az/nginx-vod-module/pull/35))

## [1.1.0](https://github.com/dio-az/nginx-vod-module/compare/v1.0.0...v1.1.0) (2025-05-16)

### Features

- Update minimum HLS version ([#26](https://github.com/dio-az/nginx-vod-module/pull/26))
- Add HLS independent segments tag ([#25](https://github.com/dio-az/nginx-vod-module/pull/25))

### Bug Fixes

- Ensure that DASH request version is correct ([#28](https://github.com/dio-az/nginx-vod-module/pull/28))

## [1.0.0](https://github.com/dio-az/nginx-vod-module/compare/26f06877b0f2a2336e59cda93a3de18d7b23a3e2...v1.0.0) (2025-05-06)

### ⚠ BREAKING CHANGES

- Drop support for HDS and MSS ([#13](https://github.com/dio-az/nginx-vod-module/pull/13))
- Improve compliance with DASH specification ([#11](https://github.com/dio-az/nginx-vod-module/pull/11))
- Use last audio track assuming higher bitrate ([#9](https://github.com/dio-az/nginx-vod-module/pull/9))

### Features

- Add quick development setup ([#20](https://github.com/dio-az/nginx-vod-module/pull/20))
- Support sequence ID in the representation ID ([#17](https://github.com/dio-az/nginx-vod-module/pull/17))
- Add HLS characteristics support ([#6](https://github.com/dio-az/nginx-vod-module/pull/6))
- Add DASH role scheme support ([#5](https://github.com/dio-az/nginx-vod-module/pull/5))
- Update CI dependencies ([#4](https://github.com/dio-az/nginx-vod-module/pull/4))

### Bug Fixes

- Fix DASH representation ID for single sequence ([#23](https://github.com/dio-az/nginx-vod-module/pull/23))
- Ensure that clock_gettime check works ([#22](https://github.com/dio-az/nginx-vod-module/pull/22))
- Ensure that lang and label are not empty ([#12](https://github.com/dio-az/nginx-vod-module/pull/12))
- Ensure that track grouping is correct ([#10](https://github.com/dio-az/nginx-vod-module/pull/10))
- Ensure that each audio group has a default ([#8](https://github.com/dio-az/nginx-vod-module/pull/8))
- Ensure that sequence autoselect is enabled by default ([#7](https://github.com/dio-az/nginx-vod-module/pull/7))
- Ensure that media info inherits the sequence tags ([#1](https://github.com/dio-az/nginx-vod-module/pull/1))

> For previous versions please check at
[`kaltura/nginx-vod-module/CHANGELOG.md`](https://github.com/kaltura/nginx-vod-module/blob/26f06877b0f2a2336e59cda93a3de18d7b23a3e2/CHANGELOG.md).
