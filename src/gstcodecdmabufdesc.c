#include "gstcodecdmabufdesc.h"

#include <string.h>
#include <unistd.h>

gboolean
gst_codec_dmabuf_buffer_to_nv12_desc (GstBuffer *buffer, guint width,
    guint height, CodecNv12BufferDesc *out_desc)
{
  GstMemory *memory;
  CodecDmabufMemory *dmabuf_memory;
  int borrowed_fd;
  int duplicated_fd;

  g_return_val_if_fail (out_desc != NULL, FALSE);

  memset (out_desc, 0, sizeof (*out_desc));
  out_desc->dmabuf_fd = -1;

  if (buffer == NULL || width == 0 || height == 0 ||
      width > CODEC_DMABUF_MAX_WIDTH || height > CODEC_DMABUF_MAX_HEIGHT)
    return FALSE;

  if (gst_codec_dmabuf_buffer_get_memory_kind (buffer) !=
      GST_CODEC_DMABUF_MEMORY_KIND_CODEC_DMABUF)
    return FALSE;

  memory = gst_buffer_peek_memory (buffer, 0);
  dmabuf_memory = gst_codec_dmabuf_memory_get (memory);
  if (dmabuf_memory == NULL)
    return FALSE;

  borrowed_fd = gst_dmabuf_memory_get_fd (memory);
  if (borrowed_fd < 0)
    return FALSE;

  duplicated_fd = dup (borrowed_fd);
  if (duplicated_fd < 0)
    return FALSE;

  out_desc->dmabuf_fd = duplicated_fd;
  out_desc->width = width;
  out_desc->height = height;
  out_desc->y_offset = dmabuf_memory->offsets[CODEC_DMABUF_Y_PLANE];
  out_desc->y_stride = dmabuf_memory->strides[CODEC_DMABUF_Y_PLANE];
  out_desc->uv_offset = dmabuf_memory->offsets[CODEC_DMABUF_UV_PLANE];
  out_desc->uv_stride = dmabuf_memory->strides[CODEC_DMABUF_UV_PLANE];
  out_desc->size = dmabuf_memory->size;
  out_desc->buffer_ref = gst_buffer_ref (buffer);

  return TRUE;
}

void
gst_codec_dmabuf_nv12_desc_clear (CodecNv12BufferDesc *desc)
{
  if (desc == NULL)
    return;

  if (desc->dmabuf_fd >= 0)
    close (desc->dmabuf_fd);
  gst_clear_buffer (&desc->buffer_ref);
  memset (desc, 0, sizeof (*desc));
  desc->dmabuf_fd = -1;
}
