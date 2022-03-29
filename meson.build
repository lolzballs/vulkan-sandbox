project('vaapi-player', 'c')

vulkandep = dependency('vulkan')
libdrm_dep = dependency('libdrm')
libxcb_dep = dependency('xcb')

sources = files([
  'src/main.c',
  'src/window.c',
])

executable('player', sources,
  dependencies: [
    vulkandep,
    libdrm_dep,
    libxcb_dep,
  ],
  include_directories: 'include')
