import re
import sys

############### ObjDumpFile Class ###############
class ObjDumpFile:
  def _remove_false_instructions(self, begin, end):
    for i in range(begin, end):
      if i in self.instructions:
        del self.instructions[i]

  def _get_text_section(self):
    f = open(self.filename, 'r')

    # Ignore the lines until text section
    line = ""
    while "Disassembly of section .text" not in line:
      line = f.readline()

    line = f.readline()
    # Read the entirity of the text section
    while "Disassembly of section" not in line:
      if re.search("\A\s*[0-9A-Fa-f]*:\s*[0-9A-Fa-f]{8}", line):
        # This is an data line
        m = re.match("\A\s*([0-9A-Fa-f]*):\s*([0-9A-Fa-f]{8})\s*(.*)", line)
        self.data[int(m.group(1),16)] = (m.group(2), m.group(3))
      elif re.search("\A\s*[0-9A-Fa-f]*:\s*[0-9A-Fa-f]{4}", line):
        # This is an assembly line
        m = re.match("\A\s*([0-9A-Fa-f]*):\s*([0-9A-Fa-f]{4})\s*(.*)", line)
        self.instructions[int(m.group(1),16)] = (m.group(2), m.group(3))
        if "svc" in m.group(3):
          # This is an exit instruction
          self.exits[int(m.group(1),16)] = (m.group(2), m.group(3))
      elif re.search("\A[0-9A-Fa-f]*\s<.*>:", line):
        # This is a label
        m = re.match("(\A[0-9A-Fa-f]*)\s<(.*)>:", line)
        self.labels[m.group(2)] = int(m.group(1),16)
      elif "Disassembly of section" in line:
        # We shouldn't have gotten here...
        break
      line = f.readline()

  def get_addresses(self):
    """
    Gets addresses of important pieces of the binary
    """
    addr = dict()
    addr['entry']         = hex(self.labels["_start"])
    addr['exit']          = hex(self.exits.keys()[0])
    addr['sp']            = hex(int(self.data[0][0],16))
    #addr['first_lib']     = self.get_first_lib_func()
    addr['cp_exit']       = self.labels['.exit_checkpoint']
    addr['cp_done']       = self.labels['.checkpoint_done']
    addr['restore_exit']  = self.labels['.exit_restore_checkpoint']
    addr['cp']            = self.labels['_checkpoint']

    return addr

  def get_first_lib_func(self):
    """
    Returns the last address that contains functions that have been run through
    our LLVM instrumentation passes.
    """

    # Get the address of last function that we've run our passes on
    dirs = self.filename.split('/')[:-1]
    dirs.append('main.llvmout')
    fname = "/".join(dirs)
    f = open(fname, 'r')

    endOfNonLib = 0
    line = f.readline()
    while line != "":
      if re.search("\A\*\*\* MemoryIdempotenceAnalysis for Function ", line):
        m = re.match("\A\*\*\* MemoryIdempotenceAnalysis for Function (.*) \*\*\*$", line)
        if endOfNonLib < self.labels[m.group(1)]:
          endOfNonLib = self.labels[m.group(1)]
      line = f.readline()

    # Get the address of the first function that has not been instrumented.
    firstLibFunction = sys.maxint
    for k in self.labels.keys():
      if self.labels[k] < firstLibFunction and self.labels[k] > endOfNonLib:
        firstLibFunction = self.labels[k]

    return firstLibFunction

  def get_instructions(self):
    return self.instructions

  def __init__(self, fname):
    self.filename = fname
    self.labels = dict()
    self.instructions = dict()
    self.data = dict()
    self.exits = dict()

    # Fill in instructions/labels/exits
    self._get_text_section()

    assert(len(self.exits) == 1)


