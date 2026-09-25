project('paladin-launcher', 'c', version: '0.1.0',
        default_options: ['c_std=c11', 'warning_level=3'])

gtk = dependency('gtk+-3.0', version: '>=3.24')
xapp = dependency('xapp')
gio = dependency('gio-2.0', version: '>=2.66')

executable('paladin-launcher',
  ['src/main.c', 'src/ui.c', 'src/game.c', 'src/runner.c'],
  dependencies: [gtk, xapp, gio], install: true)

install_data('data/io.github.thespecterx.PaladinLauncher.desktop',
  install_dir: get_option('datadir') / 'applications')

test('game-storage', executable('test-game',
  ['tests/test-game.c', 'src/game.c'], dependencies: gio,
  include_directories: include_directories('src')))

test('runner-isolation', executable('test-runner',
  ['tests/test-runner.c', 'src/game.c', 'src/runner.c'], dependencies: gio,
  include_directories: include_directories('src')))
