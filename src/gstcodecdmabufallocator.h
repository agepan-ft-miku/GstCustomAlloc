#pragma once

#include "gstcodecdmabufdriver.h"

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_CODEC_DMABUF_ALLOCATOR (gst_codec_dmabuf_allocator_get_type ())
G_DECLARE_FINAL_TYPE (GstCodecDmabufAllocator, gst_codec_dmabuf_allocator,
    GST, CODEC_DMABUF_ALLOCATOR, GstAllocator)

GstAllocator * gst_codec_dmabuf_allocator_new (const gchar *device_path);
void gst_codec_dmabuf_allocator_set_driver_ops (
    GstCodecDmabufAllocator *allocator,
    const CodecDmabufDriverOps *ops);
guint64 gst_codec_dmabuf_allocator_get_id (
    GstCodecDmabufAllocator *allocator);

GstBufferPool * gst_codec_dmabuf_standard_pool_new (
    GstCodecDmabufAllocator *allocator,
    GstCaps *caps,
    guint min_buffers,
    guint max_buffers);

void gst_codec_dmabuf_allocator_release_driver_memory (
    GstCodecDmabufAllocator *allocator,
    int device_fd,
    gpointer addr,
    gsize size,
    int exported_fd,
    gboolean export_active);

G_END_DECLS
