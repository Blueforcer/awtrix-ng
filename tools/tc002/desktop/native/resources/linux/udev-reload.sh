#!/bin/sh
if command -v udevadm >/dev/null 2>&1; then
  udevadm control --reload-rules || true
fi
