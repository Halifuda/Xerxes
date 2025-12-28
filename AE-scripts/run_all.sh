#!/usr/bin/env bash
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
echo "$DIR"
SCRIPT_FOLDER="$(basename "$DIR")"
echo "$SCRIPT_FOLDER"
PARENT_DIR="$(dirname "$DIR")"
echo "$PARENT_DIR"
SCRIPTS=(fig11.sh) # fig10.sh fig11.sh fig12.sh fig13.sh fig14.sh fig17.sh fig1516.sh)

cd "$PARENT_DIR"

mkdir -p "figures"

for s in "${SCRIPTS[@]}"; do
  script="$SCRIPT_FOLDER/$s"
  if [[ ! -x "$script" ]]; then
    if [[ -f "$script" ]]; then
      chmod +x "$script" || true
    else
      echo "Warning: $s not found, skipping." >&2
      continue
    fi
  fi

  echo "Running $s..."
  # run the script with working directory set to DIR's parent
  bash "$script"

  base="${s%.sh}"
  fig_src="output/$base/$base.png"
  if [[ -f "$fig_src" ]]; then
    cp -f "$fig_src" "figures/"
    echo "Copied $fig_src to figures/"
  else
    echo "Warning: $fig_src not found, skipping copy." >&2
  fi
done
