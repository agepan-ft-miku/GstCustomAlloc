#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

/* 初期実装では codec driver 側の allocation layout を固定する。 */
#define CODEC_DMABUF_MAX_WIDTH        800
#define CODEC_DMABUF_MAX_HEIGHT       480
#define CODEC_DMABUF_FORMAT           GST_VIDEO_FORMAT_NV12
#define CODEC_DMABUF_N_PLANES         2

#define CODEC_DMABUF_STRIDE_ALIGN     32
#define CODEC_DMABUF_SIZE_ALIGN       32

#define CODEC_DMABUF_MIN_BUFFERS      2
#define CODEC_DMABUF_MAX_BUFFERS      4

#define CODEC_DMABUF_Y_PLANE          0
#define CODEC_DMABUF_UV_PLANE         1

#define CODEC_DMABUF_Y_OFFSET         0
#define CODEC_DMABUF_Y_STRIDE         800
#define CODEC_DMABUF_Y_HEIGHT         480

#define CODEC_DMABUF_UV_OFFSET        384000
#define CODEC_DMABUF_UV_STRIDE        800
#define CODEC_DMABUF_UV_HEIGHT        240

/* 実画像が小さい場合でも driver には最大 frame size で確保を依頼する。 */
#define CODEC_DMABUF_ALLOCATION_SIZE  576000

#define CODEC_DMABUF_DEFAULT_DEVICE_PATH "/dev/codec-dmabuf"
#define CODEC_DMABUF_DRIVER_ALLOC_FLAG 0

G_END_DECLS
