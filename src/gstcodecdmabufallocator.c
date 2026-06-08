#include "gstcodecdmabufallocator.h"

#include "gstcodecdmabufconfig.h"
#include "gstcodecdmabufmemory.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <gst/allocators/gstdmabuf.h>
#include <gst/video/video.h>

struct _GstCodecDmabufAllocator {
  GstAllocator parent_instance;

  /* device_fd と driver ops は allocator が所有し、element からは触らせない。 */
  gchar *device_path;
  int device_fd;
  CodecDmabufDriverOps ops;

  /* 実際の GstMemory は公式 GstDmaBufAllocator で作る。 */
  GstAllocator *dmabuf_allocator;
  guint64 allocator_id;

  GMutex lock;
  gboolean opened;
};

G_DEFINE_TYPE (GstCodecDmabufAllocator, gst_codec_dmabuf_allocator,
    GST_TYPE_ALLOCATOR)

static GMutex next_allocator_id_lock;
static guint64 next_allocator_id = 1;

static guint64
codec_dmabuf_next_allocator_id (void)
{
  guint64 id;

  g_mutex_lock (&next_allocator_id_lock);
  id = next_allocator_id++;
  g_mutex_unlock (&next_allocator_id_lock);

  return id;
}

static gboolean
codec_dmabuf_allocator_ensure_open (GstCodecDmabufAllocator *self)
{
  gboolean opened;

  g_mutex_lock (&self->lock);
  if (!self->opened) {
    const gchar *path = self->device_path != NULL ?
        self->device_path : CODEC_DMABUF_DEFAULT_DEVICE_PATH;

    self->device_fd = self->ops.open != NULL ? self->ops.open (path) : -1;
    self->opened = self->device_fd >= 0;
  }
  opened = self->opened;
  g_mutex_unlock (&self->lock);

  return opened;
}

/* allocation 途中で失敗した場合は、作成済み driver resource だけを逆順に戻す。 */
static void
codec_dmabuf_allocator_rollback (GstCodecDmabufAllocator *self,
    gpointer addr, int exported_fd, gboolean export_active)
{
  gst_codec_dmabuf_allocator_release_driver_memory (self, self->device_fd,
      addr, CODEC_DMABUF_ALLOCATION_SIZE, exported_fd, export_active);
}

static GstMemory *
codec_dmabuf_allocator_alloc (GstAllocator *allocator, gsize size,
    GstAllocationParams *params)
{
  GstCodecDmabufAllocator *self = GST_CODEC_DMABUF_ALLOCATOR (allocator);
  MmAllocIoctl alloc_arg;
  MmExportIoctl export_arg;
  GstMemory *memory = NULL;
  CodecDmabufMemory *dmabuf_memory = NULL;
  int duplicated_fd = -1;

  (void) params;

  if (size == 0 || size > CODEC_DMABUF_ALLOCATION_SIZE) {
    errno = EINVAL;
    return NULL;
  }

  if (!codec_dmabuf_allocator_ensure_open (self))
    return NULL;

  /* driver allocation は常に固定最大サイズで行う。 */
  memset (&alloc_arg, 0, sizeof (alloc_arg));
  alloc_arg.size = CODEC_DMABUF_ALLOCATION_SIZE;
  alloc_arg.flag = CODEC_DMABUF_DRIVER_ALLOC_FLAG;

  memset (&export_arg, 0, sizeof (export_arg));
  export_arg.size = CODEC_DMABUF_ALLOCATION_SIZE;

  g_mutex_lock (&self->lock);
  if (self->ops.allocate == NULL ||
      self->ops.allocate (self->device_fd, &alloc_arg) < 0) {
    g_mutex_unlock (&self->lock);
    return NULL;
  }

  export_arg.addr = alloc_arg.addr;
  if (self->ops.export_start == NULL ||
      self->ops.export_start (self->device_fd, &export_arg) < 0) {
    g_mutex_unlock (&self->lock);
    codec_dmabuf_allocator_rollback (self, alloc_arg.addr, -1, FALSE);
    return NULL;
  }
  g_mutex_unlock (&self->lock);

  duplicated_fd = dup (export_arg.buf);
  if (duplicated_fd < 0) {
    codec_dmabuf_allocator_rollback (self, alloc_arg.addr, export_arg.buf,
        TRUE);
    return NULL;
  }

  /* GstDmaBufMemory に渡した duplicated fd は GstDmaBufMemory 側の所有になる。 */
  memory = gst_dmabuf_allocator_alloc (self->dmabuf_allocator, duplicated_fd,
      CODEC_DMABUF_ALLOCATION_SIZE);
  if (memory == NULL) {
    close (duplicated_fd);
    codec_dmabuf_allocator_rollback (self, alloc_arg.addr, export_arg.buf,
        TRUE);
    return NULL;
  }

  dmabuf_memory = gst_codec_dmabuf_memory_new (self, self->device_fd,
      export_arg.buf, alloc_arg.addr, CODEC_DMABUF_ALLOCATION_SIZE);
  /* driver の元 fd/addr は memory qdata に紐づけて、memory finalize 時に解放する。 */
  if (dmabuf_memory == NULL ||
      !gst_codec_dmabuf_memory_attach (memory, dmabuf_memory)) {
    if (dmabuf_memory != NULL)
      gst_codec_dmabuf_memory_free (dmabuf_memory);
    gst_memory_unref (memory);
    return NULL;
  }

  return memory;
}

