# H.264 GStreamer Plugin Design

## 1. Overview

This plugin provides two GStreamer elements for Linux.

- `cpuh264enc`: encodes NV12 raw video to H.264
- `cpuh264dec`: decodes H.264 to NV12 raw video

The plugin is intended as an educational sample. It does not use FFmpeg. H.264 coding itself is delegated to OpenH264, while the plugin keeps the surrounding data path explicit:

- GStreamer caps negotiation
- NV12 raw frame mapping
- DMA-BUF input/output handling
- NV12/I420 color layout conversion
- Annex-B H.264 access unit handling
- replaceable codec backend boundary

The implementation intentionally supports only a narrow format set to keep the code readable.

## 2. Supported Formats

Raw video caps:

- `format=NV12`
- `width=320..800`
- `height=128..480`
- `framerate=30/1`

H.264 caps:

- `stream-format=byte-stream`
- `alignment=au`

DMA-BUF support:

- encoder sink accepts `video/x-raw(memory:DMABuf)`
- decoder src can output `video/x-raw(memory:DMABuf)`
- CPU processing requires mapped CPU access to the buffer memory

## 3. Source Structure

| File | Role |
| --- | --- |
| `src/gstcodecplugin.c` | GStreamer plugin entry point. Registers both elements. |
| `src/gstcpuh264enc.c` | `cpuh264enc` element implementation. |
| `src/gstcpuh264dec.c` | `cpuh264dec` element implementation. |
| `src/gstcodecbackend.h` | Minimal codec backend interface. |
| `src/gstcodecbackend.c` | Shared backend object/free helpers. |
| `src/gstopenh264backend.cpp` | OpenH264-based encode/decode backend. |
| `src/gstcodeccaps.h` | Supported caps constants and fixed format limits. |
| `src/gstcodeccaps.c` | Caps validation helpers. |
| `src/gstdmabufutils.c` | DMA-BUF allocation and peer caps query helpers. |
| `src/gstcodecdmabufallocator.c` | Driver-backed custom `GstAllocator` for the hardware-codec DMABUF path. |
| `src/gstcodecdmabufdriver.c` | Codec memory driver ops wrapper and fake-test boundary. |
| `src/gstcodecdmabufmemory.c` | `GstDmaBufMemory` qdata, origin checks, and memory-kind helpers. |
| `src/gstcodecdmabufdesc.c` | NV12 descriptor creation from codec-owned DMABUF memory. |
| `tests/test_codec_caps.c` | Unit tests for supported caps checks. |
| `docs/h264_sample_notes.md` | Educational notes about raw image and H.264 access unit handling. |
| `docs/gstreamer_push_pull_considerations.md` | Push/pull scheduling considerations for codec plugin design. |
| `docs/dmabuf_allocator_design.md` | Planned hardware-codec DMABUF allocator design. |

## 4. Module Responsibilities

### Plugin Registration

`gstcodecplugin.c` contains `plugin_init()`.

It registers:

- `cpuh264enc`
- `cpuh264dec`

No codec logic exists in this file.

### Encoder Element

`gstcpuh264enc.c` subclasses `GstVideoEncoder`.

Responsibilities:

- declare sink/src pad caps
- expose `bitrate` property
- validate incoming raw video format
- create and open codec backend
- map input `GstBuffer` as `GstVideoFrame`
- pass two-plane NV12 frame pointers to backend
- convert backend packet to output `GstBuffer`
- set timestamp, duration, and delta-unit flag

It does not perform H.264 coding directly.

### Decoder Element

`gstcpuh264dec.c` subclasses `GstVideoDecoder`.

Responsibilities:

- declare sink/src pad caps
- validate incoming H.264 stream caps
- create and open codec backend
- pass one H.264 access unit buffer to backend
- validate decoded frame size
- negotiate NV12 output caps
- choose DMA-BUF output when downstream supports it and allocation succeeds
- copy backend frame data into the selected output buffer

It does not perform H.264 decoding directly.

### Codec Backend Interface

`gstcodecbackend.h` defines a deliberately small interface.

The design assumes the simplified educational data model:

- encoder input: one NV12 frame
- encoder output: zero or one H.264 access unit
- decoder input: one H.264 access unit
- decoder output: zero or one NV12 frame

Zero output is allowed because codec libraries can skip or delay frames.

### OpenH264 Backend

`gstopenh264backend.cpp` implements the backend using OpenH264.

It keeps the following transformations visible:

- `nv12_to_i420()`: converts GStreamer NV12 input to OpenH264 I420 input
- `packet_from_frame_bs_info()`: combines OpenH264 NAL/layer output into one Annex-B access unit
- `i420_to_nv12_frame()`: converts OpenH264 decoded I420 output back to NV12

This file is the best place to study the boundary between raw frames, codec API calls, and byte-stream output.

### Caps Helpers

`gstcodeccaps.*` centralizes all supported format limits.

This avoids duplicated checks in encoder and decoder code and makes supported-format tests straightforward.

### DMA-BUF Helpers

`gstdmabufutils.*` contains Linux-specific DMA-BUF support.

Responsibilities:

