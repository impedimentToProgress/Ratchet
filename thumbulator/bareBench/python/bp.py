import random
import re

import gdb
import commands

bp_list = []
bp_metrics = {}
exit_flag = False
debug_flag = False

def reset():
  gdb.execute('del')

  del bp_list[:]
  bp_metrics.clear()
  bp_metrics['fail'] = []
  bp_metrics['checkpoints'] =[]

  exit_flag = False
  debug_flag = False

def get_addr(label):
  dis = gdb.execute("disass {}".format(label), False, True)
  m = re.search('0x[0-9A-Fa-f]*(?= <)', dis)
  return m.group(0)

class GDBExcept(Exception):
  def __init__(self, value):
    self.value = value
  def __str__(self):
    return repr(self.value)


class MyBP(gdb.Breakpoint):
  def __init__(self, spec, bp_type):
    super(MyBP, self).__init__(spec, bp_type)
    bp_list.append(self)

  def delete(self):
    bp_list.remove(self)
    super(MyBP, self).delete()


class MetricBP(MyBP):
  def __init__(self, spec, bp_type=gdb.BP_BREAKPOINT):
    super(MetricBP, self).__init__(spec, bp_type)
    self.silent = True

class CP_BP(MetricBP):
  def __init__(self):
    spec = "*{}".format(get_addr('_checkpoint'))
    super(CP_BP, self).__init__(spec)

  def handler(self):
    #bp_metrics['checkpoints'] = self.hit_count
    pass

class ECP_BP(MetricBP):
  def __init__(self):
    spec = "*{}".format(get_addr('_exit_checkpoint'))
    self.cycle = 0
    super(ECP_BP, self).__init__(spec)

  def handler(self):
    prev_cp = self.cycle
    self.cycle = commands.total_cycles()
    lr = str(gdb.parse_and_eval('$lr'))
    bp_metrics['checkpoints'].append({'time': self.cycle, 'last_cp': self.cycle - prev_cp, 'ret_addr': lr})

class ERestore_BP(MetricBP):
  def __init__(self):
    spec = "*{}".format(get_addr('_exit_restore_checkpoint'))
    super(ERestore_BP, self).__init__(spec)

  def handler(self):
    bp = None
    for _bp in bp_list:
      if isinstance(_bp, ECP_BP):
        bp = _bp
        break

    wasted = commands.total_cycles() - bp.cycle

    bp_metrics['fail'][-1]['wasted'] = wasted


class FailBP(MyBP):
  def __init__(self, spec, bp_type):
    super(FailBP, self).__init__(spec, bp_type)
    self.silent = True

  def handler(self):
    self._handler()

    cycles = commands.total_cycles()
    cycles_since = commands.cycles_since_fail()
    cur_pc = commands.get_pc()
    bp_metrics['fail'].append({'time':cycles, 'last_fail' : cycles_since, 'pc':str(cur_pc), 'wasted': 0})

    commands.cpu_reset()
    commands.cycle_reset()


class FreqFailBP(FailBP):
  def __init__(self, freq, threshold):
    self.locations = []
    self.threshold = threshold
    commands.writeword(hex(commands.MEMMAPIO.resetAfterCycles), freq)
    spec = "*{} > {}".format(hex(commands.MEMMAPIO.cyclesSinceReset), freq)
    super(FreqFailBP, self).__init__(spec, gdb.BP_WATCHPOINT)

  def _handler(self):
    pc = commands.get_pc()
    self.locations.append(pc)

    if self.count_consecutive_fails(pc) > self.threshold:
      self.delete()
      raise GDBExcept("{} failures at {}, no progress being made. Try increasing the number of cycles between failures!".format(self.threshold, pc))


  def count_consecutive_fails(self, pc):
    count = 0
    for _pc in reversed(self.locations):
      if _pc == pc:
        count = count + 1
      else:
        break
    return count

class GaussFailBP(FailBP):
  def __init__(self, m, std):
    self.m = m
    self.std = std
    r = -1
    while r < 0:
      r = int(random.gauss(m, std))
    commands.writeword(hex(commands.MEMMAPIO.resetAfterCycles), r)
    spec = "*{} > {}".format(hex(commands.MEMMAPIO.cyclesSinceReset), r)
    super(GaussFailBP, self).__init__(spec, gdb.BP_WATCHPOINT)

  def _handler(self):
    for bp in bp_list:
      if isinstance(bp, GaussFailBP) and bp != self:
        bp.delete()

    GaussFailBP(self.m, self.std)

    self.enabled = False

class DebugFailBP(FailBP):
  def __init__(self, fails, skiplast=False):
    r = fails.pop(0)
    self.fails = fails
    self.skiplast= skiplast
    commands.writeword(hex(commands.MEMMAPIO.resetAfterCycles), r)
    spec = "*{} > {}".format(hex(commands.MEMMAPIO.cyclesSinceReset), r)
    super(DebugFailBP, self).__init__(spec, gdb.BP_WATCHPOINT)
    self.silent = False

  def _handler(self):
    pass

  def handler(self):
    self.enabled = False

    if len(self.fails) != 0 or not self.skiplast:
      super(DebugFailBP, self).handler()

    if len(self.fails) != 0:
      DebugFailBP(self.fails, self.skiplast)




class ExitBP(MyBP):
  def __init__(self, spec):
    super(ExitBP, self).__init__(spec, gdb.BP_BREAKPOINT)

class HashBP(ExitBP):
  def __init__(self, spec, outf):
    self.outfile = outf
    super(HashBP, self).__init__(spec)

  def handler(self):
    bp_metrics['hash']   = commands.get_hash()
    dump_metrics(self.outfile)
    print bp_metrics['hash']

def dump_metrics(fname):
  bp_metrics['cycles'] = commands.total_cycles()
  f = open(fname, 'wb')
  f.write(str(bp_metrics))
  f.close()




def stop_handler(stop_event):
  """Handles stop events in GDB. We use this to give our breakpoints
     special functionality"""
  if not hasattr(stop_event, 'breakpoints'):
    # Don't know what happened... Let's wrap this up.
    print "Umm... don't know how we got here... <stop_handler>"
    dump_metrics('error.log')
    exit_flag = True
  else:
    for bp in stop_event.breakpoints:
      if isinstance(bp, MyBP):
        bp.handler()

def add_bps(outf, logging):
  # Get the hash at the end of the program
  HashBP("*{}".format(get_addr('exit')), outf)
  if logging == "high":
    CP_BP()
    ECP_BP()
    ERestore_BP()


