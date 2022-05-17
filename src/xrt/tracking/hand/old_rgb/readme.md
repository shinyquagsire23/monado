<!--
Copyright 2021, Collabora, Ltd.
Authors:
Moses Turner <moses@collabora.com>
SPDX-License-Identifier: BSL-1.0
-->

# What is this?
This is a driver to do optical hand tracking. The actual code mostly written by Moses Turner, with tons of help from Marcus Edel, Jakob Bornecrantz, Ryan Pavlik, and Christoph Haag. Jakob Bornecrantz and Marcus Edel are the main people who gathered training data for the initial Collabora models.

In `main` it only works with Valve Index, although we've used a lot of Luxonis cameras in development. With additional work, it should work fine with devices like the T265, or PS4/PS5 cam, should there be enough interest for any of those.

Under good lighting, I would say it's around as good as Oculus Quest 2's hand tracking. Not that I'm trying to make any claims; that's just what I honestly would tell somebody if they are wondering if it's worth testing out.


# How to get started
## Get dependencies
### Get OpenCV
Each distro has its own way to get OpenCV, and it can change at any time; there's no specific reason to trust this documentation over anything else.

Having said that, on Ubuntu, it would look something like

```
sudo apt install libopencv-dev libopencv-contrib-dev
```

Or you could build it from source, or get it from one of the other 1000s of package managers. Whatever floats your boat.

### Get ONNXRuntime
I followed the instructions here: https://onnxruntime.ai/docs/how-to/build/inferencing.html#linux

then had to do
```
cd build/Linux/RelWithDebInfo/
sudo make install
```

### Get the ML models
Make sure you have git-lfs installed, then run ./scripts/get-ht-models.sh. Should work fine.

## Building the driver
Once onnxruntime is installed, you should be able to build like normal with CMake or Meson.

If it properly found everything, - CMake should say

```
-- Found ONNXRUNTIME: /usr/local/include/onnxruntime

[...]

-- #    DRIVER_HANDTRACKING: ON
```

and Meson should say

```
Run-time dependency libonnxruntime found: YES 1.8.2

[...]

Message: Configuration done!
Message:     drivers:  [...] handtracking, [...]
```

## Running the driver
Currently, it's only set up to work on Valve Index.

So, the two things you can do are
* Use the `survive` driver with both controllers off - It should automagically start hand tracking upon not finding any controllers.
* Use the `vive` driver with `VIVE_USE_HANDTRACKING=ON` and it should work the same as the survive driver.

You can see if the driver is working with `openxr-simple-playground`, StereoKit, or any other app you know of. Poke me (Moses) if you find any other cool hand-tracking apps; I'm always looking for more!

# Tips and tricks

This tracking likes to be in a bright, evenly-lit room with multiple light sources. Turn all the lights on, see if you can find any lamps. If the ML models can see well, the tracking quality can get surprisingly nice.

Sometimes, the tracking fails when it can see more than one hand. As the tracking gets better (we train better ML models and squash more bugs) this should happen less often or not at all. If it does, put one of your hands down, and it should resume tracking the remaining hand just fine.

# Future improvements

* Get more training data; train better ML models.
* Improve the tracking math
  * Be smarter about keeping tracking lock on a hand
  * Try predicting the next bounding box based on the estimated keypoints of the last few frames instead of uncritically trusting the detection model, and not run the detection model *every single* frame.
  * Instead of directly doing disparity on the observed keypoints, use a kinematic model of the hand and fit that to the 2D observations - this should get rid of a *lot* of jitter and make it look better to the end user if the ML models fail
  * Make something that also works with non-stereo (mono, trinocular, or N cameras) camera setups
* Optionally run the ML models on GPU - currently, everything's CPU bound which could be sub-optimal under some circumstances
* Write a lot of generic code so that you can run this on any stereo camera
* More advanced prediction/interpolation code that doesn't care at all about the input frame cadence. One-euro filters are pretty good about this, but we can get better!
