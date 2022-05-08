vulkan_shaders_src = [
  'shader.vert',
  'shader.frag'
]

glslang = find_program('glslangValidator', native: true, required: true)

vulkan_shaders = []

foreach shader : vulkan_shaders_src
  name = shader.underscorify() + '_data'
  args = [glslang, '--target-env', 'vulkan1.2', '-V', '@INPUT@', '-o', '@OUTPUT@', '--vn', name]
  header = custom_target(
    shader + '_spv',
    output: shader + '.h',
    input: shader,
    command: args
  )

  vulkan_shaders += [header]
endforeach

sources += vulkan_shaders
