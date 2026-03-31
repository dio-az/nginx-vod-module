# NGINX-based VOD Packager
## nginx-vod-module [![CI](https://github.com/diogoazevedos/nginx-vod-module/actions/workflows/main.yml/badge.svg)](https://github.com/diogoazevedos/nginx-vod-module/actions/workflows/main.yml)

### Features

- On-the-fly repackaging of MP4 files to DASH or HLS.
- Working modes:

  - Local - serve locally accessible files (local disk or mounted NFS).
  - Remote - serve files accessible via HTTP using range requests.
  - Mapped - serve files according to a specification encoded in JSON format. The JSON can pulled
    from a remote server, or read from a local file.

- Adaptive bitrate support.
- Playlist support (playing several different media files one after the other) - `mapped` mode only.
- Simulated live support (generating a live stream from MP4 files) - `mapped` mode only.
- Fallback support for file not found in `local` or `mapped` modes (useful in multi-datacenter
  environments).
- Video codecs: H264, H265, AV1, VP8 (DASH), and VP9 (DASH).
- Audio codecs: AAC, MP3 (HLS), AC-3, E-AC-3, VORBIS (DASH), OPUS (DASH), FLAC (HLS), and DTS (HLS).
- Captions support:

  Input:

  - WebVTT
  - SRT
  - DFXP/TTML (requires `libxml2`)
  - CAP (Cheetah)

  Output:

  - DASH - either sidecar WebVTT or SMPTE-TT segments (configurable).
  - HLS - segmented WebVTT.

- Audio or video only files.
- Alternative audio renditions - supporting both:

  - Generation of manifest with different audio renditions, allowing selection on the client side.
  - Muxing together audio and video streams from separate files / tracks - provides the ability to
    serve different audio renditions of a single video, without the need for any special support on
    the client side.

- Track selection for multi audio/video MP4 files.
- Playback rate change - 0.5x up to 2x (requires `libavcodec` and `libavfilter`).
- Source file clipping (only from I-Frame to P-frame).
- Support for variable segment lengths - enabling the player to select the optimal bitrate fast,
  without the overhead of short segments for the whole duration of the video.
- Clipping of MP4 files for progressive download playback.
- Thumbnail capture (requires `libavcodec`) and resize (requires `libswscale`).
- Volume map (requires `libavcodec`) - returns a CSV containing the volume level in each interval.
- Decryption of CENC-encrypted MP4 files (it is possible to create such files with MP4Box).
- DASH: common encryption (CENC) support.
- HLS: support for AES-128 / SAMPLE-AES encryption.
- HLS: Generation of I-frames playlist (`#EXT-X-I-FRAMES-ONLY`).

### Limitations

- Track selection and playback rate change are not supported in progressive download.
- I-frames playlist generation is not supported when encryption is enabled.

### Compilation

#### Dependencies

In general, if the required dependencies for building NGINX are present, it should be possible to
build `nginx-vod-module`. However, some optional features of the module require additional
dependencies. These are detected during the `configure` step — if any dependency is missing, the
corresponding feature will be disabled.

The optional features are:

