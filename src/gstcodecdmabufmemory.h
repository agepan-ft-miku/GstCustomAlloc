#pragma once

#include "gstcodecdmabufallocator.h"
#include "gstcodecdmabufconfig.h"

#include <gst/allocators/gstdmabuf.h>

G_BEGIN_DECLS

typedef enum {
  GST_CODEC_DMABUF_MEMORY_KIND_NONE = 0,
  GST_CODEC_DMABUF_MEMORY_KIND_CODEC_DMABUF,
  GST_CODEC_DMABUF_MEMORY_KIND_OTHER_DMABUF,
  GST_CODEC_DMABUF_MEMORY_KIND_SYSTEM
} GstCodecDmabufMemoryKind;

typedef struct _CodecDmabufMemory {
  GstCodecDmabufAllocator *allocator;

  int device_fd;
  int exported_fd;
  gpointer addr;
  gsize size;

  gboolean export_active;
  guint64 allocator_id;

  guint width;
  guint height;
  GstVideoFormat format;

  guint n_planes;
  gsize offsets[GST_VIDEO_MAX_PLANES];
  gint strides[GST_VIDEO_MAX_PLANES];
} CodecDmabufMemory;

CodecDmabufMemory * gst_codec_dmabuf_memory_new (
    GstCodecDmabufAllocator *allocator,
    int device_fd,
    int exported_fd,
    gpointer addr,
    gsize size);
void gst_codec_dmabuf_memory_free (CodecDmabufMemory *memory);

gboolean gst_codec_dmabuf_memory_attach (
    GstMemory *memory,
    CodecDmabufMemory *dmabuf_memory);
CodecDmabufMemory * gst_codec_dmabuf_memory_get (GstMemory *memory);

gboolean gst_codec_dmabuf_buffer_is_dmabuf (GstBuffer *buffer);
GstCodecDmabufMemoryKind gst_codec_dmabuf_buffer_get_memory_kind (
    GstBuffer *buffer);
const gchar * gst_codec_dmabuf_memory_kind_to_string (
    GstCodecDmabufMemoryKind kind);
gboolean gst_codec_dmabuf_buffer_is_supported_input (
    GstBuffer *buffer,
    GstCodecDmabufAllocator *allocator);

G_END_DECLS
