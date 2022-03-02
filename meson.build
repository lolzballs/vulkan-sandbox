project('vulkan-sandbox', 'c')

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: true)

vulkandep = dependency('vulkan')

sources = files([
  'src/vulkan/context.c',
  'src/vulkan/memory.c',
])

incdir = include_directories('include')

subdir('shaders')

executable('basic', files('src/basic.c') + sources,
  dependencies: [vulkandep],
  include_directories: incdir)

executable('psnr', files('src/psnr.c') + sources,
  dependencies: [vulkandep, m_dep],
  include_directories: incdir)
