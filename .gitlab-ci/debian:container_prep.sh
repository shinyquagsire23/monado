#!/bin/bash

(
    cd $(dirname $0)
    bash ./install-cross.sh
)
(
    cd $(dirname $0)
    bash ./build-openxr-openhmd.sh
)
