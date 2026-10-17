#!/bin/sh
set -e

#--------------------------------------------------------------------------------------------------
# Windowed benchmark: splatviewer on each backend against Spark, interleaved in rounds (AB, then
# BA) so the laptop GPU throttling hits every side alike. See doc/benchmark.md.
#--------------------------------------------------------------------------------------------------

usage()
{
    echo "Usage: bench <quick | full> [options]"
    echo ""
    echo "quick: 2 rounds of 200 frames, a few minutes. For experiments and A/B checks."
    echo "full:  4 rounds of the test/bench.json frames, about 30 minutes. For doc/benchmark.md."
    echo ""
    echo "Options:"
    echo "  --scene a,b          Scenes from test/bench.json (default all)."
    echo "  --backends \"a b\"     Viewer backends (default \"d3d12 d3d11 vulkan\")."
    echo "  --no-spark           Skip Spark."
    echo "  --kit mingw | msvc   Viewer build (default msvc, the only one with D3D12)."
    echo "  --size WxH           Window size (default 2560x1440)."
    echo "  --tag name           Result files prefix in build/bench (default the time)."
    echo ""
    echo "Environment: SPARK (default C:/dev/test/spark), the Spark checkout."

    exit 1
}

preset="$1"

case "$preset" in quick|full) ;; *) usage ;; esac

shift

scene=""
backends="d3d12 d3d11 vulkan"
spark=1
kit="msvc"
size="2560x1440"
tag="$(date +%Y%m%d-%H%M%S)"

while [ $# -gt 0 ]; do

    case "$1" in
    --scene)    scene="$2";    shift ;;
    --backends) backends="$2"; shift ;;
    --no-spark) spark=0 ;;
    --kit)      kit="$2";      shift ;;
    --size)     size="$2";     shift ;;
    --tag)      tag="$2";      shift ;;
    *)          usage ;;
    esac

    shift
done

root="$(cd "$(dirname "$0")/.." && pwd)"

SPARK="${SPARK:-C:/dev/test/spark}"

width="${size%x*}"
height="${size#*x}"

if [ "$preset" = "quick" ]; then

    rounds=2
    frames="--frames 200 --warmup 30"
else
    rounds=4
    frames=""
fi

if [ "$kit" = "msvc" ]; then

    viewer="$root/bin/msvc/splatviewer.exe"

    export PATH="/c/Qt/6.10.1/msvc2022_64/bin:$PATH"
else
    viewer="$root/bin/splatviewer.exe"

    export PATH="/c/Qt/6.10.1/mingw_64/bin:/c/Qt/Tools/mingw1310_64/bin:$PATH"
fi

export QT_FORCE_STDERR_LOGGING=1

output="$root/build/bench"

mkdir -p "$output"

scenes=""

if [ -n "$scene" ]; then scenes="--scene $scene"; fi

#--------------------------------------------------------------------------------------------------
# Runs
#--------------------------------------------------------------------------------------------------

run_spark()
{
    echo "=== round $1 spark"

    (cd "$root/test/reference" && node bench.mjs --spark "$SPARK" --angle d3d11 --repeats 1 \
        --width $width --height $height $frames $scenes --output "$output/$tag-spark-r$1.json") \
        | grep -E "splats, median"
}

run_viewer()
{
    echo "=== round $1 $2"

    "$viewer" --backend $2 --bench "$root/test/bench.json" --fixtures "$root/build/fixtures" \
        --repeats 1 --width $width --height $height $frames $scenes \
        --output "$output/$tag-$2-r$1.json" 2>&1 | grep -E "splats, frame"
}

reversed=""

for backend in $backends; do reversed="$backend $reversed"; done

# NOTE: The GPU slows down by about 2x within a minute of load, then stays there. A short quick
#       run would measure its first runs at full clocks, so it warms the GPU up first.
if [ "$preset" = "quick" ]; then

    first="${backends%% *}"

    echo "=== warm-up ($first, not measured)"

    "$viewer" --backend $first --bench "$root/test/bench.json" --fixtures "$root/build/fixtures" \
        --repeats 1 --width $width --height $height --frames 600 --warmup 0 $scenes \
        --output "$output/warmup-$tag.json" > /dev/null 2>&1
fi

round=1

while [ $round -le $rounds ]; do

    if [ $((round % 2)) = 1 ]; then

        if [ $spark = 1 ]; then run_spark $round; fi

        for backend in $backends; do run_viewer $round $backend; done
    else
        for backend in $reversed; do run_viewer $round $backend; done

        if [ $spark = 1 ]; then run_spark $round; fi
    fi

    round=$((round + 1))
done

#--------------------------------------------------------------------------------------------------
# Summary
#--------------------------------------------------------------------------------------------------

echo ""

node "$root/test/reference/summary.mjs" "$output/$tag"-*.json
