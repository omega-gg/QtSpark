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
               .azure-pipelines.yml \
               3rdparty.sh \
               build.sh \
               deploy.sh \
               dist/start.sh \
               doc/plan.md \
               doc/resync.md \
               doc/benchmark.md \
               upstream/spark.pin \
               upstream/files.txt \
               tools/resync.sh \
               tools/bench.sh \
               test/scenes.json \
               test/bench.json