- query whether downstream accepts `memory:DMABuf`
- allocate a DMA-BUF from `/dev/dma_heap/system` or `/dev/dma_heap/linux,cma`
- wrap the file descriptor as `GstMemory`

DMA-BUF is used as a buffer transport mechanism. The current codec path still maps data for CPU processing.

The hardware-codec path uses a stricter allocator design described in
`docs/dmabuf_allocator_design.md`. In that design, a custom `GstAllocator` owns
driver allocation, `export_start`, `export_end`, and driver free. It returns
official `GstDmaBufMemory` with qdata that carries the driver resource lifetime.
Buffer reuse stays with a standard `GstBufferPool`; no custom pool is planned for
the initial hardware path.

## 5. Main Data Types

### `GstCodecBackendFrame`

Represents one NV12 raw frame.

Fields:

- `width`, `height`
- `data[0]`: Y plane
- `data[1]`: interleaved UV plane
- `stride[0]`, `stride[1]`
- `pts`, `duration`

### `GstCodecBackendPacket`

Represents one H.264 access unit.

Fields:

- `data`
- `size`
- `pts`
- `duration`
- `keyframe`

The packet is expected to be Annex-B byte-stream data.

### `GstCodecBackendVTable`

Backend operations:

- `free`
- `open_encoder`
- `encode`
- `open_decoder`
- `decode`

This is the hardware-codec replacement boundary.

## 6. Function List

### Plugin

| Function | Purpose |
| --- | --- |
| `plugin_init()` | Registers encoder and decoder elements. |
| `gst_cpu_h264_enc_register()` | Registers `cpuh264enc`. |
| `gst_cpu_h264_dec_register()` | Registers `cpuh264dec`. |

### Encoder

| Function | Purpose |
| --- | --- |
| `gst_cpu_h264_enc_set_property()` | Handles `bitrate` property update. |
| `gst_cpu_h264_enc_get_property()` | Reads `bitrate` property. |
| `gst_cpu_h264_enc_stop()` | Releases backend on stop. |
| `gst_cpu_h264_enc_set_format()` | Validates raw caps, creates backend, opens encoder, sets H.264 output caps. |
| `packet_to_buffer()` | Copies backend packet bytes into a `GstBuffer`. |
| `gst_cpu_h264_enc_handle_frame()` | Maps NV12 frame, calls backend encode, finishes encoded frame. |
| `gst_cpu_h264_enc_class_init()` | Registers pad templates, metadata, properties, vfuncs. |
| `gst_cpu_h264_enc_init()` | Sets default bitrate. |

### Decoder

| Function | Purpose |
| --- | --- |
| `gst_cpu_h264_dec_stop()` | Releases backend and codec states. |
| `gst_cpu_h264_dec_set_format()` | Validates H.264 caps, creates backend, opens decoder. |
| `ensure_output_state()` | Validates decoded size, negotiates NV12 output caps, selects DMA-BUF output when possible. |
| `allocate_output_buffer()` | Allocates DMA-BUF or normal output buffer. |
| `copy_backend_frame_to_buffer()` | Copies backend NV12 frame into output `GstBuffer`. |
| `finish_one_frame()` | Creates final decoded output and finishes the frame. |
| `gst_cpu_h264_dec_handle_frame()` | Calls backend decode and handles zero/one decoded output frame. |
| `gst_cpu_h264_dec_class_init()` | Registers pad templates, metadata, vfuncs. |
| `gst_cpu_h264_dec_init()` | Element instance initialization. |

### Backend Helpers

| Function | Purpose |
| --- | --- |
| `gst_openh264_backend_new()` | Creates OpenH264 backend instance. |
| `gst_codec_backend_free()` | Calls backend vtable `free`. |
| `gst_codec_backend_packet_free()` | Releases packet memory. |
| `gst_codec_backend_frame_free()` | Releases frame plane memory. |

### OpenH264 Backend

| Function | Purpose |
| --- | --- |
| `copy_plane()` | Copies strided image plane data row by row. |
| `nv12_to_i420()` | Converts NV12 frame to I420 for OpenH264 encoder. |
| `packet_from_frame_bs_info()` | Converts OpenH264 encoded layer/NAL output into one H.264 access unit packet. |
| `i420_to_nv12_frame()` | Converts decoded I420 frame to NV12 backend frame. |
| `open_encoder()` | Creates and configures OpenH264 encoder. |
| `encode_frame()` | Encodes one NV12 frame into zero/one H.264 packet. |
| `open_decoder()` | Creates and configures OpenH264 decoder. |
| `decode_buffer()` | Decodes one H.264 access unit into zero/one NV12 frame. |
| `backend_free()` | Releases OpenH264 encoder/decoder. |

### Caps Helpers

| Function | Purpose |
| --- | --- |
| `gst_codec_video_info_is_supported()` | Checks NV12, size range, and framerate. |
| `gst_codec_h264_caps_are_supported()` | Checks `byte-stream` and `au`. |
| `gst_codec_video_info_set_nv12_fixed()` | Initializes fixed NV12 output `GstVideoInfo`. |

### DMA-BUF Helpers

