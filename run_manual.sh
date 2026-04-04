#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
SCRIPTS_DIR="${SCRIPTS_DIR:-$ROOT/scripts}"
INPUTS_DIR="${INPUTS_DIR:-$SCRIPTS_DIR/Inputs}"
HASH_SCRIPT="${HASH_SCRIPT:-$ROOT/newFilter/hash_images.py}"

# Dataset locations (override if needed)
COLUMBIA_DIR="${COLUMBIA_DIR:-$ROOT/ColumbiaArchive/coil-100}"
LG_DIR="${LG_DIR:-$ROOT/LGarchive/dataset}"

# Optional filters (if unset, run all combos)
RUN_DATASET="${RUN_DATASET:-}"   # columbia | lg
RUN_METHOD="${RUN_METHOD:-}"     # phash | dhash | ahash

echo "Root: $ROOT"

# --- 1) local micromamba environment ---
if [[ ! -x "$ROOT/.local/bin/micromamba" ]]; then
  echo "Installing micromamba into $ROOT/.local ..."
  mkdir -p "$ROOT/.local"
  curl -L https://micro.mamba.pm/api/micromamba/linux-64/latest \
    | tar -xvj -C "$ROOT/.local" bin/micromamba
fi

export MAMBA_ROOT_PREFIX="$ROOT/.local/mamba"
eval "$("$ROOT/.local/bin/micromamba" shell hook -s bash)"

if [[ ! -d "$ROOT/.env" ]]; then
  echo "Creating local env at $ROOT/.env ..."
  micromamba create -y -p "$ROOT/.env" \
    python=3.10 cmake make gcc_linux-64=10 gxx_linux-64=10
fi

if [[ "${CONDA_PREFIX:-}" != "$ROOT/.env" ]]; then
  set +u
  micromamba activate "$ROOT/.env"
  set -u
fi

python -m pip install -U pip numpy pillow matplotlib pandas brokenaxes

# --- 2) build (local compilers only) ---
export CC="$CONDA_PREFIX/bin/x86_64-conda-linux-gnu-cc"
export CXX="$CONDA_PREFIX/bin/x86_64-conda-linux-gnu-c++"

cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX"
cmake --build "$BUILD_DIR" --target measure_newfilter_perf measure_newfilter_built measure_newfilter_fpp -j"$(nproc)"

# --- 3) generate hashes for both datasets & three methods ---
mkdir -p "$INPUTS_DIR"

for method in phash dhash ahash; do
  if [[ -d "$COLUMBIA_DIR" ]]; then
    python3 "$HASH_SCRIPT" --input "$COLUMBIA_DIR" --method "$method" \
      --output "$INPUTS_DIR/columbia_${method}.txt"
  fi
  if [[ -d "$LG_DIR" ]]; then
    python3 "$HASH_SCRIPT" --input "$LG_DIR" --method "$method" \
      --output "$INPUTS_DIR/lg_${method}.txt"
  fi
done

# --- 4) run benchmarks for all dataset+hash combos (unless filtered) ---
DATASETS=(columbia lg)
METHODS=(phash dhash ahash)

if [[ -n "$RUN_DATASET" ]]; then
  DATASETS=("$RUN_DATASET")
fi
if [[ -n "$RUN_METHOD" ]]; then
  METHODS=("$RUN_METHOD")
fi

for dataset in "${DATASETS[@]}"; do
  for method in "${METHODS[@]}"; do
    case "$dataset" in
      columbia) HASH_FILE="$INPUTS_DIR/columbia_${method}.txt" ;;
      lg)       HASH_FILE="$INPUTS_DIR/lg_${method}.txt" ;;
      *) echo "Unknown dataset: $dataset"; exit 1 ;;
    esac

    if [[ ! -f "$HASH_FILE" ]]; then
      echo "Skip $dataset/$method: hash file not found: $HASH_FILE"
      continue
    fi

    export SIMDUP_HASH_FILE="$HASH_FILE"
    export SIMDUP_SHUFFLE="${SIMDUP_SHUFFLE:-1}"

    rm -f "$INPUTS_DIR/SimHash-4x16-OR" \
          "$INPUTS_DIR/SimHash-4x16-OR-hitrate" \
          "$INPUTS_DIR/SimHash-4x16-OR-fill" \
          "$SCRIPTS_DIR/build-newfilter.csv" \
          "$SCRIPTS_DIR/fpp_table_newfilter.csv"

    if command -v taskset >/dev/null 2>&1; then
      taskset -c 2 "$BUILD_DIR/measure_newfilter_perf"
      taskset -c 2 "$BUILD_DIR/measure_newfilter_built"
      taskset -c 2 "$BUILD_DIR/measure_newfilter_fpp"
    else
      "$BUILD_DIR/measure_newfilter_perf"
      "$BUILD_DIR/measure_newfilter_built"
      "$BUILD_DIR/measure_newfilter_fpp"
    fi

    python3 "$ROOT/newFilter/plot_results.py" --outdir "$SCRIPTS_DIR"

    outdir="$SCRIPTS_DIR/results_${dataset}_${method}"
    mkdir -p "$outdir"
    shopt -s nullglob
    mv "$SCRIPTS_DIR"/newfilter-*.pdf "$outdir"/
    shopt -u nullglob
    cp "$INPUTS_DIR"/SimHash-4x16-OR* "$outdir"/
    cp "$SCRIPTS_DIR/build-newfilter.csv" "$outdir/build-newfilter-${dataset}-${method}.csv"
    cp "$SCRIPTS_DIR/fpp_table_newfilter.csv" "$outdir/fpp_table_newfilter-${dataset}-${method}.csv"
  done
done

echo "Done. Results saved under $SCRIPTS_DIR/results_*"
echo "Tip: set RUN_DATASET=lg or RUN_METHOD=dhash to limit."
