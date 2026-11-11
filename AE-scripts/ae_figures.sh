#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PROFILE="full"
SECTIONS=("all")
MODE="plot"
OUT_DIR="output/ae_figures"
FORMATS=("jpg" "eps")

usage() {
  cat <<'EOF'
Usage: bash AE-scripts/ae_figures.sh [run|dry-run|plot] [options]

Modes:
  run       Run the AE extension experiments, analyze summaries, then plot.
  dry-run   Generate configs, traces, and sanity summaries without running Xerxes.
  plot      Plot the six AE extension figures from existing summary CSVs.

Options:
  --profile smoke|full      Experiment profile to use for plotting/running. Default: full.
  --sections LIST...        Sections for run/dry-run. Default: all.
                            Choices match run_ae_extension_experiments.py: 1.1 1.2 1.3 2.1 2.2 2.3 all.
  --out-dir PATH            Figure output directory. Default: output/ae_figures.
  --formats LIST...         Figure formats. Default: jpg eps.
  -h, --help                Show this help.

Examples:
  bash AE-scripts/ae_figures.sh plot
  bash AE-scripts/ae_figures.sh dry-run --profile smoke --sections 1.1 1.3
  bash AE-scripts/ae_figures.sh run --profile full --sections all
EOF
}

if [[ $# -gt 0 && "$1" != --* ]]; then
  MODE="$1"
  shift
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile)
      PROFILE="$2"
      shift 2
      ;;
    --sections)
      shift
      SECTIONS=()
      while [[ $# -gt 0 && "$1" != --* ]]; do
        SECTIONS+=("$1")
        shift
      done
      ;;
    --out-dir)
      OUT_DIR="$2"
      shift 2
      ;;
    --formats)
      shift
      FORMATS=()
      while [[ $# -gt 0 && "$1" != --* ]]; do
        FORMATS+=("$1")
        shift
      done
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ ${#SECTIONS[@]} -eq 0 ]]; then
  echo "At least one section is required after --sections." >&2
  exit 2
fi
if [[ ${#FORMATS[@]} -eq 0 ]]; then
  echo "At least one format is required after --formats." >&2
  exit 2
fi

case "$MODE" in
  run)
    python3 AE-scripts/run_ae_extension_experiments.py --profiles "$PROFILE" --sections "${SECTIONS[@]}"
    python3 AE-scripts/analyze_ae_extension_results.py
    python3 AE-scripts/run_ae_footprint_regimes.py --mode final
    ;;
  dry-run)
    python3 AE-scripts/run_ae_extension_experiments.py --profiles "$PROFILE" --sections "${SECTIONS[@]}" --dry-run
    python3 AE-scripts/analyze_ae_extension_results.py
    python3 AE-scripts/run_ae_footprint_regimes.py --mode final --dry-run
    ;;
  plot)
    ;;
  *)
    echo "Unknown mode: $MODE" >&2
    usage >&2
    exit 2
    ;;
esac

if python3 - <<'PY' >/dev/null 2>&1
import matplotlib
import numpy
PY
then
  PLOT_CMD=(python3)
elif command -v uv >/dev/null 2>&1; then
  PLOT_CMD=(uv run --with matplotlib --with numpy python3)
else
  echo "matplotlib and numpy are required for plotting. Install them or install uv." >&2
  exit 1
fi

"${PLOT_CMD[@]}" output/plot_ae_figures.py \
  --profile "$PROFILE" \
  --out-dir "$OUT_DIR" \
  --formats "${FORMATS[@]}"

mkdir -p figures/ae
for fmt in "${FORMATS[@]}"; do
  cp -f "$OUT_DIR"/fig_ae_*."$fmt" figures/ae/
done
echo "Copied AE figures to figures/ae/"
