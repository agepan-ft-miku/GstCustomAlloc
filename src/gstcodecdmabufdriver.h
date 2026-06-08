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
  /* 実機では Linux API、単体テストでは fake ops に差し替える。 */
  int (*open) (const char *device_path);
  int (*close) (int device_fd);
  int (*allocate) (int device_fd, MmAllocIoctl *arg);
  int (*free) (int device_fd, MmAllocIoctl *arg);
  int (*export_start) (int device_fd, MmExportIoctl *arg);
  int (*export_end) (int device_fd, MmExportIoctl *arg);
} CodecDmabufDriverOps;

/**
 * gst_codec_dmabuf_driver_default_ops:
 *
 * Linux driver access 用の既定 ops を返す。
 * ioctl 定義が build 環境に無い場合、対応する ops は errno を %ENOTSUP にして失敗する。
 *
 * Returns: (transfer none): 静的に保持される既定 #CodecDmabufDriverOps。
 */
const CodecDmabufDriverOps * gst_codec_dmabuf_driver_default_ops (void);

G_END_DECLS