- Thumbnail capture and volume map - depends on [`ffmpeg`](https://ffmpeg.org).
- Audio filtering (for changing playback rate / gain) - depends on [`ffmpeg`](https://ffmpeg.org)
  and `libfdk_aac`.
- Encryption / decryption (DRM / HLS AES) - depends on `openssl`.
- DFXP captions - depends on `libxml2`.
- UTF-16 encoded SRT files - depends on `iconv`.

#### Build

To link statically against NGINX, use:

```sh
./configure --add-module=/path/to/nginx-vod-module
make
make install
 ```

To compile as a dynamic module, use:

```sh
./configure --add-dynamic-module=/path/to/nginx-vod-module
```

> [!NOTE]
> The `load_module` directive should be used in `nginx.conf` to load the module.

Optional recommended settings:

- `--with-file-aio` - enable asynchronous I/O support, highly recommended, relevant only to `local`
  and `mapped` modes.
- `--with-threads` - enable asynchronous file open using thread pool (also requires
  `vod_open_file_thread_pool` in `nginx.conf`), relevant only to `local` and `mapped` modes.
- `--with-cc-opt='-O3 -mpopcnt'` - enable additional compiler optimizations (about 8% reduction
  noticed in the MP4 parse time and frame processing time compared to the default `-O`).
  > The `-mpopcnt` is only available on `x86_64` architectures.

Debug settings:

- `--with-debug` - enable debug messages (also requires passing `debug` in the `error_log` directive
  in `nginx.conf`).
- `--with-cc-opt='-O0'` - disable compiler optimizations (for debugging with `gdb`).

C Macro Configurations:

- `--with-cc-opt='-DNGX_VOD_MAX_TRACK_COUNT=256 -mavx2'` - increase the maximum track count
  (preferably to multiples of `64`). It's recommended to enable vector extensions (`AVX2`) as well.

If you wish to make use of the following features:

- Thumbnail capture
- Playback rate change - 0.5x up to 2x

You will also need to install the [`ffmpeg`](https://ffmpeg.org).

#### Quick start

You can build and run a Docker image using the provided `Dockerfile`, which includes a basic
implementation of `nginx-vod-module` with all required dependencies.

```sh
docker image build -t nginx-vod-module .
docker container run --rm -it -p 8000:8000 nginx-vod-module
```

> [!TIP]
> The sample configuration files are available in the [`sample` folder](./sample). You can customize
> the configurations based on your specific requirements.

```sh
npx http-server demo -p 8080 -g --cors
```

Watch the demo content in your browser by visiting
[Tears of Steel demo](https://shaka-player-demo.appspot.com/demo/#assetBase64=eyJtYW5pZmVzdFVyaSI6Imh0dHA6Ly9sb2NhbGhvc3Q6ODAwMC9jbGVhci90ZWFycy1vZi1zdGVlbC8ubTN1OCJ9).

> [!TIP]
> Download the sample media files
> [`tears-of-steel.zip`](https://media-sample-storage.s3.eu-central-1.amazonaws.com/tears-of-steel.zip)
> and extract them at `demo/storage/tears-of-steel`.

### URL structure

#### Basic URL structure

The basic structure of the URL is: `https://<domain>/<location>/<fileuri>/<filename>`

Where:

- `domain` - the domain of the `nginx-vod-module` server.
- `location` - the `location` specified in the `nginx.conf`.
- `fileuri` - a URI to the MP4 file:
  - `local` mode - the full file path is determined according to the root / alias `nginx.conf`
    directives.
  - `mapped` mode - the full file path is determined according to the JSON received from the
    upstream / local file.
  - `remote` mode - the MP4 file is read from upstream in chunks.
- `filename` - detailed below.

> [!NOTE]
> In `mapped` and `remote` modes, the URL of the upstream request is
> `https://<upstream>/<location>/<fileuri>?<extra_args>` (`extra_args` is determined by the
> `vod_upstream_extra_args` parameter).

#### Multi URL structure

Multi URLs are used to encode several URLs on a single URL. A multi URL can be used to specify the
URLs of several different MP4 files that should be included together in a DASH MPD for example.

The structure of a multi URL is:

`http://<domain>/<location>/<prefix>,<middle1>,<middle2>,<middle3>,<postfix>.urlset/<filename>`

The sample URL above represents 3 URLs:

- `http://<domain>/<location>/<prefix><middle1><postfix>/<filename>`
- `http://<domain>/<location>/<prefix><middle2><postfix>/<filename>`
- `http://<domain>/<location>/<prefix><middle3><postfix>/<filename>`

The suffix `.urlset` (can be changed using `vod_multi_uri_suffix`) indicates that the URL should be
treated as a multi URL. For example - the URL
`http://<domain>/hls/big_buck_bunny_,6,9,15,00k.mp4.urlset/master.m3u8` will return a manifest
containing:

- `http://<domain>/hls/big_buck_bunny_600k.mp4/index.m3u8`
- `http://<domain>/hls/big_buck_bunny_900k.mp4/index.m3u8`
- `http://<domain>/hls/big_buck_bunny_1500k.mp4/index.m3u8`

#### URL path parameters

The following parameters are supported on the URL path:

- `clipFrom` - an offset in milliseconds since the beginning of the video, where the generated
  stream should start. For example, `.../clipFrom/10000/...` will generate a stream that starts 10
  seconds into the video.
- `clipTo` - an offset in milliseconds since the beginning of the video, where the generated stream
  should end. For example, `.../clipTo/60000/...` will generate a stream truncated to 60 seconds.
- `tracks` - can be used to select specific audio/video tracks. The structure of the parameter is:
  `v<id1>-v<id2>-a<id1>-a<id2>...` For example, `.../tracks/v1-a1/...` will select the first video
  and audio track. The default is to include all tracks.
- `shift` - can be used to apply a timing shift to one or more streams. The structure of the
  parameter is: `v<vshift>-a<ashift>-s<sshift>` For example, `.../shift/v100/...` will apply a
  forward shift of 100ms to the video timestamps.

#### Filename structure

The structure of filename is:

`<basename>[<seqparams>][<fileparams>][<trackparams>][<langparams>].<extension>`

Where:

- `basename` + `extension` - the set of options is packager specific (the list below applies to the
  default settings):

  - `dash` - `manifest.mpd`.
  - `hls` master playlist - `master.m3u8`.
  - `hls` media playlist - `index.m3u8`.
  - `thumb` - `thumb-<offset>[<resizeparams>].jpg` (offset is the thumbnail video offset in
    milliseconds).
  - `volume_map` - `volume_map.csv`.

- `seqparams` - can be used to select specific sequences by ID (provided in the JSON mapping), e.g.
  `master-sseq1.m3u8`.
- `fileparams` - can be used to select specific sequences by index when using multi URLs. For
  example, `manifest-f1.mpd` will return a MPD only from the first URL.
- `trackparams` - can be used to select specific audio/video tracks. For example, `manifest-a1.mpd`
  will return a MPD containing only the first audio stream of each sequence. The default is to
  include the first audio and first video tracks of each file. The tracks selected on the file name
  are AND-ed with the tracks selected with the `/tracks/` path parameter. `v0/a0` select all
  video/audio tracks respectively. The A/V parameters can be combined with f/s, e.g. `f1-v1-f2-a1` =
  `video1` of `file1` + `audio1` of `file2`, `f1-f2-v1` = `video1` of `file1` + `video1` of `file2`.
- `langparams` - can be used to filter audio tracks/subtitles according to their language
  (ISO 639-3 code). For example, `master-leng.m3u8` will return only english audio tracks.
- `resizeparams` - can be used to resize the returned thumbnail image. For example,
  `thumb-1000-w150-h100.jpg` captures a thumbnail *1 second* into the video, and resizes it to
  `150x100`. If one of the dimensions is omitted, its value is set so that the resulting image will
  retain the aspect ratio of the video frame.

### Mapping response format

When configured to run in mapped mode, `nginx-vod-module` issues an HTTP request to a configured
upstream server in order to receive the layout of media streams it should generate. The response has
to be in JSON format.

This section contains a few simple examples followed by a reference of the supported objects and
fields. But first, a couple of definitions:

- `Source Clip` - a set of audio and/or video frames (tracks) extracted from a single media file.
- `Generator` - a component that can generate audio/video frames. Currently, the only supported
  generator is the silence generator.
- `Filter` - a manipulation that can be applied on audio/video frames. The following filters are
  supported:

  - `Rate` - (speed) change - applies to both audio and video.
  - `Gain` - audio volume change.
  - `Mix` - used to merge several audio tracks together, or to merge the audio of source A
    with the video of source B.

- `Clip` - the result of applying zero or more filters on a set of [source clips](#source-clip).
- `Dynamic Clip` - a clip whose contents is not known in advance, e.g. targeted ad content.
- `Sequence` - a set of clips that should be played one after the other.
- `Set` - several sequences that play together as an adaptive set, each sequence must have the same
  number of clips.

#### Simple mapping

The JSON below maps the request URI to a single MP4 file:

```json
{
  "sequences": [
    {
      "clips": [
        {
          "type": "source",
          "path": "/path/to/video.mp4"
        }
      ]
    }
  ]
}
```

When using multi URLs, this is the only allowed JSON pattern. In other words, it is not possible to
combine more complex JSONs using multi URL.

#### Adaptive set

As an alternative to using multi URL, an adaptive set can be defined via JSON:

```json
{
  "sequences": [
    {
      "clips": [
        {
          "type": "source",
          "path": "/path/to/bitrate1.mp4"
        }
      ]
    },
    {
      "clips": [
        {
          "type": "source",
          "path": "/path/to/bitrate2.mp4"
        }
      ]
    }
  ]
}
```

#### Playlist

The JSON below will play *35 seconds* of `video1` followed by *22 seconds* of `video2`:

```json
{
  "durations": [35000, 22000],
  "sequences": [
    {
      "clips": [
        {
          "type": "source",
          "path": "/path/to/video1.mp4"
        },
        {
          "type": "source",
          "path": "/path/to/video2.mp4"
        }
      ]
    }
  ]
}
```

#### Filters

The JSON below takes `video1`, plays it at *x1.5* and *mixes* the audio of the result with the audio
of `video2`, after reducing it to *50% volume*:

```json
{
  "sequences": [
    {
      "clips": [
        {
          "type": "mixFilter",
          "sources": [
            {
              "type": "rateFilter",
              "rate": 1.5,
              "source": {
                "type": "source",
                "path": "/path/to/video1.mp4"
              }
            },
            {
              "type": "gainFilter",
              "gain": 0.5,
              "source": {
                "type": "source",
                "path": "/path/to/video2.mp4",
                "tracks": "a1"
              }
            }
          ]
        }
      ]
    }
  ]
}
```

#### Continuous live

The JSON below is a sample of a continuous live stream (a live stream in which all videos have
**exactly** the same encoding parameters).

> [!TIP]
> In practice, this JSON will have to be generated by some script, since it is time dependent. (see
> `test/playlist.php` for a sample implementation).

```json
{
  "playlistType": "live",
  "discontinuity": false,
  "segmentBaseTime": 1451904060000,
  "firstClipTime": 1451917506000,
  "durations": [83000, 83000],
  "sequences": [
    {
      "clips": [
        {
          "type": "source",
          "path": "/path/to/video1.mp4"
        },
        {
          "type": "source",
          "path": "/path/to/video2.mp4"
        }
      ]
    }
  ]
}
```

#### Non-continuous live

The JSON below is a sample of a non-continuous live stream (a live stream in which the videos have
**different** encoding parameters).

> [!TIP]
> In practice, this JSON will have to be generated by some script, since it is time dependent (see
> `test/playlist.php` for a sample implementation).

```json
{
  "playlistType": "live",
  "discontinuity": true,
  "initialClipIndex": 171,
  "initialSegmentIndex": 153,
  "firstClipTime": 1451918170000,
  "durations": [83000, 83000],
  "sequences": [
    {
      "clips": [
        {
          "type": "source",
          "path": "/path/to/video1.mp4"
        },
        {
          "type": "source",
          "path": "/path/to/video2.mp4"
        }
      ]
    }
  ]
}
```

### Mapping reference

#### Set

> Top-level object in the mapping.

Mandatory fields:

- `sequences` - array of [sequence](#sequence) objects. The mapping has to contain at least one
  sequence and up to *32 sequences*.

Optional fields:

- `id` - a string that identifies the set. The ID can be retrieved via `$vod_set_id`.
- `playlistType` - a string whose value can be `live`, `vod` or `event` (only supported
  for HLS playlists). Defaults to `vod`.
- `durations` - an array of integers representing clip durations in milliseconds. This field is
  **mandatory** if the mapping contains more than a single clip per sequence. If specified, this
  array must contain at least one element and up to *128 elements*.
- `discontinuity` - a boolean indicating whether the different clips in each sequence have different
  media parameters. This field has different manifestations according to the delivery protocol - a
  value of `true` will generate `#EXT-X-DISCONTINUITY` in HLS, and a multi-period MPD in DASH. The
  default value is `true`, set to `false` only if the media files were transcoded with exactly the
  same parameters (in AVC for example, the clips should have exactly the same SPS/PPS).
- `segmentDuration` - an integer that sets the segment duration in milliseconds. This field, if
  specified, takes priority over the value set in `vod_segment_duration`.
- `consistentSequenceMediaInfo` - a boolean which currently affects only DASH. When set to `true`
  (default) the MPD will report the same media parameters in each period element. Setting to `false`
  can have severe performance implications for long sequences (`nginx-vod-module` has to read the
  media info of all clips included in the mapping in order to generate the MPD).
- `referenceClipIndex` - an integer that sets the (*1-based*) index of the clip that should be used
  to retrieve the video metadata for manifest requests (such as codec, width, height) If
  `consistentSequenceMediaInfo` is set to `false`, this parameter has no effect - all clips are
  parsed. If this parameter is not specified, `nginx-vod-module` uses the last clip by default.
- `notifications` - an array of [notification](#notification) objects, when a segment is requested,
  all the notifications that fall between the start/end times of the segment are fired. The
  notifications must be ordered in an increasing offset order.
- `clipFrom` - an integer that contains a timestamp indicating where the returned stream should
  start. Setting this parameter is equivalent to passing `/clipFrom/` in the URL.
- `clipTo` - an integer that contains a timestamp indicating where the returned stream should end.
  Setting this parameter is equivalent to passing `/clipTo/` in the URL.
- `cache` - a boolean indicating whether the mapping response should be cached (`vod_mapping_cache`)
  or not. Defaults to `true`, the response is cached.
- `closedCaptions` - array of [closed captions](#closed-captions) objects, containing languages and
  IDs of any embedded CEA-608 / CEA-708 captions. If an empty array is provided, the module will
  output `CLOSED-CAPTIONS=NONE` on each `#EXT-X-STREAM-INF` tag. If the list does not appear in the
  JSON, the module will not output any `CLOSED-CAPTIONS` fields in the playlist.

Live fields:

- `firstClipTime` - an integer, **mandatory** for all live playlists unless `clipTimes` is
  specified, containing the absolute time of the first clip in the playlist in milliseconds since
  the *epoch* (`unix_timestamp x 1000`).
- `clipTimes` - an array of integers that sets the absolute time of all the clips in the playlist in
  milliseconds since the *epoch* (`unix_timestamp x 1000`). This field can be used only when
  `discontinuity` is set to `true`. The timestamps may contain gaps, but they are not allowed to
  overlap (`clipTimes[n + 1] >= clipTimes[n] + durations[n]`).
- `segmentBaseTime` - an integer, **mandatory** for continuous live streams, containing the absolute
  time of the first segment of the stream in milliseconds since the *epoch*
  (`unix_timestamp x 1000`). This value must not change during playback. For discontinuous live
  streams, this field is optional:

  - if **not** set, sequential segment indexes will be used throughout the playlist. In this case,
    the upstream server generating the JSON mapping has to maintain state, and update
    `initialSegmentIndex` every time a clip is removed from the playlist.
  - if set, the timing gaps between clips must not be lower than `vod_segment_duration`.

- `firstClipStartOffset` - an integer, *optional*, measured in milliseconds, containing the
  difference between first clip time, and the original start time of the first clip - the time it
  had when it was initially added (before the live window shifted).
- `initialClipIndex` - an integer, **mandatory** for non-continuous live streams that mix videos
  having different encoding parameters (SPS/PPS), containg the index of the first clip in the
  playlist. Whenever a clip is pushed out of the head of the playlist, this value must be
  incremented by one.
- `initialSegmentIndex` - an integer, **mandatory** for live streams that do not set
  `segmentBaseTime`, containing the index of the first segment in the playlist. Whenever a clip is
  pushed out of the head of the playlist, this value must be incremented by the number of segments
  in the clip.
- `presentationEndTime` - an integer, *optional*, measured in milliseconds since the *epoch*. When
  supplied, the module will compare the current time to the supplied value, and signal the end of
  the live presentation if `presentationEndTime` has passed. In HLS, for example, this parameter
  controls whether an `#EXT-X-ENDLIST` tag should be included in the media playlist. When the
  parameter is **not** supplied, the module will **not** signal live presentation end.
- `expirationTime` - an integer, *optional*, measured in milliseconds since the *epoch*. When
  supplied, the module will compare the current time to the supplied value, and if `expirationTime`
  has passed, the module will return a 404 error for manifest requests (segment requests will
  continue to be served). When both `presentationEndTime` and `expirationTime` have passed,
  `presentationEndTime` takes priority, i.e. manifest requests will be served and signal
  presentation end.
- `liveWindowDuration` - an integer, *optional*, providing a way to override
  `vod_live_window_duration` specified in the configuration. If the value exceeds the absolute value
  specified in `vod_live_window_duration`, it is ignored.
- `timeOffset` - and integer that sets an offset that should be applied to the server clock when
  serving live requests. This parameter can be used to test future/past events.

#### Sequence

Mandatory fields:

- `clips` - an array of [clip](#clip-abstract) objects (**mandatory**). The number of elements must
  match the number the `durations` array specified on the set. If the `durations` array is **not**
  specified, the clips array must contain a **single** element.

Optional fields:

- `id` - a string that identifies the sequence. The string must **not** contain `-` and be at most
  *10 characters* long. The ID can be retrieved via `$vod_sequence_id`.
- `language` - a 3-letter (ISO-639-2) language code, this field takes priority over any language
  specified on the media file (`mdhd` MP4 atom).
- `label` - a friendly string that identifies the sequence. If a language is specified, a default
  label will be automatically derived by it - e.g. if language is `ita`, by default `italiano`
  will be used as the label.
  > For `roles`, `characteristics`, and `forced`, the label must be explicitly specified. The
  > default label does not consider them.

- `roles` - an array of role schemes as defined in
  [ISO/IEC 23009-1](https://www.iso.org/standard/83314.html) Section 5.8.5.5.
- `default` - a boolean that sets the value of the `DEFAULT` attribute of `#EXT-X-MEDIA` tags using
  this sequence. If not specified, the first `#EXT-X-MEDIA` tag in each group returns `DEFAULT=YES`.
- `autoselect` - a boolean that sets the value of the `AUTOSELECT` attribute of `#EXT-X-MEDIA` tags
  using this sequence. If not specified, the `#EXT-X-MEDIA` tags return `AUTOSELECT=YES`.
- `characteristics` - a string of characteristics as defined in
  [RFC 8216 Section 4.3.4.1](https://datatracker.ietf.org/doc/html/rfc8216#section-4.3.4.1).
- `forced` - a boolean that sets the value of the `FORCED` attribute of `#EXT-X-MEDIA` tags using
  this sequence.
- `bitrate` - an object that can be used to set the bitrate for the different media types,
  in bits per second. For example, `{"v": 900000, "a": 64000}`. If the bitrate is not supplied,
  `nginx-vod-module` will estimate it based on the last clip in the sequence.
- `avg_bitrate` - an object that can be used to set the average bitrate for the different media
  types, in bits per second. See `bitrate` above for a sample object. If specified, the module will
  use the value to populate the `AVERAGE-BANDWIDTH` attribute of `#EXT-X-STREAM-INF` in HLS.

> [!IMPORTANT]
> The options `label`, `roles`, `characteristics`, and `forced` are used to group tracks. As HLS
> does not have the concept of **video** `AdaptationSet`, any use of these options will cause the
> HLS manifest builder to consider only the **first** video group.

#### Clip (abstract)

Mandatory fields:

- `type` - a string that defines the type of the clip. Allowed values are:

  - `source`
  - `rateFilter`
  - `mixFilter`
  - `gainFilter`
  - `silence`
  - `concat`
  - `dynamic`

Optional fields:

- `keyFrameDurations` - an array of integers containing the durations in milliseconds of the video
  key frames in the clip. This property can only be supplied on the top level clips of each
  sequence. Supplying this property on nested clips has no effect. Supplying the key frame durations
  enables the module to both:

  - align the segments to key frames.
  - report the correct segment durations in the manifest - providing an alternative to setting
    `vod_manifest_segment_durations_mode` to `accurate`, which is not supported for multi clip media
    sets (*for performance reasons*).

- `firstKeyFrameOffset` - an integer measured in milliseconds relative to `firstClipTime` that sets
  the offset of the first video key frame in the clip. Defaults to `0`.

#### Source clip

Mandatory fields:

- `type` - a string with the value `source`.
- `path` - a string containing the path of the MP4 file. The string `"empty"` can be used to
  represent an empty captions file (useful in case only some videos in a playlist have captions).

Optional fields:

- `id` - a string that identifies the source clip.
- `sourceType` - sets the interface that should be used to read the MP4 file, allowed values are:
  `file` and `http`. Defaults to `http` when `vod_remote_upstream_location` is set, `file`
  otherwise.
- `tracks` - a string that specifies the tracks that should be used. Defaults to `v1-a1`, which
  means the first video and audio track.
- `clipFrom` - an integer that specifies an offset in milliseconds from the beginning of the media
  file, from which to start loading frames.
- `encryptionKey` - a base64 encoded string containing the key (128/192/256 bit) that should be used
  to decrypt the file.
- `encryptionIv` - a base64 encoded string containing the iv (128 bit) that should be used to
  decrypt the file.
- `encryptionScheme` - the encryption scheme that was used to encrypt the file. Currently, only two
  schemes are supported - `cenc` for MP4 files and `aes-cbc` for caption files.

#### Rate filter clip

Mandatory fields:

- `type` - a string with the value `rateFilter`.
- `rate` - a float that specified the acceleration factor, e.g. a value of `2` means double speed.
  Allowed values are in the range `0.5` to `2` with up to two decimal points.
- `source` - a [clip](#clip-abstract) object on which to perform the rate filtering.

#### Gain filter clip

Mandatory fields:

- `type` - a string with the value `gainFilter`.
- `gain` - a float that specified the amplification factor, e.g. a value of `2` means twice as loud.
  The gain must be positive with up to two decimal points.
- `source` - a [clip](#clip-abstract) object on which to perform the gain filtering.

#### Mix filter clip

Mandatory fields:

- `type` - a string with the value `mixFilter`.
- `sources` - an array of [clip](#clip-abstract) objects to mix. This array must contain at least
  one clip and up to *32 clips*.

#### Concat clip

Mandatory fields:

- `type` - a string with the value `concat`.
- `durations` - an array of integers representing MP4 durations in milliseconds. This array must
  match the `paths` array in count and order.

> [!IMPORTANT]
> Either `paths` or `clipIds` must be specified.

Optional fields:

- `paths` - an array of strings containing the paths of the MP4 files.
- `clipIds` - an array of strings, containing the IDs of [source clips](#source-clip). The IDs are
  translated to paths by issuing a request to the URI specified in `vod_source_clip_map_uri`.
- `tracks` - a string that specifies the tracks that should be used. Defaults to `v1-a1`, which
  means the first video and audio track.
- `offset` - an integer in milliseconds that indicates the timestamp offset of the first frame in
  the concatenated stream relative to the clip start time.
- `basePath` - a string that should be added as a prefix to all the paths.
- `notifications` - array of [notification](#notification) objects. When a segment is requested, all
  the notifications that fall between the start/end times of the segment are fired. The
  notifications must be ordered in an increasing offset order.

#### Dynamic clip

Mandatory fields:

- `type` - a string with the value `dynamic`.
- `id` - a string that uniquely identifies the dynamic clip. Used for mapping the clip to its
  content.

#### Notification

Mandatory fields:

- `offset` - an integer in milliseconds that indicates the time in which the notification should be
  fired. When the notification object is contained in the media set, the `offset` is relative to
  `firstClipTime` (`0` for vod). When the notification object is contained in a
  [concat clip](#concat-clip), the `offset` is relative to the beginning of that clip.
- `id` - a string that identifies the notification. This ID can be referenced in
  `vod_notification_uri` using the variable `$vod_notification_id`.

#### Closed captions

Mandatory fields:

- `id` - a string that identifies the embedded captions. This will become the `INSTREAM-ID` field
  and must have one of the following values: `CC1`, `CC3`, `CC3`, `CC4`, or `SERVICEn`, where `n` is
  between `1` and `63`.
- `label` - a friendly string that indicates the language of the closed caption track.

Optional fields:

- `language` - a 3-letter (ISO-639-2) language code that indicates the language of the closed
  caption track.


### Security

#### Authorization

##### CDN-based delivery

Media packaged by `nginx-vod-module` can be protected using CDN tokens, this works as follows:

- Some application authenticates the user and decides whether the user should be allowed to watch a
  specific video. If the user is allowed, the application generates a tokenized URL for the manifest
  of the video.
- The CDN validates the token, and if found to be valid, forwards the request to `nginx-vod-module`
  on the origin.
- The NGINX server builds the manifest response and generates tokens for the segment URLs contained
  inside it. The [nginx-secure-token-module](https://github.com/kaltura/nginx-secure-token-module)
  can be used to accomplish this task, it currently supports Akamai and CloudFront tokens.
- The CDN validates the token on each segment that is requested.

In this setup it is also highly recommended to block direct access to the origin server by
authenticating the CDN requests. Without this protection, a user who somehow gets the address of the
origin will be able to bypass any CDN token enforcement. When using Akamai, this can be accomplished
using [nginx_mod_akamai_g2o](https://github.com/refractalize/nginx_mod_akamai_g2o). For other
CDNs, it may be possible to configure the CDN to send a secret header to the origin and then simply
enforce the header using an NGINX `if` statement:

```lua
if ($http_x_secret_origin_header != 'secret value') {
  return 403;
}
```

In addition to the above, most CDNs support other access control settings, such as geo-location.
These restrictions are completely transparent to the origin and should work well.

##### Direct delivery

Deployments in which the media is pulled directly from `nginx-vod-module` can protect the media
using NGINX access control directives, such `allow`, `deny`, or `access_by_lua` (for more complex
scenarios).

In addition, it is possible to build a token based solution (as detailed in the previous section)
without a CDN, by having the NGINX server validate the token. The
[nginx-akamai-token-validate-module](https://github.com/kaltura/nginx-akamai-token-validate-module)
can be used to validate Akamai tokens. *Locations* on which the module is enabled will return `403`
unless the request contains a valid Akamai token.

#### URL encryption

As an alternative to tokenization, URL encryption can be used to prevent an attacker from being able
to craft a playable URL. URL encryption can be implemented with
[nginx-secure-token-module](https://github.com/kaltura/nginx-secure-token-module), and is supported
for HLS and DASH (with manifest format set to segmentlist).

In terms of security, the main advantage of CDN tokens over URL encryption is that CDN tokens
usually expire, while encrypted URLs do not (someone who obtains a playable URL will be able to use
it indefinitely).

#### Media encryption

This module supports AES-128 and SAMPLE-AES HLS encryption schemes. The main difference between
media encryption and [DRM](#drm) is the mechanism used to transfer the encryption key to the client.
With media encryption the key is fetched by the client by performing a simple GET request to
`nginx-vod-module`, while with DRM the key is returned inside a vendor specific license response.

Media encryption reduces the problem of securing the media to the need to secure the encryption key.
The media segment URLs (which compose the vast majority of the traffic) can be completely
unprotected, and easily cacheable by any proxies between the client and servers (unlike
tokenization). The encryption key request can then be protected using one of the methods mentioned
above (CDN tokens, NGINX access rules).

In addition, it is possible to configure the `nginx-vod-module` to return the encryption key over
HTTPS while having the segments delivered over HTTP. The way to configure this is to set
`vod_segments_base_url` to `http://<domain>` and set `vod_base_url` to `https://<domain>`.

#### DRM

This module has the ability to perform on-the-fly encryption for DASH (CENC) and FairPlay HLS. The
encryption is performed while serving a video / audio segment to the client. Therefore, when working
with DRM it is recommended not to serve the content directly from `nginx-vod-module` to end-users. A
more scalable architecture would be to use proxy servers or a CDN in order to cache the encrypted
segments.

In order to perform the encryption, `nginx-vod-module` needs several parameters, including `key` and
`key_id`, these parameters are fetched from an external server via HTTP GET requests. The
`vod_drm_upstream_location` parameter specifies an NGINX `location` that is used to access the DRM
server, and the request URI is configured using `vod_drm_request_uri` (this parameter can include
[variables](#nginx-variables)). The response of the DRM server is a JSON with the following format:

```json
[
  {
    "pssh": [
      {
        "data": "CAESEGMyZjg2MTczN2NjNGYzODIaB2thbHR1cmEiCjBfbmptaWlwbXAqBVNEX0hE",
        "uuid": "edef8ba9-79d6-4ace-a3c8-27dcd51d21ed"
      }
    ],
    "key": "GzoNU9Dfwc//Iq3/zbzMUw==",
    "key_id": "YzJmODYxNzM3Y2M0ZjM4Mg=="
  }
]
```

- `pssh.data` - base64 encoded binary data, the format of this data is DRM vendor specific.
- `pssh.uuid` - the DRM system UUID, in this case, `edef8ba9-79d6-4ace-a3c8-27dcd51d21ed` stands for
  Widevine.
- `key` - base64 encoded encryption key (128 bit).
- `key_id` - base64 encoded key identifier (128 bit).
- `iv` - **optional** base64 encoded initialization vector (128 bit). The IV is currently used only
  in HLS (FairPlay), in the other protocols an IV is automatically generated.

##### Sample configurations

Apple FairPlay HLS:

```conf
location ~ ^/hls/cbcs/(?<playback_token>[^/]+)/ {
  vod hls;

  vod_hls_version 5;
  vod_hls_encryption_method sample-aes;
  vod_hls_encryption_key_uri 'skd://$vod_set_id';
  vod_hls_encryption_key_format 'com.apple.streamingkeydelivery';
  vod_hls_encryption_key_format_versions '1';

  vod_drm_enabled on;
  vod_drm_request_uri '/drm/$playback_token';

  vod_last_modified_types *;

  add_header Access-Control-Allow-Methods 'GET, HEAD, OPTIONS';
  add_header Access-Control-Allow-Headers 'Origin, Range';
  add_header Access-Control-Allow-Origin '*';
  add_header Access-Control-Expose-Headers 'Server, Range, Content-Length, Content-Range';
  add_header Access-Control-Max-Age '86400';

  expires 1y;
}
```

Common Encryption HLS:

```conf
location ~ ^/hls/cenc/(?<playback_token>[^/]+)/ {
  vod hls;

  vod_hls_version 6;
  vod_hls_encryption_method sample-aes-ctr;

  vod_drm_enabled on;
  vod_drm_request_uri '/drm/$playback_token';

  vod_last_modified_types *;

  expires 1y;

  add_header Access-Control-Allow-Methods 'GET, HEAD, OPTIONS';
  add_header Access-Control-Allow-Headers 'Origin, Range';
  add_header Access-Control-Allow-Origin '*';
  add_header Access-Control-Expose-Headers 'Server, Range, Content-Length, Content-Range';
  add_header Access-Control-Max-Age '86400';
}
```

### Performance recommendations

- Do not allow customers to play the content directly from `nginx-vod-module`. As all the different
  streaming protocols supported are HTTP based, so they can be cached by standard HTTP CDNs (such as
  Akamai, CloudFront, Fastly).
- Keep the `nginx-vod-module` as close as possible to where the source MP4 files are stored.
- Enable `nginx-vod-module` cache mechanisms:
  - `vod_metadata_cache` - saves the video metadata avoiding the need to re-read them for every
    segment. This cache should be rather large, in the order of **GBs**.
  - `vod_response_cache` - saves the responses of manifest requests. This cache may not be required
    when using a second layer of caching server. No need to allocate a large buffer for this cache,
    `128m` is probably more than enough for most deployments.
  - `vod_mapping_cache` - for `mapped` mode only, few MBs is usually enough.
  - `open_file_cache` - caches open file handles.
  > The hit/miss ratios of these caches can be tracked by enabling `vod_performance_counters` and
  > setting up a `vod_status` `location`.

- Enable `aio` for `local` and `mapped` modes - NGINX has to be compiled with `aio` support, and it
  has to be enabled in `nginx.conf` (`aio on`). You can verify the setup by looking at the
  performance counters on the status page - `read_file` (`aio off`) vs. `async_read_file`
  (`aio on`).
- Enable asynchronous file open for `local` and `mapped` modes - NGINX has to be compiled with
  **threads** support, and `vod_open_file_thread_pool` has to be specified in `nginx.conf`. You can
  verify the setup by looking at the performance counters on the status page - `open_file` vs.
  `async_open_file`.
  > The `open_file` may be non-zero with `vod_open_file_thread_pool` enabled due to the open file
  > cache - open requests that are served from cache will be counted as synchronous `open_file`.

- When using DASH with DRM, if the video files have a **single** NALU per frame, set
  `vod_min_single_nalu_per_frame_segment` to non-zero.
- The muxing overhead of the streams generated by this module can be reduced by changing the
  following parameters:
  - HLS - set `vod_hls_mpegts_align_frames` to `off` and `vod_hls_mpegts_interleave_frames` to `on`.
- Enable gzip compression on manifest responses -
  `gzip_types application/vnd.apple.mpegurl application/dash+xml text/xml text/vtt`.
- Apply common NGINX performance best practices, such as `tcp_nodelay on`, `client_header_timeout`.
- Tweak common Kernel parameters for high-throughput, such as `net.core.*`, `net.ipv4.*`,
  `vm.swappiness`, as well as file descriptor limits.

### Configuration directives - Basic

#### vod

- **syntax**: `vod none | dash | hls | thumb | volume_map`
- **default**: `n/a`
- **context**: `location`

Enables the `nginx-vod-module` on the enclosing `location`:

- `none` - serves the MP4 files as is / clipped.
- `dash` - Dynamic Adaptive Streaming over HTTP packager.
- `hls` - Apple HTTP Live Streaming packager.
- `thumb` - thumbnail capture.
- `volume_map` - audio volume map.

#### vod_mode

- **syntax**: `vod_mode local | remote | mapped`
- **default**: `local`
- **context**: `http`, `server`, `location`

Sets the file access mode - `local`, `remote`, or `mapped` (see the features section above for more
details).

#### vod_status

- **syntax**: `vod_status`
- **default**: `n/a`
- **context**: `location`

Enables the `nginx-vod-module` status page on the enclosing `location`. The following query params
are supported:

- `?reset=1` - resets the performance counters and cache stats.
- `?format=prom` - returns the output in format compatible with Prometheus (the default format is
  XML).

### Configuration directives - Segmentation

#### vod_segment_duration

- **syntax**: `vod_segment_duration :duration`
- **default**: `10s`
- **context**: `http`, `server`, `location`

Sets the segment duration in milliseconds. It is highly recommended to use a segment duration that
is a multiple of the GOP duration. If the segment duration is not a multiple of GOP duration, and
`vod_align_segments_to_key_frames` is enabled, there may be significant differences between the
segment duration that is reported in the manifest and the actual segment duration. This could also
lead to the appearance of empty segments within the stream.

#### vod_live_window_duration

- **syntax**: `vod_live_window_duration :duration`
- **default**: `30000`
- **context**: `http`, `server`, `location`

Sets the total duration in milliseconds of the segments that should be returned in a live manifest.

- If the value is *positive*, this module returns a range of maximum `vod_live_window_duration`
  milliseconds, ending at the current server time.
- If the value is *negative*, this module returns a range of maximum `vod_live_window_duration`
  milliseconds from the end of the JSON mapping.
- If the value is set to `0`, the live manifest will contain all the segments that are fully
  contained in the JSON mapping time frame.

#### vod_force_playlist_type_vod

- **syntax**: `vod_force_playlist_type_vod on | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

Generate a vod stream even when the media set has `playlistType=live`. Enabling this setting has the
following effects:

- Frame timestamps will be continuous and start from zero.
- Segment indexes will start from one.
- In case of HLS, the returned manifest will have both `#EXT-X-PLAYLIST-TYPE:VOD` and
- `#EXT-X-ENDLIST`.

This can be useful for clipping vod sections out of a live stream.

#### vod_force_continuous_timestamps

- **syntax**: `vod_force_continuous_timestamps on | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

Generate continuous timestamps even when the media set has gaps (gaps can created by the use of
`clipTimes`). If ID3 timestamps are enabled (`vod_hls_mpegts_output_id3_timestamps`), they contain
the original timestamps that were set in `clipTimes`.

#### vod_bootstrap_segment_durations

- **syntax**: `vod_bootstrap_segment_durations :duration`
- **default**: `none`
- **context**: `http`, `server`, `location`

Adds a bootstrap segment duration in milliseconds. This setting can be used to make the first few
segments shorter than the default segment duration, thus making the adaptive bitrate selection
kick-in earlier without the overhead of short segments throughout the video.

#### vod_align_segments_to_key_frames

- **syntax**: `vod_align_segments_to_key_frames on | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

When enabled, the module forces all segments to start with a key frame. Enabling this setting can
lead to differences between the actual segment durations and the durations reported in the manifest
(unless `vod_manifest_segment_durations_mode` is set to accurate).

#### vod_segment_count_policy

- **syntax**: `vod_segment_count_policy last_short | last_long | last_rounded`
- **default**: `last_short`
- **context**: `http`, `server`, `location`

Configures the policy for calculating the segment count, for `segment_duration` of *10 seconds*:

- `last_short` - a file of *33 seconds* is partitioned as - `10`, `10`, `10`, `3`.
- `last_long` - a file of *33 seconds* is partitioned as - `10`, `10`, `13`.
- `last_rounded` - a file of *33 seconds* is partitioned as - `10`, `10`, `13`, a file of
  *38 seconds* is partitioned as `10`, `10`, `10`, `8`.

#### vod_manifest_duration_policy

- **syntax**: `vod_manifest_duration_policy min | max`
- **default**: `max`
- **context**: `http`, `server`, `location`

Configures the policy for calculating the duration of a manifest containing multiple streams:

- `max` - uses the maximum stream duration.
- `min` - uses the minimum non-zero stream duration.

#### vod_manifest_segment_durations_mode

- **syntax**: `vod_manifest_segment_durations_mode estimate | accurate`
- **default**: `estimate`
- **context**: `http`, `server`, `location`

Configures the calculation mode of segment durations within manifest requests:

- `estimate` - reports the duration as configured in `nginx.conf`, e.g. if `vod_segment_duration`
  is `10000`, an HLS manifest will contain `#EXTINF:10`.
- `accurate` - reports the exact duration of the segment, taking into account the frame durations,
  e.g. for a frame rate of `29.97` and *10 seconds* segments it will report the first segment as
  `10.01`. this mode also takes into account the key frame alignment, in case
  `vod_align_segments_to_key_frames` is `on`.

#### vod_media_set_override_json

- **syntax**: `vod_media_set_override_json :json`
- **default**: `{}`
- **context**: `http`, `server`, `location`

This parameter provides a way to override portions of the media set JSON (mapped mode only). For
example, `vod_media_set_override_json '{"clipTo":20000}'` clips the media set to *20 seconds*. The
parameter value can contain [variables](#nginx-variables).

### Configuration directives - Upstream

#### vod_upstream_location

- **syntax**: `vod_upstream_location :location`
- **default**: `none`
- **context**: `http`, `server`, `location`

Sets an NGINX `location` that is used to read the MP4 file (`remote` mode) or mapping the request
URI (`mapped` mode).

#### vod_remote_upstream_location

- **syntax**: `vod_remote_upstream_location :location`
- **default**: `none`
- **context**: `http`, `server`, `location`

Sets an NGINX `location` that is used to read the MP4 file on `remote` or `mapped` mode. If this
directive is set on `mapped` mode, the module reads the MP4 files over HTTP, treating the paths in
the JSON mapping as URIs (default behavior is to read from local files).

#### vod_max_upstream_headers_size

- **syntax**: `vod_max_upstream_headers_size :size`
- **default**: `4k`
- **context**: `http`, `server`, `location`

Sets the size that is allocated for holding the response headers when issuing upstream requests to
`vod_xxx_upstream_location`.

#### vod_upstream_extra_args

- **syntax**: `vod_upstream_extra_args :querystring`
- **default**: `empty`
- **context**: `http`, `server`, `location`

Extra query string arguments, e.g. `"arg1=value1&arg2=value2&..."`, that should be added to the
upstream request (`remote` and `mapped` modes only). The parameter value can contain
[variables](#nginx-variables).

#### vod_media_set_map_uri

- **syntax**: `vod_media_set_map_uri :uri`
- **default**: `$vod_suburi`
- **context**: `http`, `server`, `location`

Sets the URI of media set mapping requests, the parameter value can contain
[variables](#nginx-variables). In case of multi URI, `$vod_suburi` will be the current sub URI (a
separate request is issued per sub URL).

#### vod_path_response_prefix

- **syntax**: `vod_path_response_prefix :prefix`
- **default**: `{"sequences":[{"clips":[{"type":"source","path":"`
- **context**: `http`, `server`, `location`

Sets the prefix that is expected in URI mapping responses (`mapped` mode only).

#### vod_path_response_postfix

- **syntax**: `vod_path_response_postfix postfix`
- **default**: `"}]}]}`
- **context**: `http`, `server`, `location`

Sets the postfix that is expected in URI mapping responses (`mapped` mode only).

#### vod_max_mapping_response_size

- **syntax**: `vod_max_mapping_response_size :length`
- **default**: `1k`
- **context**: `http`, `server`, `location`

Sets the maximum length of a path returned from upstream (`mapped` mode only).

### Configuration directives - Fallback

#### vod_fallback_upstream_location

- **syntax**: `vod_fallback_upstream_location :location`
- **default**: `none`
- **context**: `http`, `server`, `location`

Sets an NGINX `location` to which the request is forwarded after encountering a file not found error
(`local` and `mapped` modes only).

#### vod_proxy_header_name

- **syntax**: `vod_proxy_header_name :name`
- **default**: `X-Kaltura-Proxy`
- **context**: `http`, `server`, `location`

Sets the name of an HTTP header that is used to prevent fallback proxy loops (`local` and `mapped`
modes only).

#### vod_proxy_header_value

- **syntax**: `vod_proxy_header_value :name`
- **default**: `dumpApiRequest`
- **context**: `http`, `server`, `location`

Sets the value of an HTTP header that is used to prevent fallback proxy loops (`local` and `mapped`
modes only).

### Configuration directives - Performance

#### vod_metadata_cache

- **syntax**: `vod_metadata_cache :zone_name :zone_size [:expiration] | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

Configures the size and shared memory object name of the video metadata cache. For MP4 files this
cache holds the `moov` atom.

#### vod_mapping_cache

- **syntax**: `vod_mapping_cache :zone_name :zone_size [:expiration] | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

Configures the size and shared memory object name of the mapping cache for vod (`mapped` mode only).

#### vod_live_mapping_cache

- **syntax**: `vod_live_mapping_cache :zone_name :zone_size [:expiration] | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

Configures the size and shared memory object name of the mapping cache for live (`mapped` mode
only).

#### vod_response_cache

- **syntax**: `vod_response_cache :zone_name :zone_size [:expiration] | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

Configures the size and shared memory object name of the response cache. The response cache holds
manifests and other non-video content (like DASH init segment, HLS encryption key). Video segments
are not cached.

#### vod_live_response_cache

- **syntax**: `vod_live_response_cache :zone_name :zone_size [:expiration] | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

Configures the size and shared memory object name of the response cache for time changing live
responses. This cache holds the following types of responses for live: DASH MPD, HLS M3U8 playlist.

#### vod_initial_read_size

- **syntax**: `vod_initial_read_size :size`
- **default**: `4k`
- **context**: `http`, `server`, `location`

Sets the size of the initial read operation of the MP4 file.

#### vod_max_metadata_size

- **syntax**: `vod_max_metadata_size :size`
- **default**: `128m`
- **context**: `http`, `server`, `location`

Sets the maximum supported video metadata size (for MP4 - `moov` atom size).

#### vod_max_frames_size

- **syntax**: `vod_max_frames_size :size`
- **default**: `16m`
- **context**: `http`, `server`, `location`

Sets the limit on the total size of the frames of a single segment.

#### vod_max_frame_count

- **syntax**: `vod_max_frame_count :count`
- **default**: `1048576`
- **context**: `http`, `server`, `location`

Sets the limit on the total count of the frames read to serve non segment (e.g. playlist) request.

#### vod_segment_max_frame_count

- **syntax**: `vod_segment_max_frame_count :count`
- **default**: `65536`
- **context**: `http`, `server`, `location`

Sets the limit on the total count of the frames read to serve segment request.

#### vod_cache_buffer_size

- **syntax**: `vod_cache_buffer_size :size`
- **default**: `256k`
- **context**: `http`, `server`, `location`

Sets the size of the cache buffers used when reading MP4 frames.

#### vod_open_file_thread_pool

- **syntax**: `vod_open_file_thread_pool :pool_name | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

Enables the use of asynchronous file open via thread pool. The thread pool must be defined with a
`thread_pool` directive, if no pool name is specified the *default* pool is used. This directive is
supported when compiling with `--add-threads`.

> [!NOTE]
> This directive currently disables the use of `open_file_cache`.

#### vod_output_buffer_pool

- **syntax**: `vod_output_buffer_pool :size :count | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

Pre-allocates buffers for generating response data, saving the need allocate/free the buffers on
every request.

#### vod_performance_counters

- **syntax**: `vod_performance_counters :zone_name | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

Configures the shared memory object name of the performance counters.

### Configuration directives - URL structure

#### vod_base_url

- **syntax**: `vod_base_url :url`
- **default**: `see below`
- **context**: `http`, `server`, `location`

Sets the base URL (scheme + domain) that should be returned in manifest responses. The parameter
value can contain [variables](#nginx-variables). If the parameter evaluates to an empty string
relative URLs will be used. If the parameter evaluates to a string ending with `/`, it is assumed to
be a full URL - the module only appends the file name to it, instead of a full URI.

When not set the base URL is determined as follows:

- If the request did not contain a host header (HTTP/1.0) relative URLs will be returned.
- Otherwise, the base URL will be `$scheme://$http_host`. This affects only HLS and DASH.

#### vod_segments_base_url

- **syntax**: `vod_segments_base_url :url`
- **default**: `see below`
- **context**: `http`, `server`, `location`

Sets the base URL (scheme + domain) that should be used for delivering video segments. The parameter
value can contain [variables](#nginx-variables), if the parameter evaluates to an empty string
relative URLs will be used. When not set `vod_base_url` will be used. This affects only HLS.

#### vod_multi_uri_suffix

- **syntax**: `vod_multi_uri_suffix :suffix`
- **default**: `.urlset`
- **context**: `http`, `server`, `location`

A URL suffix that is used to identify multi URLs. A multi URL is a way to encode several different
URLs that should be played together as an adaptive streaming set, under a single URL. When the
default suffix is used an HLS set URL may look like:
`http://<domain>/hls/common-prefix,bitrate1,bitrate2,common-suffix.urlset/master.m3u8`.

#### vod_clip_to_param_name

- **syntax**: `vod_clip_to_param_name :name`
- **default**: `clipTo`
- **context**: `http`, `server`, `location`

The name of the clip to request parameter.

#### vod_clip_from_param_name

- **syntax**: `vod_clip_from_param_name :name`
- **default**: `clipFrom`
- **context**: `http`, `server`, `location`

The name of the clip from request parameter.

#### vod_tracks_param_name

- **syntax**: `vod_tracks_param_name :name`
- **default**: `tracks`
- **context**: `http`, `server`, `location`

The name of the tracks request parameter.

#### vod_time_shift_param_name

- **syntax**: `vod_time_shift_param_name :name`
- **default**: `shift`
- **context**: `http`, `server`, `location`

The name of the shift request parameter.

#### vod_speed_param_name

- **syntax**: `vod_speed_param_name :name`
- **default**: `speed`
- **context**: `http`, `server`, `location`

The name of the speed request parameter.

#### vod_lang_param_name

- **syntax**: `vod_lang_param_name :name`
- **default**: `lang`
- **context**: `http`, `server`, `location`

The name of the language request parameter.

#### vod_force_sequence_index

- **syntax**: `vod_force_sequence_index on | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

Use sequence index in segment URIs even if there is only one sequence.

### Configuration directives - Response headers

#### vod_expires

- **syntax**: `vod_expires :time`
- **default**: `none`
- **context**: `http`, `server`, `location`

Sets the value of the `Expires` and `Cache-Control` response headers for successful requests.

This directive is similar to built-in `expires` directive, except that it only supports the
expiration interval scenario (`epoch`, `max`, `off`, and day time are not supported).

The main motivation for using this directive instead of the built-in `expires` is to have different
expiration for VOD and dynamic live content. If this directive is not specified, `nginx-vod-module`
will not set the `Expires` / `Cache-Control` headers. This setting affects all types of requests in
VOD playlists and segment requests in live playlists.

#### vod_expires_live

- **syntax**: `vod_expires_live :time`
- **default**: `none`
- **context**: `http`, `server`, `location`

Same as [`vod_expires`](#vod_expires) for live requests that are not time dependent and not segments
(e.g. HLS - `master.m3u8`).

#### vod_expires_live_time_dependent

- **syntax**: `vod_expires_live_time_dependent :time`
- **default**: `none`
- **context**: `http`, `server`, `location`

Same as [`vod_expires`](#vod_expires) for live requests that are time dependent (HLS - `index.m3u8`,
DASH - `manifest.mpd`).

#### vod_last_modified

- **syntax**: `vod_last_modified :time`
- **default**: `none`
- **context**: `http`, `server`, `location`

Sets the value of the `Last-Modified` header returned on the response, by default the module does
not return a `Last-Modified` header.

The reason for having this parameter here is in order to support `If-Modified-Since` and
`If-Unmodified-Since`. Since the built-in `ngx_http_not_modified_filter_module` runs before any
other header filter module, it will not see any headers set by `add_headers` or `more_set_headers`.
This makes NGINX always reply as if the content changed (`412` for `If-Unmodified-Since` or `200`
for `If-Modified-Since`).

For live requests that are not segments (e.g. live DASH MPD), the `Last-Modified` is set to the
current server time.

#### vod_last_modified_types

- **syntax**: `vod_last_modified_types :mime_type...`
- **default**: `none`
- **context**: `http`, `server`, `location`

Sets the MIME types for which the `Last-Modified` header should be set. The special value `*`
matches any MIME type.

### Configuration directives - Ad stitching

> [!IMPORTANT]
> For `mapped` mode only.

#### vod_dynamic_mapping_cache

- **syntax**: `vod_dynamic_mapping_cache :zone_name :zone_size [:expiration] | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

Configures the size and shared memory object name of the cache that stores the mapping of
[dynamic clips](#dynamic-clip).

#### vod_dynamic_clip_map_uri

- **syntax**: `vod_dynamic_clip_map_uri :uri`
- **default**: `none`
- **context**: `http`, `server`, `location`

Sets the URI that should be used to map [dynamic clips](#dynamic-clip). The parameter value can
contain [variables](#nginx-variables), specifically, `$vod_clip_id` contains the ID of the clip that
should be mapped. The expected response from this URI is a JSON containing a
[concat clip](#concat-clip) object.

#### vod_source_clip_map_uri

- **syntax**: `vod_source_clip_map_uri :uri`
- **default**: `none`
- **context**: `http`, `server`, `location`

Sets the URI that should be used to map [source clips](#source-clip) defined using the `clipIds`
property of concat. The parameter value can contain [variables](#nginx-variables), specifically,
`$vod_clip_id` contains the ID of the clip that should be mapped. The expected response from this
URI is a JSON containing a [source clip](#source-clip) object.

#### vod_redirect_segments_url

- **syntax**: `vod_redirect_segments_url :url`
- **default**: `none`
- **context**: `http`, `server`, `location`

Sets a URL to which requests for segments should be redirected. The parameter value can contain
variables, specifically, `$vod_dynamic_mapping` contains a serialized representation of the mapping
of [dynamic clips](#dynamic-clip).

#### vod_apply_dynamic_mapping

- **syntax**: `vod_apply_dynamic_mapping :mapping`
- **default**: `none`
- **context**: `http`, `server`, `location`

Maps dynamic clips to concat clips using the given expression, previously generated by
`$vod_dynamic_mapping`. The parameter value can contain [variables](#nginx-variables).

#### vod_notification_uri

- **syntax**: `vod_notification_uri :uri`
- **default**: `none`
- **context**: `http`, `server`, `location`

Sets the URI that should be used to issue notifications. The parameter value can contain
[variables](#nginx-variables), specifically, `$vod_notification_id` contains the ID of the
notification that is being fired. The response from this URI is ignored.

### Configuration directives - DRM / Encryption

#### vod_secret_key

- **syntax**: `vod_secret_key :secret_key`
- **default**: `empty`
- **context**: `http`, `server`, `location`

Sets the seed that is used to generate the TS encryption key and DASH encryption IVs. The parameter
value can contain [variables](#nginx-variables), and will usually have the structure
`secret-$vod_filepath`. See the list of [variables](#nginx-variables) added by this module.

#### vod_encryption_iv_seed

- **syntax**: `vod_encryption_iv_seed :iv_seed`
- **default**: `empty`
- **context**: `http`, `server`, `location`

Sets the seed that is used to generate the encryption IV, currently applies only to HLS/fMP4 with
AES-128 encryption. The parameter value can contain [variables](#nginx-variables).

#### vod_drm_enabled

- **syntax**: `vod_drm_enabled on | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

When enabled, the module encrypts the media segments according to the response it gets from the DRM
upstream.

#### vod_drm_single_key

- **syntax**: `vod_drm_single_key on | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

When enabled, the module requests the DRM info only for the first sequence and applies it to all
sequences. When disabled, the DRM info is requested for each sequence separately. For DASH, enabling
this setting makes the module place the `ContentProtection` tag under `Representation`, otherwise,
it is placed under `AdaptationSet`.

#### vod_drm_clear_lead_segment_count

- **syntax**: `vod_drm_clear_lead_segment_count :count`
- **default**: `1`
- **context**: `http`, `server`, `location`

Sets the number of clear (**unencrypted**) segments in the beginning of the stream. A clear lead
enables the player to start playing without having to wait for the license response.

#### vod_drm_max_info_length

- **syntax**: `vod_drm_max_info_length :length`
- **default**: `4k`
- **context**: `http`, `server`, `location`

Sets the maximum length of a DRM info returned from upstream.

#### vod_drm_upstream_location

- **syntax**: `vod_drm_upstream_location :location`
- **default**: `none`
- **context**: `http`, `server`, `location`

Sets the NGINX `location` that should be used for getting the DRM info for the file.

#### vod_drm_info_cache

- **syntax**: `vod_drm_info_cache :zone_name :zone_size [:expiration] | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

Configures the size and shared memory object name of the DRM info cache.

#### vod_drm_request_uri

- **syntax**: `vod_drm_request_uri :uri`
- **default**: `$vod_suburi`
- **context**: `http`, `server`, `location`

Sets the URI of DRM info requests, the parameter value can contain [variables](#nginx-variables). In
case of multi URL, `$vod_suburi` will be the current sub URI (a separate DRM info request is issued
per sub URL).

#### vod_min_single_nalu_per_frame_segment

- **syntax**: `vod_min_single_nalu_per_frame_segment :index`
- **default**: `0`
- **context**: `http`, `server`, `location`

Sets the minimum segment index (*1-based*) that should be assumed to have a single H264 NALU per
frame. If the value is `0`, no assumption is being made on the number of NAL units per frame. This
setting only affects DASH configurations that have DRM enabled.

When transcoding videos using `libx264`, by default, all frames have a single NAL unit, except the
first frame that contains an additional NALU with the `libx264` copyright information. Setting this
parameter to a value greater than `0` can provide a significant performance improvement, since the
layout of the segment can be calculated in advance, allowing the module to:

- Output segment buffers as they are generated (it doesn't have to wait for the whole segment to
  complete).
- Avoid frame processing for requests that do not need the segment data (e.g. `HEAD`, `Range 0-0`).

### Configuration directives - DASH

#### vod_dash_absolute_manifest_urls

- **syntax**: `vod_dash_absolute_manifest_urls on | off`
- **default**: `on`
- **context**: `http`, `server`, `location`

When enabled the server returns absolute URLs in MPD requests.

#### vod_dash_manifest_file_name_prefix

- **syntax**: `vod_dash_manifest_file_name_prefix :name`
- **default**: `manifest`
- **context**: `http`, `server`, `location`

The name of the MPD file (an `.mpd` extension is implied).

#### vod_dash_profiles

- **syntax**: `vod_dash_profiles :profiles`
- **default**: `urn:mpeg:dash:profile:isoff-main:2011`
- **context**: `http`, `server`, `location`

Sets the profiles that are returned in the MPD tag in manifest responses.

#### vod_dash_init_file_name_prefix

- **syntax**: `vod_dash_init_file_name_prefix :name`
- **default**: `init`
- **context**: `http`, `server`, `location`

The name of the MP4 initialization file (an `.mp4` extension is implied).

#### vod_dash_fragment_file_name_prefix

- **syntax**: `vod_dash_fragment_file_name_prefix :name`
- **default**: `fragment`
- **context**: `http`, `server`, `location`

The name of the fragment files (an `.m4s` extension is implied).

#### vod_dash_manifest_format

- **syntax**: `vod_dash_manifest_format segmentlist | segmenttemplate | segmenttimeline`
- **default**: `segmenttimeline`
- **context**: `http`, `server`, `location`

Sets the MPD format:

- `segmentlist` - uses `SegmentList` and `SegmentURL` tags, in this format the URL of each fragment
  is explicitly set in the MPD.
- `segmenttemplate` - uses `SegmentTemplate` reporting a single duration for all fragments.
- `segmenttimeline` - uses `SegmentTemplate` and `SegmentTimeline` to explicitly set the duration of
  the fragments.

#### vod_dash_subtitle_format

- **syntax**: `vod_dash_subtitle_format webvtt | smpte-tt`
- **default**: `webvtt`
- **context**: `http`, `server`, `location`

Sets the format of the subtitles returned in the MPD.

#### vod_dash_init_mp4_pssh

- **syntax**: `vod_dash_init_mp4_pssh on | off`
- **default**: `on`
- **context**: `http`, `server`, `location`

When enabled, the DRM `pssh` boxes are returned in the DASH `init` segment and in the manifest. When
disabled, the `pssh` boxes are returned only in the manifest.

#### vod_dash_duplicate_bitrate_threshold

- **syntax**: `vod_dash_duplicate_bitrate_threshold :threshold`
- **default**: `4096`
- **context**: `http`, `server`, `location`

The bitrate threshold for removing **identical** bitrates, streams whose bitrate differences are
less than this value will be considered identical.

#### vod_dash_use_base_url_tag

- **syntax**: `vod_dash_use_base_url_tag on | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

When enabled, a `BaseURL` tag will be used to specify the fragments and init segment base URL.
Otherwise, the `media` and `initialization` attributes under `SegmentTemplate` will contain absolute
URLs.

### Configuration directives - HLS

#### vod_hls_encryption_method

- **syntax**: `vod_hls_encryption_method none | aes-128 | sample-aes | sample-aes-ctr`
- **default**: `none`
- **context**: `http`, `server`, `location`

Sets the encryption method of HLS segments.

#### vod_hls_force_unmuxed_segments

- **syntax**: `vod_hls_force_unmuxed_segments on | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

When enabled the server returns the audio stream in separate segments than the ones used by the
video stream (using `#EXT-X-MEDIA`).

#### vod_hls_version

- **syntax**: `vod_hls_version :version`
- **default**: `4`
- **context**: `http`, `server`, `location`

Sets the version of the manifest. See
[RFC 8216 Section 7](https://datatracker.ietf.org/doc/html/rfc8216#section-7) for protocol version
compatibility.

> [!IMPORTANT]
> The mimimum version required by this module is `3`.

> [!TIP]
> For maximum compatibility, set the version to the lowest value that allows the required features
> for playback. While this is not strictly spec-compliant, most clients are lenient and will ignore
> unknown or unsupported tags. However, advanced features (such as fMP4) may not be recognized or
> used by clients unless the version is set to the minimum required for those features.

#### vod_hls_container_format

- **syntax**: `vod_hls_container_format mpegts | fmp4 | auto`
- **default**: `auto`
- **context**: `http`, `server`, `location`

Sets the container format of the HLS segments. The default behavior is to use `fmp4` for HEVC, and
`mpegts` otherwise.

> [!NOTE]
> Apple does not support HEVC over MPEG-TS.

#### vod_hls_absolute_master_urls

- **syntax**: `vod_hls_absolute_master_urls on | off`
- **default**: `on`
- **context**: `http`, `server`, `location`

When enabled the server returns absolute playlist URLs in master playlist requests.

#### vod_hls_absolute_index_urls

- **syntax**: `vod_hls_absolute_index_urls on | off`
- **default**: `on`
- **context**: `http`, `server`, `location`

When enabled the server returns absolute segment URLs in media playlist requests.

#### vod_hls_absolute_iframe_urls

- **syntax**: `vod_hls_absolute_iframe_urls on | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

When enabled the server returns absolute segment URLs in iframe playlist requests.

#### vod_hls_output_iframes_playlist

- **syntax**: `vod_hls_output_iframes_playlist on | off`
- **default**: `on`
- **context**: `http`, `server`, `location`

When disabled iframe playlists are not returned as part of master playlists.

#### vod_hls_master_file_name_prefix

- **syntax**: `vod_hls_master_file_name_prefix :name`
- **default**: `master`
- **context**: `http`, `server`, `location`

The name of the HLS master playlist file (an `.m3u8` extension is implied).

#### vod_hls_index_file_name_prefix

- **syntax**: `vod_hls_index_file_name_prefix :name`
- **default**: `index`
- **context**: `http`, `server`, `location`

The name of the HLS media playlist file (an `.m3u8` extension is implied).

#### vod_hls_iframes_file_name_prefix

- **syntax**: `vod_hls_iframes_file_name_prefix :name`
- **default**: `iframes`
- **context**: `http`, `server`, `location`

The name of the HLS I-frames playlist file (an `.m3u8` extension is implied).

#### vod_hls_segment_file_name_prefix

- **syntax**: `vod_hls_segment_file_name_prefix :name`
- **default**: `seg`
- **context**: `http`, `server`, `location`

The prefix of segment file names, the actual file name is
`seg-<index>-v<video-track-index>-a<audio-track-index>.ts`.

#### vod_hls_init_file_name_prefix

- **syntax**: `vod_hls_init_file_name_prefix :name`
- **default**: `init`
- **context**: `http`, `server`, `location`

The name of the init segment file name, only relevant when using fMP4 container.

#### vod_hls_encryption_key_file_name

- **syntax**: `vod_hls_encryption_key_file_name :name`
- **default**: `encryption.key`
- **context**: `http`, `server`, `location`

The name of the encryption key file name, only relevant when encryption method is **not** `none`.

#### vod_hls_encryption_key_uri

- **syntax**: `vod_hls_encryption_key_uri :uri`
- **default**: `none`
- **context**: `http`, `server`, `location`

Sets the value of the URI attribute of `#EXT-X-KEY`, only relevant when encryption method is **not**
`none`. The parameter value can contain [variables](#nginx-variables).

#### vod_hls_encryption_key_format

- **syntax**: `vod_hls_encryption_key_format :format`
- **default**: `none`
- **context**: `http`, `server`, `location`

Sets the value of the `KEYFORMAT` attribute of `#EXT-X-KEY`, only relevant when encryption method is
**not** `none`.

#### vod_hls_encryption_key_format_versions

- **syntax**: `vod_hls_encryption_key_format_versions :versions`
- **default**: `none`
- **context**: `http`, `server`, `location`

Sets the value of the `KEYFORMATVERSIONS` attribute of `#EXT-X-KEY`, only relevant when encryption
method is **not** `none`.

#### vod_hls_mpegts_interleave_frames

- **syntax**: `vod_hls_mpegts_interleave_frames on | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

When enabled, the HLS muxer interleaves frames of different streams (audio / video). When disabled,
on every switch between audio / video the muxer flushes the MPEG-TS packet.

#### vod_hls_mpegts_align_frames

- **syntax**: `vod_hls_mpegts_align_frames on | off`
- **default**: `on`
- **context**: `http`, `server`, `location`

When enabled, every video / audio frame is aligned to MPEG-TS packet boundary. Padding is added as
needed.

#### vod_hls_mpegts_output_id3_timestamps

- **syntax**: `vod_hls_mpegts_output_id3_timestamps on | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

When enabled, an ID3 TEXT frame is outputted in each TS segment. The content of the ID3 TEXT frame
can be set using the directive [`vod_hls_mpegts_id3_data`](#vod_hls_mpegts_id3_data).

#### vod_hls_mpegts_id3_data

- **syntax**: `vod_hls_mpegts_id3_data :data`
- **default**: `{"timestamp":$vod_segment_time,"sequenceId":"$vod_sequence_id"}`
- **context**: `http`, `server`, `location`

Sets the data of the ID3 TEXT frame written in each TS segment when
[`vod_hls_mpegts_output_id3_timestamps`](#vod_hls_mpegts_output_id3_timestamps) is `on`. By default
the ID3 frames contains a JSON object like `{"timestamp":1459779115000,"sequenceId":"1"}`:

- `timestamp` - an absolute time measured in milliseconds since the `epoch`
  (`unix_timestamp x 1000`).
- `sequenceId` - the ID field of the [sequence](#sequence) object, as specified in the JSON mapping.
  The field is omitted when the sequence ID is empty (not set in the JSON mapping). The parameter
  value can contain [variables](#nginx-variables).

#### vod_hls_mpegts_align_pts

- **syntax**: `vod_hls_mpegts_align_pts on | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

When enabled, the module will shift back the DTS by the PTS delay of the initial frame. This can
help keep the PTS aligned across multiple renditions.

#### vod_hls_encryption_output_iv

- **syntax**: `vod_hls_encryption_output_iv on | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

When enabled, the module outputs the `IV` attribute in returned `#EXT-X-KEY` tags.

### Configuration directives - Thumbnail capture

#### vod_thumb_file_name_prefix

- **syntax**: `vod_thumb_file_name_prefix :name`
- **default**: `thumb`
- **context**: `http`, `server`, `location`

The name of the thumbnail file (a `.jpg` extension is implied).

#### vod_thumb_accurate_positioning

- **syntax**: `vod_thumb_accurate_positioning on | off`
- **default**: `on`
- **context**: `http`, `server`, `location`

When enabled, the module grabs the frame that is closest to the requested offset. When disabled, the
module uses the keyframe that is closest to the requested offset. Setting this parameter to `off`
can result in faster thumbnail capture, since the module always decodes a single video frame per
request.

#### vod_gop_look_behind

- **syntax**: `vod_gop_look_behind :millis`
- **default**: `10000`
- **context**: `http`, `server`, `location`

Sets the interval (in milliseconds) before the thumbnail offset that should be loaded. This setting
should be set to the **maximum** GOP size, setting it to a lower value may result in capture
failure.

> [!NOTE]
> The metadata of all frames between `offset - vod_gop_look_behind` and
> `offset + vod_gop_look_ahead` are loaded, however only the frames of the *minimum* GOP containing
> `offset` will be read and decoded.

#### vod_gop_look_ahead

- **syntax**: `vod_gop_look_ahead :millis`
- **default**: `1000`
- **context**: `http`, `server`, `location`

Sets the interval (in milliseconds) after the thumbnail offset that should be loaded.

### Configuration directives - Volume map

#### vod_volume_map_file_name_prefix

- **syntax**: `vod_volume_map_file_name_prefix :name`
- **default**: `volume_map`
- **context**: `http`, `server`, `location`

The name of the volume map file (a `.csv` extension is implied).

#### vod_volume_map_interval

- **syntax**: `vod_volume_map_interval :millis`
- **default**: `1000`
- **context**: `http`, `server`, `location`

Sets the interval or resolution (in milliseconds) of the volume map.

### Configuration directives - Miscellaneous

#### vod_ignore_edit_list

- **syntax**: `vod_ignore_edit_list on | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

When enabled, the module ignores any edit lists (`elst`) in the MP4 file.

#### vod_parse_hdlr_name

- **syntax**: `vod_parse_hdlr_name on | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

When enabled, the module parses the `name` field of the `hdlr` MP4 atom and uses it as the stream
label.

#### vod_parse_udta_name

- **syntax**: `vod_parse_udta_name on | off`
- **default**: `off`
- **context**: `http`, `server`, `location`

When enabled, the module parses the `name` atom child of the `udta` MP4 atom and uses it as the
stream label.

### NGINX variables

In addition to the built-in [NGINX variables](https://nginx.org/en/docs/varindex.html), this module
adds the following ones:

- `$vod_suburi` - the current sub URI. For example, the value of `$vod_suburi` would be
  `http://<domain>/<location>/<prefix><middle1><suffix>/<filename>` when processing the first URI of
  `http://<domain>/<location>/<prefix>,<middle1>,<middle2>,<middle3>,<suffix>.urlset/<filename>`.
- `$vod_filepath` - the file path of current sub URI in `local` and `mapped` modes. Same as
  `$vod_suburi` in `remote` mode.
- `$vod_set_id` - contains the ID of the set.
- `$vod_sequence_id` - contains the ID of the current sequence, if no ID was specified in the JSON
  mapping this variable will be the same as `$vod_suburi`.
- `$vod_clip_id` - the ID of the current clip. The variable is set during the following phases:

  - Mapping of [dynamic clips](#dynamic-clip) to [concat clips](#concat-clip).
  - Mapping of [source clip](#source-clip) to paths.

- `$vod_notification_id` - the ID of the current notification. This variable is only relevant on
  `vod_notification_uri`.
- `$vod_dynamic_mapping` - a serialized representation of the mapping of
  [dynamic clips](#dynamic-clip) to [concat clips](#concat-clip).
- `$vod_request_params` - a serialized representation of the request params, e.g. `12-f2-v1-a1`. The
  variable contains:

  - The segment index (for a segment request).
  - The sequence index.
  - A selection of audio and video tracks.

- `$vod_status` - the internal error code of the module, provides a more fine grained classification
  of errors than HTTP status. The following values are defined:

  - `BAD_REQUEST` - the request is invalid, e.g. `clipFrom` is larger than the video duration.
  - `NO_STREAMS` - an invalid segment index was requested.
  - `EMPTY_MAPPING` - the mapping response is empty.
  - `BAD_MAPPING` - the JSON mapping is invalid, e.g. the `sequences` element is missing.
  - `BAD_DATA` - the video file is corrupt.
  - `EXPIRED` - the current server time is larger than `expirationTime`.
  - `ALLOC_FAILED` - the module failed to allocate memory.
  - `UNEXPECTED` - a scenario that is not supposed to happen, most likely a bug in the module.

- `$vod_segment_time` - for segment requests, contains the absolute timestamp of the first frame in
  the segment, measured in milliseconds since the `epoch` (`unix_timestamp x 1000`).
- `$vod_segment_duration` - for segment requests, contains the duration of the segment in
  milliseconds.
- `$vod_frames_bytes_read` - for segment requests, total number of bytes read while processing media
  frames.

> [!NOTE]
> Configuration directives that can accept variables are explicitly marked as such.

### Sample configurations

#### Local configuration

```conf
http {
  upstream fallback {
    server fallback.example.com:80;
  }

  server {
    # vod settings
    vod_mode local;
    vod_fallback_upstream_location /fallback;
    vod_last_modified 'Wed, 09 Apr 2025 14:35:00 GMT';
    vod_last_modified_types *;

    # vod caches
    vod_metadata_cache metadata_cache 512m;
    vod_response_cache response_cache 128m;

    # gzip manifests
    gzip on;
    gzip_types application/vnd.apple.mpegurl;

    # file handle caching / aio
    open_file_cache          max=1000 inactive=5m;
    open_file_cache_valid    2m;
    open_file_cache_min_uses 1;
    open_file_cache_errors   on;
    aio on;

    location ^~ /fallback/ {
      internal;
      proxy_pass http://fallback/;
      proxy_set_header Host $http_host;
    }

    location /content/ {
      root /web/;

      vod hls;

      add_header Access-Control-Allow-Methods 'GET, HEAD, OPTIONS';
      add_header Access-Control-Allow-Headers 'Origin, Range';
      add_header Access-Control-Allow-Origin '*';
      add_header Access-Control-Expose-Headers 'Server, Range, Content-Length, Content-Range';
      add_header Access-Control-Max-Age '86400';

      expires 1y;
    }
  }
}
```

#### Mapped configuration

```conf
http {
  upstream api {
    server api.example.com:80;
  }

  upstream fallback {
    server fallback.example.com:80;
  }

  server {
    # vod settings
    vod_mode mapped;
    vod_upstream_location /map;
    vod_upstream_extra_args 'pathOnly=1';
    vod_fallback_upstream_location /fallback;
    vod_last_modified 'Wed, 09 Apr 2025 14:35:00 GMT';
    vod_last_modified_types *;

    # vod caches
    vod_metadata_cache metadata_cache 512m;
    vod_response_cache response_cache 128m;
    vod_mapping_cache mapping_cache 5m;

    # gzip manifests
    gzip on;
    gzip_types application/vnd.apple.mpegurl;

    # file handle caching / aio
    open_file_cache          max=1000 inactive=5m;
    open_file_cache_valid    2m;
    open_file_cache_min_uses 1;
    open_file_cache_errors   on;
    aio on;

    location ^~ /fallback/ {
      internal;
      proxy_pass http://fallback/;
      proxy_set_header Host $http_host;
    }

    location ^~ /map/ {
      internal;
      proxy_pass http://api/;
      proxy_set_header Host $http_host;
    }

    location ~ ^/hls/ {
      # encrypted hls
      vod hls;

      vod_secret_key 'secret-$vod_filepath';
      vod_hls_encryption_method aes-128;

      add_header Access-Control-Allow-Methods 'GET, HEAD, OPTIONS';
      add_header Access-Control-Allow-Headers 'Origin, Range';
      add_header Access-Control-Allow-Origin '*';
      add_header Access-Control-Expose-Headers 'Server, Range, Content-Length, Content-Range';
      add_header Access-Control-Max-Age '86400';

      expires 1y;
    }
  }
}
```

#### Mapped + Remote configuration

```conf
http {
  upstream api {
    server api.example.com:80;
  }

  server {
    # vod settings
    vod_mode mapped;
    vod_upstream_location /map;
    vod_remote_upstream_location /proxy;
    vod_upstream_extra_args 'pathOnly=1';
    vod_last_modified 'Wed, 09 Apr 2025 14:35:00 GMT';
    vod_last_modified_types *;

    # vod caches
    vod_metadata_cache metadata_cache 512m;
    vod_response_cache response_cache 128m;
    vod_mapping_cache mapping_cache 5m;

    # gzip manifests
    gzip on;
    gzip_types application/vnd.apple.mpegurl;

    # file handle caching / aio
    open_file_cache    max=1000 inactive=5m;
    open_file_cache_valid    2m;
    open_file_cache_min_uses 1;
    open_file_cache_errors   on;
    aio on;

    location ^~ /map/ {
      internal;
      proxy_pass http://api/;
      proxy_set_header Host $http_host;
    }

    location ~ ^/proxy/(?<protocol>[^/]+)/(.*) {
      internal;
      proxy_pass $protocol://$1;
      resolver 8.8.8.8;
    }

    location ~ ^/hls/ {
      vod hls;

      add_header Access-Control-Allow-Methods 'GET, HEAD, OPTIONS';
      add_header Access-Control-Allow-Headers 'Origin, Range';
      add_header Access-Control-Allow-Origin '*';
      add_header Access-Control-Expose-Headers 'Server, Range, Content-Length, Content-Range';
      add_header Access-Control-Max-Age '86400';

      expires 1y;
    }
  }
}
```

Set it up so that `http://api.example.com:80/sample.json` returns the following:

```json
{
  "sequences": [
    {
      "clips": [
        {
          "type": "source",
          "path": "/http/commondatastorage.googleapis.com/gtv-videos-bucket/sample/TearsOfSteel.mp4"
        }
      ]
    }
  ]
}
```

And use this stream URL - `http://<domain>/hls/sample.json/master.m3u8`.

#### Remote configuration

```conf
http {
  upstream storage {
    server storage.example.com:80;
  }

  server {
    # vod settings
    vod_mode remote;
    vod_upstream_location /remote;
    vod_last_modified 'Wed, 09 Apr 2025 14:35:00 GMT';
    vod_last_modified_types *;

    # vod caches
    vod_metadata_cache metadata_cache 512m;
    vod_response_cache response_cache 128m;

    # gzip manifests
    gzip on;
    gzip_types application/vnd.apple.mpegurl;

    location ^~ /remote/ {
      internal;
      proxy_pass http://storage/;
      proxy_set_header Host $http_host;
    }

    location ~ ^/hls/ {
      vod hls;

      add_header Access-Control-Allow-Methods 'GET, HEAD, OPTIONS';
      add_header Access-Control-Allow-Headers 'Origin, Range';
      add_header Access-Control-Allow-Origin '*';
      add_header Access-Control-Expose-Headers 'Server, Range, Content-Length, Content-Range';
      add_header Access-Control-Max-Age '86400';

      expires 1y;
    }
  }
}
```
