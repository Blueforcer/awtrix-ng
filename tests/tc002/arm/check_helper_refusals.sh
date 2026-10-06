#!/bin/sh
# check_helper_refusals.sh HELPER EMULATOR...
#
# The speaker helper as it ships refuses to start without its socket, with a descriptor that is not
# open and with any environment variable; each refusal exits 2.
helper=$1
shift
refuses() {
  "$@" >/dev/null 2>&1
  status=$?
  if [ "$status" -ne 2 ]; then
    echo "$*: exit $status, expected 2" >&2
    exit 1
  fi
}
refuses env -i "$@" "$helper"
refuses env -i "$@" "$helper" --socket-fd 7
refuses env -i FOO=1 "$@" "$helper" --socket-fd 7
echo "refused: no socket, a closed descriptor, an environment"
