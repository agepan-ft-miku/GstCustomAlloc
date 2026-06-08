plugin_sources = [
  'gstcodecplugin.c',
  'gstcpuh264enc.c',
  'gstcpuh264dec.c',
  'gstcodeccaps.c',
  'gstcodecdmabufallocator.c',
  'gstcodecdmabufdesc.c',
  'gstcodecdmabufdriver.c',
  'gstcodecdmabufmemory.c',
  'gstcodecbackend.c',
  'gstopenh264backend.cpp',
  'gstdmabufutils.c',
]

shared_module('gstcpuh264codec',
  plugin_sources,
  dependencies: [
    gst_dep,
    gst_video_dep,
    gst_alloc_dep,
    openh264_dep,
  ],
  install: true,
  install_dir: plugin_install_dir,
  name_prefix: '')
