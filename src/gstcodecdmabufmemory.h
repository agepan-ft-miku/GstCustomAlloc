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

  /* driver が管理する元 resource。GstDmaBufMemory に渡す fd とは別に所有する。 */
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

/**
 * gst_codec_dmabuf_memory_new:
 * @allocator: resource を所有する #GstCodecDmabufAllocator。
 * @device_fd: allocation/export に使った driver fd。
 * @exported_fd: driver が返した元 DMABUF fd。
 * @addr: driver が返した allocation address。
 * @size: allocation size。
 *
 * #GstDmaBufMemory の qdata として保持する driver resource 情報を作成する。
 *
 * Returns: (transfer full) (nullable): 新しい #CodecDmabufMemory。
 */
CodecDmabufMemory * gst_codec_dmabuf_memory_new (
    GstCodecDmabufAllocator *allocator,
    int device_fd,
    int exported_fd,
    gpointer addr,
    gsize size);

/**
 * gst_codec_dmabuf_memory_free:
 * @memory: (nullable): 解放する #CodecDmabufMemory。
 *
 * #CodecDmabufMemory を解放する。
 * driver resource が残っていれば allocator 経由で export_end/free を実行する。
 */
void gst_codec_dmabuf_memory_free (CodecDmabufMemory *memory);

/**
 * gst_codec_dmabuf_memory_attach:
 * @memory: qdata を付ける #GstMemory。
 * @dmabuf_memory: (transfer full): 紐づける #CodecDmabufMemory。
 *
 * #GstDmaBufMemory に codec allocator 由来情報を qdata として紐づける。
 * 成功後、@dmabuf_memory の所有権は qdata destroy notify に移る。
 *
 * Returns: qdata を付与できた場合は %TRUE。
 */
gboolean gst_codec_dmabuf_memory_attach (
    GstMemory *memory,
    CodecDmabufMemory *dmabuf_memory);

/**
 * gst_codec_dmabuf_memory_get:
 * @memory: 確認する #GstMemory。
 *
 * @memory に紐づく #CodecDmabufMemory qdata を取得する。
 *
 * Returns: (transfer none) (nullable): qdata の #CodecDmabufMemory。
 */
CodecDmabufMemory * gst_codec_dmabuf_memory_get (GstMemory *memory);

/**
 * gst_codec_dmabuf_buffer_is_dmabuf:
 * @buffer: 確認する #GstBuffer。
 *
 * @buffer が 1 個の DMABUF memory だけを持つか確認する。
 *
 * Returns: DMABUF buffer と判定できる場合は %TRUE。
 */
gboolean gst_codec_dmabuf_buffer_is_dmabuf (GstBuffer *buffer);

/**
 * gst_codec_dmabuf_buffer_get_memory_kind:
 * @buffer: 分類する #GstBuffer。
 *
 * @buffer の memory 種別を分類する。
 * codec allocator 由来の DMABUF、外部 DMABUF、system memory、判定不能を区別する。
 *
 * Returns: @buffer の #GstCodecDmabufMemoryKind。
 */
GstCodecDmabufMemoryKind gst_codec_dmabuf_buffer_get_memory_kind (
    GstBuffer *buffer);

/**
 * gst_codec_dmabuf_memory_kind_to_string:
 * @kind: 文字列化する #GstCodecDmabufMemoryKind。
 *
 * debug log 向けに memory kind を短い文字列へ変換する。
 *
 * Returns: (transfer none): 静的文字列。
 */
const gchar * gst_codec_dmabuf_memory_kind_to_string (
    GstCodecDmabufMemoryKind kind);

/**
 * gst_codec_dmabuf_buffer_is_supported_input:
 * @buffer: 検証する #GstBuffer。
 * @allocator: 期待する #GstCodecDmabufAllocator。
 *
 * encoder input として受け入れ可能な codec allocator 由来 DMABUF か検証する。
 * allocator identity と固定 NV12 layout が一致する場合だけ成功する。
 *
 * Returns: hardware codec input として扱える場合は %TRUE。
 */
gboolean gst_codec_dmabuf_buffer_is_supported_input (
    GstBuffer *buffer,
    GstCodecDmabufAllocator *allocator);

G_END_DECLS