| Function | Purpose |
| --- | --- |
| `gst_codec_buffer_new_dmabuf()` | Allocates DMA-BUF and wraps it in `GstBuffer`. |
| `gst_codec_dmabuf_can_allocate()` | Checks whether DMA-BUF allocation works. |
| `gst_codec_pad_peer_accepts_dmabuf()` | Checks downstream caps for `memory:DMABuf`. |

## 7. Encode Flow

```text
upstream raw NV12 buffer
  |
  v
cpuh264enc sink pad caps negotiation
  |
  v
gst_cpu_h264_enc_set_format()
  - validate NV12 / size / 30fps
  - create OpenH264 backend
  - open encoder
  - set output caps: video/x-h264, byte-stream, au
  |
  v
gst_cpu_h264_enc_handle_frame()
  - map GstBuffer as GstVideoFrame
  - create GstCodecBackendFrame with Y and UV plane pointers
  - backend->encode()
  |
  v
OpenH264 backend
  - NV12 -> I420
  - OpenH264 EncodeFrame()
  - collect NAL units into one Annex-B access unit
  |
  v
packet_to_buffer()
  - create output GstBuffer
  - copy H.264 access unit bytes
  - set PTS / duration / delta-unit flag
  |
  v
downstream H.264 byte-stream AU
```

## 8. Decode Flow

```text
upstream H.264 byte-stream AU buffer
  |
  v
cpuh264dec sink pad caps negotiation
  |
  v
gst_cpu_h264_dec_set_format()
  - validate byte-stream / au
  - create OpenH264 backend
  - open decoder
  |
  v
gst_cpu_h264_dec_handle_frame()
  - pass input GstBuffer to backend->decode()
  |
  v
OpenH264 backend
  - OpenH264 DecodeFrameNoDelay()
  - I420 -> NV12
  - return one GstCodecBackendFrame, or no frame
  |
  v
ensure_output_state()
  - validate decoded size
  - negotiate NV12 output caps
  - enable memory:DMABuf if downstream accepts it and allocation works
  |
  v
allocate_output_buffer()
  - allocate DMA-BUF or normal memory
  |
  v
copy_backend_frame_to_buffer()
  - copy Y and UV planes into output buffer
  |
  v
downstream raw NV12 buffer
```

## 9. Design Intent

### Keep the codec boundary explicit

The element code handles GStreamer responsibilities. The backend handles codec responsibilities. This separation keeps hardware-codec replacement possible without rewriting caps negotiation or DMA-BUF logic.

### Keep the educational path narrow

The supported formats are intentionally limited. Supporting many raw formats, stream formats, and frame rates would add negotiation and conversion code that obscures the core path.

### Avoid FFmpeg

FFmpeg is intentionally not used. OpenH264 still hides the H.264 coding internals, but the surrounding mechanics are visible and small enough to study.

### Use one-frame API semantics

The backend API models the expected caps:

- one raw frame to zero/one H.264 access unit
- one H.264 access unit to zero/one raw frame

This avoids a more general queue-oriented design until the plugin needs B-frames, delayed output, or multiple access units per input.

Push/pull scheduling tradeoffs are documented separately in `docs/gstreamer_push_pull_considerations.md`.

### Treat DMA-BUF as transport, not acceleration

DMA-BUF is supported at the GStreamer buffer boundary. Encoding and decoding are still CPU operations. Therefore DMA-BUF memory must be CPU-mappable.

## 10. Testing Strategy

Current test coverage:

- `tests/test_codec_caps.c`
  - verifies accepted raw caps
  - verifies rejected raw caps
  - verifies accepted H.264 caps
  - verifies rejected H.264 caps

Recommended additional tests on Linux:

- `gst-inspect-1.0 cpuh264enc`
- `gst-inspect-1.0 cpuh264dec`
- encode pipeline with `videotestsrc`
- decode pipeline with `h264parse`
- DMA-BUF downstream negotiation test
- invalid caps negotiation tests

Example:

```sh
meson setup build
ninja -C build
meson test -C build
GST_PLUGIN_PATH="$PWD/build/src" gst-inspect-1.0 cpuh264enc
GST_PLUGIN_PATH="$PWD/build/src" gst-inspect-1.0 cpuh264dec
```

## 11. Known Limitations

- Only NV12 raw video is supported.
- Only 30/1 fps is supported.
- Only H.264 Annex-B byte-stream access-unit aligned input/output is supported.
- OpenH264 performs the actual H.264 coding.
- Decoder currently models zero/one decoded frame per input access unit.
- DMA-BUF allocation depends on Linux dma-heap availability.
- No explicit parser is implemented; upstream should provide AU-aligned byte-stream H.264, typically via `h264parse`.

## 12. Hardware Backend Replacement

To replace OpenH264 with a hardware codec:

1. Implement `GstCodecBackendVTable`.
2. Provide a factory equivalent to `gst_openh264_backend_new()`.
3. Return/accept the same simplified data model:
   - NV12 raw frames
   - H.264 byte-stream access units
4. Replace the factory call in encoder and decoder elements.

The element layer should not need major changes if the hardware backend can consume and produce the same formats.
