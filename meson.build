project('vaapi-player', 'c')

vulkandep = dependency('vulkan')
libdrm_dep = dependency('libdrm')
libxcb_dep = dependency('xcb')

sources = files([
  'src/image.c',
  'src/main.c',
  'src/window.c',
  'src/vulkan.c',
])

add_global_arguments('-DVK_USE_PLATFORM_XCB_KHR', language : 'c')
executable('player', sources,
  dependencies: [
    vulkandep,
    libdrm_dep,
    libxcb_dep,
  ],
  include_directories: 'include')
