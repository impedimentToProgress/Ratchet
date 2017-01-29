import gdb
import bp
from objdumpfile import ObjDumpFile
import subprocess

class MEMMAPIO:
  cycles, cyclesMSB, wasteCycles, wasteCyclesH, \
      cyclesSinceReset, cyclesSinceCP, addrOfCP, addrOfRestoreCP, \
      resetAfterCycles, do_reset, do_logging, wdt_seed, \
      wdt_val, md5_0, md5_1, md5_2, md5_3, md5_4 = range(0x80000000,0x80000000+4*18,4)

def get_pc():
  return gdb.parse_and_eval("$pc")

def cpu_reset():
  """Performs a hard reset on the processor"""
  gdb.execute("set *{}=1".format(hex(MEMMAPIO.do_reset)));

def cycle_reset():
  """Sets the cyclesSinceReset to 0"""
  gdb.execute("set *{}=0".format(hex(MEMMAPIO.cyclesSinceReset)));

def readword(addr):
  """Returns a word from a given memory address"""
  output = gdb.execute("x/wx {}".format(addr), False, True)
  return int(output.split()[1],16)

def readgword(addr):
  """Returns 2 words from a given memory address"""
  output = gdb.execute("x/gwx {}".format(addr), False, True)
  return int(output.split()[1],16)

def writeword(addr,val):
  gdb.execute("set *{}={}".format(addr, val))

def logging(on):
  if on:
    writeword(MEMMAPIO.do_logging, 1)
  else:
    writeword(MEMMAPIO.do_logging, 0)

def start_sim(path, binfile, outf):
  return subprocess.Popen([path, "-g", binfile], stdout=outf, stderr=outf)

def exit_handler(exit_event):
  print 'exit_handler'
  #print "exitcode: {}".format(exit_event.exit_code)
  #gdb.execute("quit")

def setup(fname):
  """
  Connects to thumbulator, registers our breakpoint handler, inserts our exit breakpoint
  """
  cmd = 'file {}'.format(fname)
  gdb.execute(cmd)
  gdb.execute('target remote :272727')
  #gdb.execute("set confirm off")
  gdb.execute("set pagination off")

  # register breakpoint handler
  gdb.events.stop.connect(bp.stop_handler)
  gdb.events.exited.connect(exit_handler)
  #gdb.events.exited.connect(bp.stop_handler)

  #dumpf = "".join(fname.split('.')[:-1])
  #obj = ObjDumpFile("{}.lst".format(dumpf))
  #addr = obj.get_addresses()

  #return bp.HashBP("*{}".format(addr['exit']), True)

def cont():
  gdb.execute("c")

def get_hash():
  gdb.execute("set *{}=1".format(hex(MEMMAPIO.md5_0)))
  hi = gdb.execute("x/xg {}".format(MEMMAPIO.md5_1),False, True).split()[1]
  lo = gdb.execute("x/xg {}".format(MEMMAPIO.md5_3),False, True).split()[1]
  return hi + lo[2:]

def cycles_since_fail():
  return readword(MEMMAPIO.cyclesSinceReset)

def total_cycles():
  return readgword(MEMMAPIO.cycles)



