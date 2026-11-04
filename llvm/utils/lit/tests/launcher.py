# Check that LIT_LAUNCHER runs lit through the launcher, which gets the full
# command, and that the lit started by the launcher doesn't launch again.
#
# RUN: env -u LIT_LAUNCHED LIT_LAUNCHER="%{python} %{inputs}/launcher/launcher.py" \
# RUN:   %{lit} %{inputs}/launcher | FileCheck %s
#
# CHECK:     launcher: LIT_LAUNCHED=1 {{.*}}python{{.*}} {{.*}}lit.py -j1 {{.*}} Inputs/launcher
# CHECK-NOT: launcher:
# CHECK:     PASS: launcher :: test.txt

# Check that tests get LIT_LAUNCHED=1 even when this lit wasn't launched, so
# that a lit they run (here, a stand-in for it) doesn't go through a launcher.
#
# RUN: env -u LIT_LAUNCHED %{lit} -a %{inputs}/launcher/nested.txt \
# RUN:   | FileCheck --check-prefix=NESTED %s
#
# NESTED: nested: LIT_LAUNCHED=1

# Check that the launcher isn't used when LIT_LAUNCHED is already set or when
# LIT_LAUNCHER is empty.
#
# RUN: env LIT_LAUNCHED=1 LIT_LAUNCHER="%{python} %{inputs}/launcher/launcher.py" \
# RUN:   %{lit} %{inputs}/launcher | FileCheck --check-prefix=PLAIN %s
# RUN: env -u LIT_LAUNCHED LIT_LAUNCHER= %{lit} %{inputs}/launcher \
# RUN:   | FileCheck --check-prefix=PLAIN %s
#
# PLAIN-NOT: launcher:
# PLAIN:     PASS: launcher :: test.txt

# Check that a launcher that can't be run is an error.
#
# RUN: env -u LIT_LAUNCHED LIT_LAUNCHER=/nonexistent/launcher \
# RUN:   not %{lit} %{inputs}/launcher 2>&1 | FileCheck --check-prefix=MISSING %s
#
# MISSING: lit: error: cannot run LIT_LAUNCHER /nonexistent/launcher: No such file or directory
