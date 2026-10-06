# tc002_fetch FILE URL SHA256
#
# Prints the path of TC002_DL_DIR/FILE once its SHA-256 matches, downloading it from URL when it is
# not there yet. A file that does not match stays where it is and stops the recipe; a download that
# does not match is discarded.
tc002_fetch() {
  local path=$TC002_DL_DIR/$1
  if [ ! -e "$path" ]; then
    mkdir -p "$TC002_DL_DIR"
    rm -f "$path.part"
    if ! curl -fsSL --retry 3 -o "$path.part" "$2"; then
      rm -f "$path.part"
      echo "cannot download $2" >&2
      return 1
    fi
    if [ "$(sha256sum < "$path.part" | cut -d' ' -f1)" != "$3" ]; then
      rm -f "$path.part"
      echo "$2 does not have the pinned SHA-256 $3" >&2
      return 1
    fi
    mv "$path.part" "$path"
  elif [ "$(sha256sum < "$path" | cut -d' ' -f1)" != "$3" ]; then
    echo "$path does not have the pinned SHA-256 $3; remove it to download it again" >&2
    return 1
  fi
  printf '%s\n' "$path"
}
