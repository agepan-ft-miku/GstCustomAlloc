#pragma once

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _MmAllocIoctl {
  gsize size;
  guint flag;
  gpointer addr;
} MmAllocIoctl;

typedef struct _MmExportIoctl {
  gsize size;
  gpointer addr;
  int buf;
} MmExportIoctl;

typedef struct _CodecDmabufDriverOps {
  int (*open) (const char *device_path);
  int (*close) (int device_fd);
  int (*allocate) (int device_fd, MmAllocIoctl *arg);
  int (*free) (int device_fd, MmAllocIoctl *arg);
  int (*export_start) (int device_fd, MmExportIoctl *arg);
  int (*export_end) (int device_fd, MmExportIoctl *arg);
} CodecDmabufDriverOps;

const CodecDmabufDriverOps * gst_codec_dmabuf_driver_default_ops (void);

G_END_DECLS
