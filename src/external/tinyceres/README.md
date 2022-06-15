<!--
Copyright 2022, Collabora, Ltd.
Authors:
Moses Turner <moses@collabora.com>
SPDX-License-Identifier: CC0-1.0
-->

tinyceres
============

tinyceres is a small template library for solving Nonlinear Least Squares problems, created from small subset of [ceres-solver](http://ceres-solver.org/) - mainly TinySolver and the files that TinySover includes. It was created for [Monado](https://monado.freedesktop.org/) for real-time optical hand tracking, and in order to avoid adding a submodule or another system dependency the code was simply copied into Monado's source tree. The source-controlled version can be found [here](https://gitlab.freedesktop.org/monado/utilities/hand-tracking-playground/tinyceres)