static void
codec_dmabuf_allocator_free (GstAllocator *allocator, GstMemory *memory)
{
  /* alloc() は公式 GstDmaBufMemory を返すため、driver cleanup は qdata 側で行う。 */
  (void) allocator;
  (void) memory;
}

static void
gst_codec_dmabuf_allocator_finalize (GObject *object)
{
  GstCodecDmabufAllocator *self = GST_CODEC_DMABUF_ALLOCATOR (object);

  g_mutex_lock (&self->lock);
  if (self->opened && self->ops.close != NULL)
    self->ops.close (self->device_fd);
  self->opened = FALSE;
  self->device_fd = -1;
  g_mutex_unlock (&self->lock);

  gst_clear_object (&self->dmabuf_allocator);
  g_clear_pointer (&self->device_path, g_free);
  g_mutex_clear (&self->lock);

  G_OBJECT_CLASS (gst_codec_dmabuf_allocator_parent_class)->finalize (object);
}

static void
gst_codec_dmabuf_allocator_class_init (GstCodecDmabufAllocatorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);

  object_class->finalize = gst_codec_dmabuf_allocator_finalize;
  allocator_class->alloc = codec_dmabuf_allocator_alloc;
  allocator_class->free = codec_dmabuf_allocator_free;
}

static void
gst_codec_dmabuf_allocator_init (GstCodecDmabufAllocator *self)
{
  GstAllocator *allocator = GST_ALLOCATOR_CAST (self);
  const CodecDmabufDriverOps *default_ops =
      gst_codec_dmabuf_driver_default_ops ();

  allocator->mem_type = "CodecDmabuf";
  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);

  self->device_fd = -1;
  self->ops = *default_ops;
  self->dmabuf_allocator = gst_dmabuf_allocator_new ();
  self->allocator_id = codec_dmabuf_next_allocator_id ();
  g_mutex_init (&self->lock);
}

GstAllocator *
gst_codec_dmabuf_allocator_new (const gchar *device_path)
{
  GstCodecDmabufAllocator *allocator;

  allocator = g_object_new (GST_TYPE_CODEC_DMABUF_ALLOCATOR, NULL);
  allocator->device_path = g_strdup (device_path != NULL ?
      device_path : CODEC_DMABUF_DEFAULT_DEVICE_PATH);

  return GST_ALLOCATOR_CAST (allocator);
}

void
gst_codec_dmabuf_allocator_set_driver_ops (
    GstCodecDmabufAllocator *allocator, const CodecDmabufDriverOps *ops)
{
  g_return_if_fail (GST_IS_CODEC_DMABUF_ALLOCATOR (allocator));
  g_return_if_fail (ops != NULL);

  g_mutex_lock (&allocator->lock);
  if (!allocator->opened)
    allocator->ops = *ops;
  g_mutex_unlock (&allocator->lock);
}

guint64
gst_codec_dmabuf_allocator_get_id (GstCodecDmabufAllocator *allocator)
{
  g_return_val_if_fail (GST_IS_CODEC_DMABUF_ALLOCATOR (allocator), 0);

  return allocator->allocator_id;
}

GstBufferPool *
gst_codec_dmabuf_standard_pool_new (GstCodecDmabufAllocator *allocator,
    GstCaps *caps, guint min_buffers, guint max_buffers)
{
  GstBufferPool *pool;
  GstStructure *config;

  g_return_val_if_fail (GST_IS_CODEC_DMABUF_ALLOCATOR (allocator), NULL);
  g_return_val_if_fail (caps != NULL, NULL);

  /* 再利用と min/max 管理は標準 GstBufferPool に任せる。 */
  pool = gst_buffer_pool_new ();
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps,
      CODEC_DMABUF_ALLOCATION_SIZE, min_buffers, max_buffers);
  gst_buffer_pool_config_set_allocator (config, GST_ALLOCATOR_CAST (allocator),
      NULL);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    gst_object_unref (pool);
    return NULL;
  }

  return pool;
}

void
gst_codec_dmabuf_allocator_release_driver_memory (
    GstCodecDmabufAllocator *allocator, int device_fd, gpointer addr,
    gsize size, int exported_fd, gboolean export_active)
{
  MmAllocIoctl alloc_arg;
  MmExportIoctl export_arg;

  g_return_if_fail (GST_IS_CODEC_DMABUF_ALLOCATOR (allocator));

  memset (&alloc_arg, 0, sizeof (alloc_arg));
  alloc_arg.size = size;
  alloc_arg.flag = CODEC_DMABUF_DRIVER_ALLOC_FLAG;
  alloc_arg.addr = addr;

  memset (&export_arg, 0, sizeof (export_arg));
  export_arg.size = size;
  export_arg.addr = addr;
  export_arg.buf = exported_fd;

  /* qdata destroy から呼ばれるため、driver ops 呼び出しも allocator lock で守る。 */
  g_mutex_lock (&allocator->lock);
  if (export_active && exported_fd >= 0 && allocator->ops.export_end != NULL)
    allocator->ops.export_end (device_fd, &export_arg);
  if (addr != NULL && allocator->ops.free != NULL)
    allocator->ops.free (device_fd, &alloc_arg);
  g_mutex_unlock (&allocator->lock);

  if (exported_fd >= 0)
    close (exported_fd);
}
