import bp
import commands
import gdb
import random

def freq(f, threshold=10000):
  failbp = bp.FreqFailBP(f, threshold)
  #failbp.ignore(1000000)
  #commands.cont()

def randfreq(seed, m, std):
  random.seed(seed)
  bp.GaussFailBP(m, std)

def run_til_end(dumpf):
  exit_bp = None
  for _bp in bp.bp_list:
    if isinstance(_bp, bp.ExitBP):
      exit_bp = _bp
      break

  while exit_bp.hit_count == 0 and bp.exit_flag == False:
    try:
      gdb.execute("c")
    except KeyboardInterrupt:
      break

  if bp.exit_flag:
    bp.dump_metrics(dumpf)

  try:
    gdb.execute("c")
  except:
    pass

def parselog(errorlog, silent=False):
  f = open(errorlog, 'rb')
  log = f.read()
  metrics = eval(log)

  fails = []
  cnt = 0
  for fail in metrics['fail']:
    fails.append(fail['last_fail'])
    if not silent:
      print "Fail {}: pc={}".format(cnt, fail['pc'])
      cnt += 1

  return fails



def replay(errorlog, skiplast=False):
  fails = parselog(errorlog)
  bp.DebugFailBP(fails, skiplast)

  #while bp.exit_flag == False and bp.debug_flag == False:
  #  try:
  #    gdb.execute("c")
  #  except KeyboardInterrupt:
  #    break

  #print bp.debug_flag

