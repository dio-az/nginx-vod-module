# Progressive multi-clip muxer - unit tests

These exercise the algorithmic core of the non-fragmented multi-clip MP4 muxer in isolation, with no
nginx runtime. They fabricate a `media_set` by hand and assert the emitted `stbl` bytes, the
chunk-offset math, and the order of the streamed `mdat` bytes.

## Build & run (against a configured nginx tree)

```sh
# from a built nginx source tree that included this module via --add-module
NGX=/path/to/nginx-1.24.0
MOD=/path/to/nginx-vod-module
INC="-I $NGX/src/core -I $NGX/src/event -I $NGX/src/event/modules -I $NGX/src/os/unix -I $NGX/objs -I $MOD"
OBJ="$NGX/objs/addon/mp4/mp4_progressive_builder.o"

for t in test_stbl_builder test_offsets test_mdat_writer; do
  cc -O -W -Wall -Wno-unused-parameter $INC $MOD/test/progressive/$t.c \
     $MOD/test/progressive/stubs.c $OBJ -o /tmp/$t && /tmp/$t
done
```

`stubs.c` stands in for the nginx-pool allocator and the moov assembler's dependencies, which the
success paths under test never call. All three tests link against the module's own object file, so
they verify exactly the code that ships.

## What they cover

- `test_stbl_builder.c` - 2 clips x 2 tracks (video+audio): stts run-length, stsz table,
  **stss sample numbers cumulative across clips**, stss omission for all-keyframe audio, ctts
  omission when all pts_delay are 0, filtered_tracks[c*ttc+t] indexing, and size==written agreement.
- `test_offsets.c` - total data size and per-track chunk offsets, including the 64-bit (co64) range.
- `test_mdat_writer.c` - the async mdat streaming writer over an in-memory frames_source:
  **bytes emitted in track-outer / clip-inner order** (matching the single per-track chunk offsets),
  every frame emitted once, and total size == sum of frame sizes.

## Not covered here (needs the running module / lab)

- The moov assembler (`mp4_progressive_build_moov` / `mp4_progressive_build_header`) calls
  `mp4_init_segment_build_ex`, which allocates from an nginx pool - so it is compile-verified only.
- The async resume path of the mdat writer (VOD_AGAIN yields between cache reads) is structural: the
  loop mirrors `mp4_fragment_frame_writer_process`. The in-memory source returns each frame in one
  read, so the ordering and completion are tested but the yield/resume is exercised only in the lab.
- End-to-end validation (a real source, the full ftyp+moov+mdat produced, the file opened in an
  editor) needs a running nginx: configure `vod none` + `vod_mode mapped`, return a static multi-clip
  mapping JSON (a `sequences[0].clips[]` array with per-clip `clipFrom` plus a `durations` array),
  request the base mp4 URL, and check the result with `ffprobe` (one moov, one mdat, no moof) and a
  full decode.
