project('mcpdisp', 'c', 'cpp', version : '0.1.0', license : 'GPLv2')

jackdep = dependency('jack')

cc = meson.get_compiler('c')
fltkdep = cc.find_library('fltk', required: true)

executable('mcpdisp',
    sources: ['src/mcpdisp.cc'],
    dependencies: [fltkdep, jackdep],
    install: true,
    )
