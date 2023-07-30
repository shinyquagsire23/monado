#!/usr/bin/env python3
# Copyright 2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
# Authors: Moses Turner <moses@collabora.com>
"""Simple script to upload Monado camera calibrations to DepthAI devices."""

# Todo, make this work with calibrations from Basalt

from dataclasses import dataclass
from typing import Any, Callable, ClassVar, Dict, Iterator, List, Optional, Tuple
import cv2
import cv2.fisheye
import depthai as dai
import json
import numpy as np
import math
import argparse

parser = argparse.ArgumentParser(description='Train keypoints network')

parser.add_argument("calibration_file")

parser.add_argument('--super-cow',
                    help="I know what I'm doing, run the script with no prompt",
                    dest='super_cow', action='store_true'
                    )

parser.add_argument('--baseline',
                    help="Specified camera baseline (by CAD, or whatever), in centimeters",
                    type=float
                    )

parser.set_defaults(super_cow=False)
parser.set_defaults(baseline=8)
args = parser.parse_args()

# print(args.super_cow, args.calibration_file)


if (not args.super_cow):
  print("Warning! This script will erase the current calibration on your DepthAI device and replace it with something new, and there is no going back!")
  print("Also, there is no way to specify which device to upload to, so make sure you've only plugged one in!")
  print("If you don't know what you're doing, please exit this script!")
  print("Otherwise, type \"I know what I am doing\"")

  text = input()

  if (text != "I know what I am doing"):
    print("Prompt failed!")
    exit()

print(args.baseline)

# Create pipeline
pipeline = dai.Pipeline()

# Define sources and outputs
monoLeft = pipeline.create(dai.node.MonoCamera)
monoRight = pipeline.create(dai.node.MonoCamera)
xoutLeft = pipeline.create(dai.node.XLinkOut)
xoutRight = pipeline.create(dai.node.XLinkOut)

xoutLeft.setStreamName('left')
xoutRight.setStreamName('right')

# Properties
monoLeft.setBoardSocket(dai.CameraBoardSocket.LEFT)
monoLeft.setResolution(dai.MonoCameraProperties.SensorResolution.THE_720_P)
monoRight.setBoardSocket(dai.CameraBoardSocket.RIGHT)
monoRight.setResolution(dai.MonoCameraProperties.SensorResolution.THE_720_P)

# Linking
monoRight.out.link(xoutRight.input)
monoLeft.out.link(xoutLeft.input)


@dataclass
class camera:
  camera_matrix: List[List[float]]
  distortion: List[float]
  # rotation_matrix: List[List[float]]


with open(args.calibration_file) as f:
  calibration_json = json.load(f)

fisheye = calibration_json["cameras"][0]["model"] == "fisheye_equidistant4"

print("Fisheye:", fisheye)

if fisheye:
  camera_model = dai.CameraModel.Fisheye
else:
  camera_model = dai.CameraModel.Perspective

# Connect to device and start pipeline
with dai.Device(pipeline) as device:

  # Output queues will be used to get the grayscale frames from the outputs defined above
  qLeft = device.getOutputQueue(name="left", maxSize=4, blocking=False)
  qRight = device.getOutputQueue(name="right", maxSize=4, blocking=False)

  while True:
    # Instead of get (blocking), we use tryGet (nonblocking) which will return the available data or None otherwise
    inLeft = qLeft.tryGet()
    inRight = qRight.tryGet()

    if inLeft is not None:
      cv2.imshow("left", inLeft.getCvFrame())

    if inRight is not None:
      cv2.imshow("right", inRight.getCvFrame())

    if cv2.waitKey(1) == ord('q'):
      break
    break

  # [[0,0,0],[0,0,0],[0,0,0]]
  cameras = [camera([[0, 0, 0], [0, 0, 0], [0, 0, 0]], [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]),
             camera([[0, 0, 0], [0, 0, 0], [0, 0, 0]], [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])]

  for ele, camera_struct in zip(calibration_json["cameras"], cameras):
    Lint = ele["intrinsics"]
    camera_struct.camera_matrix = [
        [Lint["fx"], 0, Lint["cx"]],
        [0, Lint["fy"], Lint["cy"]],
        [0, 0, 1]
    ]
    Ldist = ele["distortion"]
    if fisheye:
      camera_struct.distortion[0] = Ldist["k1"]
      camera_struct.distortion[1] = Ldist["k2"]
      camera_struct.distortion[2] = Ldist["k3"]
      camera_struct.distortion[3] = Ldist["k4"]
    else:
      camera_struct.distortion[0] = Ldist["k1"]
      camera_struct.distortion[1] = Ldist["k2"]
      camera_struct.distortion[2] = Ldist["p1"]
      camera_struct.distortion[3] = Ldist["p2"]
      camera_struct.distortion[4] = Ldist["k3"]

  R = np.array([[0, 0, 0], [0, 0, 0], [0, 0, 0]], dtype=np.float32)
  acc_idx = 0
  for row in range(3):
    for col in range(3):
      R[row][col] = calibration_json["opencv_stereo_calibrate"]["rotation"][acc_idx]
      acc_idx += 1

  T = np.array(calibration_json["opencv_stereo_calibrate"]["translation"])
  T *= 100  # Centimeters

  if False:
    print(R, T)
    print(np.array(cameras[0].camera_matrix),
          np.array(cameras[0].distortion),
          np.array(cameras[1].camera_matrix),
          np.array(cameras[1].distortion))

    print(cameras[0].distortion[:4])

  R1, R2, P1, P2, Q = cv2.fisheye.stereoRectify(
      np.array(cameras[0].camera_matrix),
      np.array(cameras[0].distortion[:4]),
      np.array(cameras[1].camera_matrix),
      np.array(cameras[1].distortion[:4]),
      (1280, 800),  # imagesize
      R,
      T,
      0
  )

  calh = dai.CalibrationHandler()

  current_eeprom = device.readCalibration().getEepromData()

  # currently only copies whatever is on the device to the new calibration
  calh.setBoardInfo(productName = current_eeprom.productName, boardName = current_eeprom.boardName,
                    boardRev = current_eeprom.boardRev, boardConf = current_eeprom.boardConf,
                    hardwareConf = current_eeprom.hardwareConf, batchName = current_eeprom.batchName,
                    batchTime = current_eeprom.batchTime, boardOptions = current_eeprom.boardOptions,
                    boardCustom = current_eeprom.boardCustom)

  calh.setCameraExtrinsics(dai.CameraBoardSocket.LEFT, dai.CameraBoardSocket.RIGHT,
                           R, translation=T, specTranslation=[args.baseline, 0, 0])

  calh.setCameraIntrinsics(dai.CameraBoardSocket.LEFT, cameras[0].camera_matrix, 1280, 800)
  calh.setCameraIntrinsics(dai.CameraBoardSocket.RIGHT, cameras[1].camera_matrix, 1280, 800)

  calh.setCameraType(dai.CameraBoardSocket.LEFT, camera_model)
  calh.setCameraType(dai.CameraBoardSocket.RIGHT, camera_model)

  calh.setDistortionCoefficients(dai.CameraBoardSocket.LEFT, cameras[0].distortion)
  calh.setDistortionCoefficients(dai.CameraBoardSocket.RIGHT, cameras[1].distortion)

  calh.setStereoLeft(dai.CameraBoardSocket.LEFT, R1)
  calh.setStereoRight(dai.CameraBoardSocket.RIGHT, R2)

  success = device.flashCalibration(calh)

  print("success is:", success)
