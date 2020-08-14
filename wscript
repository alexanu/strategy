import sys, os

from waflib.Tools.compiler_c import c_compiler
from waflib.Tools.compiler_cxx import cxx_compiler

sys.path += [ '../backend/tools/waf-plugins' ]

def options(opt):
  opt.load('defaults')
  opt.load('compiler_c')
  opt.load('compiler_cxx')

def configure(conf):
  from waflib import Task, Context
  conf.load('defaults')
  conf.load('compiler_c')
  conf.load('compiler_cxx')
  conf.env.INCLUDES += [ '../external/common/include', 'include' ]
  conf.env.INCLUDES += [ '../backend/src', 'src' ]
  conf.env.CXXFLAGS += [ '-g', '-ldl', '-std=c++11']
  conf.check(lib='pthread', uselib_store='pthread')
  conf.check(lib='config++', uselib_store='config++')
  conf.check(lib='python2.7', uselib_store='python2.7')
  conf.check(lib='zmq', uselib_store='zmq')
  conf.check(lib='z', uselib_store='z')

from waflib.Build import BuildContext
class all_class(BuildContext):
  cmd = "all"
class simplemaker_class(BuildContext):
  cmd = "simplemaker"
class simplearb_class(BuildContext):
  cmd = "simplearb"
class simplearb2_class(BuildContext):
  cmd = "simplearb2"
class coinarb_class(BuildContext):
  cmd = "coinarb"
class pairtrading_class(BuildContext):
  cmd = "pairtrading"
class demostrat_class(BuildContext):
  cmd = "demostrat"
from lint import add_lint_ignore

def build(bld):
  add_lint_ignore('../external')
  add_lint_ignore('../backend')
  if bld.cmd == "all":
    run_all(bld)
    return
  if bld.cmd == "simplemaker":
    run_simplemaker(bld)
    return
  if bld.cmd == "simplearb":
    run_simplearb(bld)
    return
  if bld.cmd == "simplearb2":
    run_simplearb2(bld)
    return
  if bld.cmd == "coinarb":
    run_coinarb(bld)
    return
  if bld.cmd == "pairtrading":
    run_pairtrading(bld)
    return
  if bld.cmd == "demostrat":
    run_demostrat(bld)
    return
  else:
    print("error! ", str(bld.cmd))
    return

def run_simplemaker(bld):
  bld.read_shlib('nick', paths=['../external/common/lib'])
  bld.shlib(
    target = 'lib/simplemaker',
    source = ['simplemaker/simplemaker.cpp'],
    includes = ['../external/zeromq/include'],
    use = 'zmq nick pthread config++'
  )

def run_simplearb(bld):
  bld.read_shlib('nick', paths=['../external/common/lib'])
  bld.shlib(
    target = 'lib/simplearb',
    source = ['simplearb/simplearb.cpp'],
    includes = ['../external/zeromq/include'],
    use = 'zmq nick pthread config++ shm'
  )

def run_simplearb2(bld):
  bld.read_shlib('nick', paths=['../external/common/lib'])
  bld.shlib(
    target = 'lib/simplearb2',
    source = ['simplearb2/simplearb2.cpp'],
    includes = ['../external/zeromq/include'],
    use = 'zmq nick pthread config++ shm'
  )

def run_coinarb(bld):
  bld.read_shlib('nick', paths=['../external/common/lib'])
  bld.shlib(
    target = 'lib/coinarb',
    source = ['coinarb/coinarb.cpp'],
    includes = ['../external/zeromq/include'],
    use = 'zmq nick pthread config++ shm c'
  )

def run_pairtrading(bld):
  bld.read_shlib('nick', paths=['../external/common/lib'])
  bld.shlib(
    target = 'lib/pairtrading',
    source = ['pairtrading/pairtrading.cpp'],
    includes = ['../external/zeromq/include'],
    use = 'zmq nick pthread config++ shm'
  )

def run_demostrat(bld):
  bld.read_shlib('nick', paths=['../external/common/lib'])
  bld.shlib(
    target = 'lib/demostrat',
    source = ['demostrat/demostrat.cpp'],
    includes = ['../external/zeromq/include'],
    use = 'zmq nick pthread config++ z'
  )

def run_all(bld):
  run_simplearb(bld)
  run_simplearb2(bld)
  run_coinarb(bld)
  run_pairtrading(bld)
  run_demostrat(bld)
  run_simplemaker(bld)
