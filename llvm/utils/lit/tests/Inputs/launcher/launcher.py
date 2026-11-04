# A launcher that prints the command it gets and runs it.
import os
import subprocess
import sys

print("launcher: LIT_LAUNCHED=%s %s" % (os.environ.get("LIT_LAUNCHED"), " ".join(sys.argv[1:])))
sys.stdout.flush()
sys.exit(subprocess.call(sys.argv[1:]))
