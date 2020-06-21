project('mcpdisp', 'c', 'cpp', version : '0.1.2', license : 'GPL-2+')

add_project_arguments('-DVERSION="0.1.2"', language : 'cpp')

jackdep = dependency('jack')

cc = meson.get_compiler('c')
fltkdep = cc.find_library('fltk', required: true)

executable('mcpdisp',
    sources: ['src/mcpdisp.cc'],
    cpp_args : '-O0',
    dependencies: [fltkdep, jackdep],
    install: true,
    )

install_data(['src/mcpdisp.desktop', 'src/mcpdisp-ext.desktop'],
    install_dir : get_option('datadir') / 'applications')

install_data('src/mcpdisp.svg',
    install_dir : get_option('datadir') / 'icons/hicolor/scalable/apps')

install_man('mcpdisp.1')
