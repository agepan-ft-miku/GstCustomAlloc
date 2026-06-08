#include "gstcodecdmabufmemory.h"

static GQuark
codec_dmabuf_memory_quark (void)
{
  return g_quark_from_static_string ("gst-codec-dmabuf-memory");
}

CodecDmabufMemory *
gst_codec_dmabuf_memory_new (GstCodecDmabufAllocator *allocator,
    int device_fd, int exported_fd, gpointer addr, gsize size)
{
  CodecDmabufMemory *memory;

  g_return_val_if_fail (GST_IS_CODEC_DMABUF_ALLOCATOR (allocator), NULL);

  memory = g_new0 (CodecDmabufMemory, 1);
  memory->allocator = g_object_ref (allocator);
  memory->device_fd = device_fd;
  memory->exported_fd = exported_fd;
  memory->addr = addr;
  memory->size = size;
  memory->export_active = TRUE;
  memory->allocator_id = gst_codec_dmabuf_allocator_get_id (allocator);
  memory->width = CODEC_DMABUF_MAX_WIDTH;
  memory->height = CODEC_DMABUF_MAX_HEIGHT;
  memory->format = CODEC_DMABUF_FORMAT;
  memory->n_planes = CODEC_DMABUF_N_PLANES;
  memory->offsets[CODEC_DMABUF_Y_PLANE] = CODEC_DMABUF_Y_OFFSET;
  memory->offsets[CODEC_DMABUF_UV_PLANE] = CODEC_DMABUF_UV_OFFSET;
  memory->strides[CODEC_DMABUF_Y_PLANE] = CODEC_DMABUF_Y_STRIDE;
  memory->strides[CODEC_DMABUF_UV_PLANE] = CODEC_DMABUF_UV_STRIDE;

  return memory;
}

void
gst_codec_dmabuf_memory_free (CodecDmabufMemory *memory)
{
  if (memory == NULL)
    return;

  if (memory->allocator != NULL) {
    gst_codec_dmabuf_allocator_release_driver_memory (memory->allocator,
        memory->device_fd, memory->addr, memory->size, memory->exported_fd,
        memory->export_active);
    gst_object_unref (memory->allocator);
  }

  g_free (memory);
}

gboolean
gst_codec_dmabuf_memory_attach (GstMemory *memory,
    CodecDmabufMemory *dmabuf_memory)
{
  g_return_val_if_fail (memory != NULL, FALSE);
  g_return_val_if_fail (dmabuf_memory != NULL, FALSE);

  if (gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (memory),
          codec_dmabuf_memory_quark ()) != NULL)
    return FALSE;

  gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (memory),
      codec_dmabuf_memory_quark (), dmabuf_memory,
      (GDestroyNotify) gst_codec_dmabuf_memory_free);
  return TRUE;
}

CodecDmabufMemory *
gst_codec_dmabuf_memory_get (GstMemory *memory)
{
  if (memory == NULL)
    return NULL;

  return gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (memory),
      codec_dmabuf_memory_quark ());
}

gboolean
gst_codec_dmabuf_buffer_is_dmabuf (GstBuffer *buffer)
{
  GstMemory *memory;

  if (buffer == NULL || gst_buffer_n_memory (buffer) != 1)
    return FALSE;

  memory = gst_buffer_peek_memory (buffer, 0);
  return gst_is_dmabuf_memory (memory);
}

GstCodecDmabufMemoryKind
gst_codec_dmabuf_buffer_get_memory_kind (GstBuffer *buffer)
{
  GstMemory *memory;

  if (buffer == NULL || gst_buffer_n_memory (buffer) != 1)
    return GST_CODEC_DMABUF_MEMORY_KIND_NONE;

  memory = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_dmabuf_memory (memory))
    return GST_CODEC_DMABUF_MEMORY_KIND_SYSTEM;

  if (gst_codec_dmabuf_memory_get (memory) == NULL)
    return GST_CODEC_DMABUF_MEMORY_KIND_OTHER_DMABUF;

  return GST_CODEC_DMABUF_MEMORY_KIND_CODEC_DMABUF;
}

const gchar *
gst_codec_dmabuf_memory_kind_to_string (GstCodecDmabufMemoryKind kind)
{
  switch (kind) {
    case GST_CODEC_DMABUF_MEMORY_KIND_CODEC_DMABUF:
      return "codec-dmabuf";
    case GST_CODEC_DMABUF_MEMORY_KIND_OTHER_DMABUF:
      return "other-dmabuf";
    case GST_CODEC_DMABUF_MEMORY_KIND_SYSTEM:
      return "system";
    case GST_CODEC_DMABUF_MEMORY_KIND_NONE:
    default:
      return "none";
  }
}

gboolean
gst_codec_dmabuf_buffer_is_supported_input (GstBuffer *buffer,
    GstCodecDmabufAllocator *allocator)
{
  GstMemory *memory;
  CodecDmabufMemory *dmabuf_memory;

  g_return_val_if_fail (GST_IS_CODEC_DMABUF_ALLOCATOR (allocator), FALSE);

  if (gst_codec_dmabuf_buffer_get_memory_kind (buffer) !=
      GST_CODEC_DMABUF_MEMORY_KIND_CODEC_DMABUF)
    return FALSE;

  memory = gst_buffer_peek_memory (buffer, 0);
  dmabuf_memory = gst_codec_dmabuf_memory_get (memory);
  if (dmabuf_memory == NULL)
    return FALSE;

  if (dmabuf_memory->allocator_id !=
      gst_codec_dmabuf_allocator_get_id (allocator))
    return FALSE;

  return dmabuf_memory->format == CODEC_DMABUF_FORMAT &&
      dmabuf_memory->n_planes == CODEC_DMABUF_N_PLANES &&
      dmabuf_memory->offsets[CODEC_DMABUF_Y_PLANE] == CODEC_DMABUF_Y_OFFSET &&
      dmabuf_memory->offsets[CODEC_DMABUF_UV_PLANE] ==
          CODEC_DMABUF_UV_OFFSET &&
      dmabuf_memory->strides[CODEC_DMABUF_Y_PLANE] ==
          CODEC_DMABUF_Y_STRIDE &&
      dmabuf_memory->strides[CODEC_DMABUF_UV_PLANE] ==
          CODEC_DMABUF_UV_STRIDE;
}
