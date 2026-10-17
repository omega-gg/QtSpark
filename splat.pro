TEMPLATE = subdirs

CONFIG += ordered

SUBDIRS = src \
          viewer \
          test/splattest

OTHER_FILES += README.md \
               LICENSE.md \
               LICENSE.spark \
               build.sh \
               doc/plan.md \
               doc/implementation.md \
               doc/resync.md \
               upstream/spark.pin \
               upstream/files.txt \
               tools/resync.sh \
               test/scenes.json \
               test/bench.json
