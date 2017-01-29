import subprocess
import re
import sys
import os

def get_config(item, config):
  m = re.search('(?<={}=).*'.format(item), config)
  return m.group(0)

def generate_config(name, root, mean, std, debug, logging):
  f = open(name, 'w')
  f.write('ROOT={}\n'.format(root))
  f.write('ELF={}\n'.format(root + "main.elf"))
  f.write('BIN={}\n'.format(root + "main.bin"))

  resultsdir = str(root) + 'results/'
  f.write('RESULTS={}\n'.format(resultsdir))
  f.write('LOG={}\n'.format(resultsdir+'logfile.txt'))
  f.write('LOGGING={}\n'.format(logging))
  f.write('SIMOUT={}\n'.format(resultsdir+'simout.txt'))
  f.write('SIM={}\n'.format('../sim_main'))
  f.write('MEAN={}\n'.format(mean))
  f.write('STDDEV={}\n'.format(std))
  f.write('DEBUG={}\n'.format(debug))
  f.close()

def verify(root):
  f = open("{}/results/logfile.txt".format(root), 'r')
  iters = f.read()
  f.close()

  iters = iters.split(',')[:-1]

  h = None

  print "Verifying {}!".format(root)

  success = True
  for i in iters:
    f=open('{}/results/{}.txt'.format(root,i), 'r')
    out = eval(f.read())
    f.close()

    print "{}: {} -- {}".format(i, out['hash'], out['cycles'])

    if h == None:
      h = out['hash']
    elif h != out['hash']:
      success = False

  if success:
    print "{} passes all checks!".format(root)
  else:
    print "{} failed checks!".format(root)

  return success

def remake(roots):
  for root in roots:
    os.chdir(root)
    subprocess.call(["make", "clean"])
    subprocess.call(["make"])
    os.chdir("..")

def test(args, iters):
  config_name = 'regression.config'
  gdbpath = "../../gccToolchain/tools/bin/arm-none-eabi-gdb"
  gdbfile = "gdbtools.py"
  mean = 2400000
  stdev= 240000
  roots = ['rsa/']
  #roots = ['rsa/','crc/', 'FFT/', 'sha/', 'picojpeg/',
  #    'stringsearch/', 'dijkstra/', 'basicmath/']
  #roots = ['basicmath/']
  success = {}

  if len(args) > 1 and args[1] == "test":
    try:
      for root in roots:
        generate_config(config_name, root, mean, stdev, 1, "low")
        subprocess.call([gdbpath, "--command={}".format(gdbfile)])
    except KeyboardInterrupt:
      pass
  elif len(args) > 1 and argv[1] == "verify":
    for root in roots:
      success[root] = verify(root)
  else:
    remake(roots)

    try:
      for root in roots:
        generate_config(config_name, root, mean, stdev, 0, "low")
        for i in range(iters):
          subprocess.call([gdbpath, "--command={}".format(gdbfile)])
          print "Iteration: {}".format(i)

        success[root] = verify(root)
    except KeyboardInterrupt:
      pass

  return success

if __name__ == "__main__":
  iters = 3
  print test(sys.argv, iters)
