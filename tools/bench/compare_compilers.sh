#!/usr/bin/env bash
# GCC vs clang on the engine: builds tools/bench with both (the Linux
# plugin's options, Linux_Build/CMakeLists.txt + CMake Release), runs each
# ROUNDS times, alternating, and prints a markdown table of every line's
# best realtime factor (higher = faster). Run by
# .github/workflows/compare-linux-compilers.yml; works on any Linux box
# with g++ and clang++:  tools/bench/compare_compilers.sh [rounds]
set -euo pipefail
cd "$(dirname "$0")/../.."
rounds=${1:-5}
out=$(mktemp -d)
flags=(-std=c++20 -O2 -fno-fast-math -fPIC -DNDEBUG)
bank="Shared/Resources/Synth2 Starter Banks/Madame Eljay"

g++ "${flags[@]}" tools/bench/main.cpp -o "$out/gcc"
clang++ "${flags[@]}" tools/bench/main.cpp -o "$out/clang"
for _ in $(seq "$rounds"); do
    for c in gcc clang; do
        "$out/$c" >>"$out/$c.txt"
        "$out/$c" "$bank/010.sy1" "$bank/035.sy1" >>"$out/$c.txt"
    done
done

cpu=$(lscpu 2>/dev/null | sed -n 's/^Model name: *//p' | head -1 || true)
echo "### $(uname -m), ${cpu:-unknown CPU}, $(getconf _NPROCESSORS_ONLN) cores"
echo
echo "- $(g++ --version | head -1)"
echo "- $(clang++ --version | head -1)"
echo "- options: ${flags[*]}; best of $rounds rounds"
echo
awk '
FNR == 1 { file++ }
match($0, / x[0-9.]+ realtime/) {
    label = substr($0, 1, RSTART - 1)
    gsub(/^ +| +$/, "", label)
    value = substr($0, RSTART + 2, RLENGTH - 11) + 0
    if (!(label in seen)) { seen[label] = 1; order[++n] = label }
    if (value > best[file, label]) best[file, label] = value
}
END {
    print "| realtime factor | GCC | clang | clang / GCC |"
    print "|---|---:|---:|---:|"
    for (i = 1; i <= n; i++) {
        l = order[i]
        printf "| %s | x%.1f | x%.1f | %.2f |\n", l, best[1, l], best[2, l], best[1, l] ? best[2, l] / best[1, l] : 0
    }
}' "$out/gcc.txt" "$out/clang.txt"
