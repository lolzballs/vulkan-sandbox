project('vulkan-sandbox', 'c')

vulkandep = dependency('vulkan')

sources = files([
  'src/sandbox.c',
  'src/vulkan/context.c',
])

incdir = include_directories('include')

subdir('shaders')

executable('sandbox', sources,
  dependencies: vulkandep,
  include_directories: incdir)
