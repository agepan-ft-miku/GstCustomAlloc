#pragma once

#include "gstcodecdmabufdriver.h"

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_CODEC_DMABUF_ALLOCATOR (gst_codec_dmabuf_allocator_get_type ())
G_DECLARE_FINAL_TYPE (GstCodecDmabufAllocator, gst_codec_dmabuf_allocator,
    GST, CODEC_DMABUF_ALLOCATOR, GstAllocator)

/**
 * gst_codec_dmabuf_allocator_new:
 * @device_path: (nullable): codec memory driver の device path。%NULL の場合は既定値を使う。
 *
 * driver-backed DMABUF を確保する custom #GstAllocator を作成する。
 * 返された allocator は driver fd と公式 #GstDmaBufAllocator を内部所有する。
 *
 * Returns: (transfer full): 新しい #GstAllocator。不要になったら gst_object_unref() する。
 */
GstAllocator * gst_codec_dmabuf_allocator_new (const gchar *device_path);

/**
 * gst_codec_dmabuf_allocator_set_driver_ops:
 * @allocator: #GstCodecDmabufAllocator。
 * @ops: 差し替える driver ops。
 *
 * allocator がまだ driver を open していない場合に driver ops を差し替える。
 * 主に単体テストで fake ops を注入するために使う。
 */
void gst_codec_dmabuf_allocator_set_driver_ops (
    GstCodecDmabufAllocator *allocator,
    const CodecDmabufDriverOps *ops);

/**
 * gst_codec_dmabuf_allocator_get_id:
 * @allocator: #GstCodecDmabufAllocator。
 *
 * allocator instance を識別する ID を返す。
 * DMABUF memory が期待する allocator 由来かどうかの検証に使う。
 *
 * Returns: allocator ごとに割り当てられた識別子。
 */
guint64 gst_codec_dmabuf_allocator_get_id (
    GstCodecDmabufAllocator *allocator);

/**
 * gst_codec_dmabuf_standard_pool_new:
 * @allocator: memory allocation に使う #GstCodecDmabufAllocator。
 * @caps: pool に設定する negotiated caps。
 * @min_buffers: pool の最小 buffer 数。
 * @max_buffers: pool の最大 buffer 数。
 *
 * custom pool は作らず、標準 #GstBufferPool にこの allocator を設定する。
 * driver resource の寿命は memory qdata が管理し、pool は buffer 再利用だけを担当する。
 *
 * Returns: (transfer full) (nullable): 設定済み #GstBufferPool。失敗時は %NULL。
 */
GstBufferPool * gst_codec_dmabuf_standard_pool_new (
    GstCodecDmabufAllocator *allocator,
    GstCaps *caps,
    guint min_buffers,
    guint max_buffers);

/**
 * gst_codec_dmabuf_allocator_release_driver_memory:
 * @allocator: driver ops を所有する #GstCodecDmabufAllocator。
 * @device_fd: allocation/export に使った driver fd。
 * @addr: driver が返した allocation address。
 * @size: allocation size。
 * @exported_fd: driver が返した元 DMABUF fd。
 * @export_active: %TRUE の場合は export_end を呼ぶ。
 *
 * CodecDmabufMemory の qdata destroy path から呼ばれる driver resource 解放口。
 * export が有効なら export_end を行い、その後 driver allocation を free する。
 */
void gst_codec_dmabuf_allocator_release_driver_memory (
    GstCodecDmabufAllocator *allocator,
    int device_fd,
    gpointer addr,
    gsize size,
    int exported_fd,
    gboolean export_active);

G_END_DECLS
