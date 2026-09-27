project('paladin-launcher', 'c', version: '0.2.0',
        default_options: ['c_std=c11', 'warning_level=3'])

gtk = dependency('gtk+-3.0', version: '>=3.24')
xapp = dependency('xapp')
gio = dependency('gio-2.0', version: '>=2.76')
gmodule = dependency('gmodule-2.0')
core = files('src/game.c', 'src/storage.c', 'src/runner.c', 'src/operations.c')
gui = files('src/ui.c', 'src/dialogs.c')
core_deps = [gio, gmodule]
gui_deps = [gtk, xapp, gio, gmodule]
inc = include_directories('src')

executable('paladin-launcher', ['src/main.c', core, gui],
  dependencies: gui_deps, install: true)
install_data('data/io.github.thespecterx.PaladinLauncher.desktop',
  install_dir: get_option('datadir') / 'applications')

foreach name : ['game', 'runner', 'operations']
  test(name, executable('test-' + name, ['tests/test-' + name + '.c', core],
    dependencies: core_deps, include_directories: inc), suite: 'core', timeout: 30)
endforeach
test('ui-lifetime', executable('test-ui', ['tests/test-ui.c', core, gui],
  dependencies: gui_deps, include_directories: inc), suite: 'gui', timeout: 40)
