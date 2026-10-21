#!/usr/bin/env python3
'''
Run a batch command in the in-build GIMP and make every abnormal end a
test failure: non-zero exit, a crash of the core or of the plug-in
(fatal error, signal, stack trace prompt, broken wire), or a batch
command which did not report success.

The core is run with --stack-trace-mode=never and stdin closed, so a
crash can never wait on the interactive "(E)xit, (S)tack trace,
(P)roceed" prompt (which used to let a crashing test end with status 0).
'''

import subprocess
import sys

CRASH_MARKERS = [
  'fatal error',
  'Segmentation fault',
  'received signal',
  '(E)xit, (S)tack trace',
  'gimp_wire_read(): unexpected EOF',
  'crashed',
]

SUCCESS_MARKER = 'batch command executed successfully'

def gimp_output(output):
  '''
  The output without the command dump of tools/in-build-gimp.py
  ("RUNNING: <command line>", which contains the whole test script as
  the -b argument, over many lines, up to the final --quit): a script
  that mentions a crash marker, e.g. in a comment or a message string,
  must not fail the test.
  '''
  lines = output.splitlines(keepends=True)
  kept  = []
  i     = 0

  while i < len(lines):
    if lines[i].startswith('RUNNING: '):
      while i < len(lines) and not lines[i].rstrip().endswith('--quit'):
        i += 1
      i += 1
      continue
    kept.append(lines[i])
    i += 1

  return ''.join(kept)

def run_gimp_batch(gimp_exe, cmd):
  args = [sys.executable, gimp_exe, '-nis', '--stack-trace-mode=never',
          '--batch-interpreter', 'python-fu-eval', '-b', cmd, '--quit']
  proc = subprocess.run(args, stdin=subprocess.DEVNULL,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                        text=True, errors='replace')
  sys.stdout.write(proc.stdout)
  sys.stderr.write(proc.stderr)
  sys.stdout.flush()
  sys.stderr.flush()

  output = proc.stdout + gimp_output(proc.stderr)
  errors = []

  if proc.returncode != 0:
    errors.append('GIMP exited with status {}'.format(proc.returncode))
  for marker in CRASH_MARKERS:
    if marker in output:
      errors.append('output contains "{}"'.format(marker))
  if SUCCESS_MARKER not in output:
    errors.append('output does not contain "{}"'.format(SUCCESS_MARKER))

  if errors:
    sys.stderr.write('\nTEST FAILED:\n  ' + '\n  '.join(errors) + '\n')
    sys.exit(proc.returncode if proc.returncode not in (0, 77) else 1)
