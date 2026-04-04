# FastFilter newFilter 从 0 到 1 完整流程（Linux）

> 说明：所有依赖安装在当前目录的 `.env/.local`，不影响系统环境。

---

## 0) 进入项目根目录
```bash
cd /home/user/FastFilter/FastFilter
```

---

## 1) 创建本地环境（使用清华镜像）
```bash
# 清理旧环境（如果需要从头来）
rm -rf .local .env
mkdir -p .local

# 下载 micromamba（清华镜像 + 重试）
curl -L --retry 5 --retry-connrefused --retry-delay 2 \
  -o /tmp/micromamba.tar.bz2 \
  https://mirrors.tuna.tsinghua.edu.cn/anaconda/micromamba/linux-64/latest

tar -xvjf /tmp/micromamba.tar.bz2 -C .local bin/micromamba

# 激活 micromamba
export MAMBA_ROOT_PREFIX="$PWD/.local/mamba"
eval "$($PWD/.local/bin/micromamba shell hook -s bash)"

# 创建本地环境（只在当前目录）
micromamba create -y -p "$PWD/.env" \
  -c https://mirrors.tuna.tsinghua.edu.cn/anaconda/pkgs/main \
  -c https://mirrors.tuna.tsinghua.edu.cn/anaconda/pkgs/free \
  -c https://mirrors.tuna.tsinghua.edu.cn/anaconda/cloud/conda-forge \
  python=3.10 cmake make gcc_linux-64=10 gxx_linux-64=10

micromamba activate "$PWD/.env"
```

---

## 2) 安装 Python 依赖（清华 PyPI）
```bash
python -m pip install -U pip -i https://pypi.tuna.tsinghua.edu.cn/simple
python -m pip install -U numpy pillow matplotlib pandas brokenaxes -i https://pypi.tuna.tsinghua.edu.cn/simple
```

---

## 3) 编译 newFilter
```bash
export CC="$CONDA_PREFIX/bin/x86_64-conda-linux-gnu-cc"
export CXX="$CONDA_PREFIX/bin/x86_64-conda-linux-gnu-c++"

cmake -S /home/user/FastFilter/FastFilter -B /home/user/FastFilter/FastFilter/build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX"

cmake --build /home/user/FastFilter/FastFilter/build \
  --target measure_newfilter_perf measure_newfilter_built measure_newfilter_fpp -j"$(nproc)"
```

---

## 4) 生成 6 份 hash（两数据集 × 三方法）
```bash
# Columbia
python3 newFilter/hash_images.py --input /home/user/FastFilter/FastFilter/ColumbiaArchive/coil-100 --method phash --output /home/user/FastFilter/FastFilter/scripts/Inputs/columbia_phash.txt
python3 newFilter/hash_images.py --input /home/user/FastFilter/FastFilter/ColumbiaArchive/coil-100 --method dhash --output /home/user/FastFilter/FastFilter/scripts/Inputs/columbia_dhash.txt
python3 newFilter/hash_images.py --input /home/user/FastFilter/FastFilter/ColumbiaArchive/coil-100 --method ahash --output /home/user/FastFilter/FastFilter/scripts/Inputs/columbia_ahash.txt

# LGarchive
python3 newFilter/hash_images.py --input /home/user/FastFilter/FastFilter/LGarchive/dataset --method phash --output /home/user/FastFilter/FastFilter/scripts/Inputs/lg_phash.txt
python3 newFilter/hash_images.py --input /home/user/FastFilter/FastFilter/LGarchive/dataset --method dhash --output /home/user/FastFilter/FastFilter/scripts/Inputs/lg_dhash.txt
python3 newFilter/hash_images.py --input /home/user/FastFilter/FastFilter/LGarchive/dataset --method ahash --output /home/user/FastFilter/FastFilter/scripts/Inputs/lg_ahash.txt
```

---

## 5) 运行 6 组 newFilter（含统计 + 出图）
```bash
cd /home/user/FastFilter/FastFilter/build

for dataset in columbia lg; do
  for method in phash dhash ahash; do
    export SIMDUP_HASH_FILE=/home/user/FastFilter/FastFilter/scripts/Inputs/${dataset}_${method}.txt
    export SIMDUP_SHUFFLE=1

    rm -f ../scripts/Inputs/SimHash-4x16-OR \
          ../scripts/Inputs/SimHash-4x16-OR-hitrate \
          ../scripts/Inputs/SimHash-4x16-OR-fill \
          ../scripts/build-newfilter.csv \
          ../scripts/fpp_table_newfilter.csv

    taskset -c 2 ./measure_newfilter_perf
    taskset -c 2 ./measure_newfilter_built
    taskset -c 2 ./measure_newfilter_fpp

    python3 ../newFilter/plot_results.py --outdir ../scripts

    outdir=../scripts/results_${dataset}_${method}
    mkdir -p "$outdir"
    mv ../scripts/newfilter-*.pdf "$outdir"/
    cp ../scripts/Inputs/SimHash-4x16-OR* "$outdir"/
    cp ../scripts/build-newfilter.csv "$outdir/build-newfilter-${dataset}-${method}.csv"
    cp ../scripts/fpp_table_newfilter.csv "$outdir/fpp_table_newfilter-${dataset}-${method}.csv"
  done
done
```

---

## 6) 汇总图（可选）
```bash
python3 /home/user/FastFilter/FastFilter/newFilter/plot_summary.py --outdir /home/user/FastFilter/FastFilter/scripts
```

---

✅ 结果输出目录：
- `scripts/results_columbia_*`
- `scripts/results_lg_*`
- `scripts/newfilter-*.pdf`
