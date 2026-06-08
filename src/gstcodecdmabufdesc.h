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
  GstBuffer *buffer_ref;
} CodecNv12BufferDesc;

gboolean gst_codec_dmabuf_buffer_to_nv12_desc (
    GstBuffer *buffer,
    guint width,
    guint height,
    CodecNv12BufferDesc *out_desc);
void gst_codec_dmabuf_nv12_desc_clear (CodecNv12BufferDesc *desc);

G_END_DECLS
