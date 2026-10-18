TEMPLATE = subdirs

CONFIG += ordered

SUBDIRS = src \
          viewer \
          test/splattest

OTHER_FILES += README.md \
               implementation.md \
               LICENSE.md \
               LICENSE.spark \
               LICENSE.three \
               build.sh \
               doc/plan.md \
               doc/resync.md \
               doc/benchmark.md \
               upstream/spark.pin \
               upstream/files.txt \
               tools/resync.sh \
               tools/bench.sh \
               test/scenes.json \
               test/bench.json
