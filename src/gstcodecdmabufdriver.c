#include "gstcodecdmabufdriver.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

static int
codec_dmabuf_driver_open (const char *device_path)
{
  return open (device_path, O_RDWR | O_CLOEXEC);
}

static int
codec_dmabuf_driver_close (int device_fd)
{
  return close (device_fd);
}

static int
codec_dmabuf_driver_allocate (int device_fd, MmAllocIoctl *arg)
{
#ifdef MM_IOC_ALLOC_CO
  return ioctl (device_fd, MM_IOC_ALLOC_CO, arg);
#else
  /* driver ioctl 定義が無い build では、実機 allocation は未対応にする。 */
  (void) device_fd;
  (void) arg;
  errno = ENOTSUP;
  return -1;
#endif
}

static int
codec_dmabuf_driver_free (int device_fd, MmAllocIoctl *arg)
{
#ifdef MM_IOC_FREE_CO
  return ioctl (device_fd, MM_IOC_FREE_CO, arg);
#else
  (void) device_fd;
  (void) arg;
  errno = ENOTSUP;
  return -1;
#endif
}

static int
codec_dmabuf_driver_export_start (int device_fd, MmExportIoctl *arg)
{
#ifdef MM_IOC_EXPORT_START
  return ioctl (device_fd, MM_IOC_EXPORT_START, arg);
#else
  /* export_start が無い環境では外部 DMABUF fd を生成できない。 */
  (void) device_fd;
  (void) arg;
  errno = ENOTSUP;
  return -1;
#endif
}

static int
codec_dmabuf_driver_export_end (int device_fd, MmExportIoctl *arg)
{
#ifdef MM_IOC_EXPORT_END
  return ioctl (device_fd, MM_IOC_EXPORT_END, arg);
#else
  (void) device_fd;
  (void) arg;
  errno = ENOTSUP;
  return -1;
#endif
}

const CodecDmabufDriverOps *
gst_codec_dmabuf_driver_default_ops (void)
{
  static const CodecDmabufDriverOps ops = {
    codec_dmabuf_driver_open,
    codec_dmabuf_driver_close,
    codec_dmabuf_driver_allocate,
    codec_dmabuf_driver_free,
    codec_dmabuf_driver_export_start,
    codec_dmabuf_driver_export_end,
  };

  return &ops;
}
