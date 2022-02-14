project('vulkan-sandbox', 'c')

vulkandep = dependency('vulkan')

sources = files([
  'src/sandbox.c'
])

subdir('shaders')

executable('sandbox', sources,
  dependencies: vulkandep)
