#!/bin/sh
set -e
(
    cd $(dirname $0)
    export JAVA_HOME=/home/ryan/apps/android-studio/jre/
    make "$@"
    if [ -x ./test ]; then
        ./test
    fi
)
