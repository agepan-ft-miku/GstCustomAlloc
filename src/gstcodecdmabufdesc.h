#pragma once

#include "gstcodecdmabufmemory.h"

G_BEGIN_DECLS

typedef struct _CodecNv12BufferDesc {
  int dmabuf_fd;

  guint width;
  guint height;

  gsize y_offset;
  gint y_stride;

  gsize uv_offset;
  gint uv_stride;

  gsize size;
  /* descriptor 利用中に backing GstBuffer が解放されないよう保持する。 */
  GstBuffer *buffer_ref;
} CodecNv12BufferDesc;

/**
 * gst_codec_dmabuf_buffer_to_nv12_desc:
 * @buffer: descriptor 化する codec allocator 由来 #GstBuffer。
 * @width: visible width。
 * @height: visible height。
 * @out_desc: (out caller-allocates): 生成した descriptor の出力先。
 *
 * codec allocator 由来の DMABUF buffer から NV12 descriptor を生成する。
 * descriptor 内の fd は borrowed fd を dup() したもので、利用中は @buffer の ref も保持する。
 *
 * Returns: descriptor を生成できた場合は %TRUE。
 */
gboolean gst_codec_dmabuf_buffer_to_nv12_desc (
    GstBuffer *buffer,
    guint width,
    guint height,
    CodecNv12BufferDesc *out_desc);

/**
 * gst_codec_dmabuf_nv12_desc_clear:
 * @desc: clear する #CodecNv12BufferDesc。
 *
 * descriptor が所有する duplicated fd を close し、保持している #GstBuffer ref を解放する。
 */
void gst_codec_dmabuf_nv12_desc_clear (CodecNv12BufferDesc *desc);

G_END_DECLS
