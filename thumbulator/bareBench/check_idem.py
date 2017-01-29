import sys
import re

cp_re    = re.compile("[0-9A-F]{8}: CP: ([0-9]*).*$")
read_re  = re.compile("[0-9A-F]{8}: (?:Flash|Ram) read at (0x[0-9A-F]{8})=0x[0-9A-F]{8}$")
write_re = re.compile("[0-9A-F]{8}: (?:Flash|Ram) write at (0x[0-9A-F]{8})=0x[0-9A-F]{8}$")

def print_violation(violators, lines, output):
  addr_re = re.compile("[0-9A-F]{8}: (?:Flash|Ram) (?:read|write) at (0x[0-9A-F]{8})=0x[0-9A-F]{8}$")
  s = "Found idempotency violation!\n"
  s += str(violators) + '\n'
  s += "----- BEGIN -----\n"
  for l in lines:
    addr = addr_re.match(l)
    if addr == None:
      continue

    for v in violators:
      if v == addr.group(1):
        s += str(l[:-1]) + '\n'
  s += "------ END ------\n"
  if s not in output:
    output.append(s)
    print s

def check_idempotency(reads, line, ignore):
  addr_re = re.compile("([0-9A-F]{8}):")

  addr = addr_re.match(line).group(1)
  write = write_re.match(line).group(1)

  if write == ignore:
    return False

  for read in reads:
    if write == read[0] and addr != addr_re.match(read[1]).group(1):
      return True

  return False

def parse_file(fname, ignore):
  if fname != '-':
    f = open(fname, 'r')
  else:
    f = sys.stdin
  writes = []
  reads  = []
  lines  = []
  violators = []
  output = []
  all_violators = []
  for line in f:

    # Check to see if we're at a checkpoint
    #cp = cp_re.match(line)
    #if cp != None:
    if "CP: " in line:
      if len(violators):
        print_violation(violators, lines, output)

      writes = []
      reads  = []
      lines  = []
      violators = []
      continue

    lines.append(line)


    if "read at" in line:
      r = read_re.match(line)
      addr = r.group(1)
      if addr not in writes:
        reads.append((addr, line))

    if "write at" in line:
      w = write_re.match(line)
      addr = w.group(1)
      flag = False
      for write in writes:
        if addr == write[0]:
          flag = True
          break

      if flag == False and check_idempotency(reads, line, ignore):
        if addr not in violators:
          violators.append(addr)
          if addr not in all_violators:
            all_violators.append(addr)
      writes.append((addr, line))

    if "Program exit" in line:
      if len(violators):
        print_violation(violators, lines, output)
        break

  if len(violators):
    print_violation(violators, lines, output)

  print "All aliasing addresses:"
  print all_violators

def idem_sect_length(fname):
  f = open(fname, 'r')
  last_cp = 0
  lengths = []

  for line in f:
    m = cp_re.match(line)

    if m != None:
      lengths.append(int(m.group(1))-last_cp)
      last_cp = int(m.group(1))

  return lengths




if __name__=="__main__":
  if len(sys.argv) < 3:
    print "Usage: python {} <filename> <ignore>".format(sys.argv[0])
    sys.exit()

  parse_file(sys.argv[1], sys.argv[2])
  #print idem_sect_length(sys.argv[1])
