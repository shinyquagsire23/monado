# Mercury hand tracking {#hand-tracking-mercury}

<!--
Copyright 2022, Collabora, Ltd.
SPDX-License-Identifier: BSL-1.0
-->

- Last updated: 18-June-2022

# Checklist for upstreaming new work
* Avoid nested arrays. If you do, for sure don't use magic numbers in array lengths, and try to stick then in a named struct.
* Does it pass format and spellcheck?
* Does it pass reuse?
* Does ASan say it leaks memory?
* Are all your functions (besides exports) static?
* Are you only including what you use?

# About optimizer parameters/residuals
* My understanding is that Ceres uses *forward-mode autodiff*, which means that for each number you want autodiff to consider, it's stored in a dual number (ceres::Jet) which has a "real" part - just the value, and then a "infinitesmal" part - *this value's partial derivatives with respect to the input parameters*. Then, for every operation, you do the operation to the value and apply the chain rule to the infinitesmal parts. 
  * As such, it's much easier for the number of residuals to be different than the number of input parameters - creating one `Ceres::Jet` is the same as creating many `ceres::Jet`s, but having the number of infinitesmal parts in a Jet be different at runtime would be weird and probably really expensive. 
  * Sooooo... for now, the input size is calculated statically. We have two possible input sizes - one that includes the hand size; one that doesn't. I've tried having the output residual size be calculated statically (annoying, because you need to write a ton of boilerplate to dispatch the correct template for different cases), and I've tried having it be calculated dynamically, and the performance difference doesn't seem to be huge. So, for simplicity and for compile times, the residual size is dynamic for now. We'll revisit this later when we have dataset playback capability :)


# About spatial types
In response to Ryan disliking all my nested arrays, I created some spatial types (lm_defines.hpp; Quat, Vec3 and Vec2) and stuck most of the nested arrays inside structs (Translations55, Orientations54). I think this slowed compile times down by a fair bit, and I suspect compile times are why Ceres uses a lot of raw nested arrays in their examples - sure, it's unsafe, hairy and slightly harder to understand, but iteration times are faster and that counts for a whole lot. For now we'll keep the spatial types, but I might be motivated to switch everything back to nested arrays of Jets if compile times start driving me up the wall.

We could very much use Eigen's spatial types - they template well (enough) with ceres::Jets, but I decided not to:
* I don't like digging through Eigen - when I want my code to do something, I'd rather just write the procedure myself rather than hope Eigen is doing what I expect it to do.
* Writing my own templates is fun
* Somewhat lower compile times - I can keep the template depth quite a bit lower and only include the procedures I actually need

This is definitely worth revisiting, and doing direct speed comparisons, but we need to do it later. Will be much easier once we have dataset playback

# About how we deal with left and right hands
Your hands are just about mirror images of each other. As such, it would be nice to come up with an abstraction for evaluating a model of a hand to oriented hand joints *once*, say for the left hand, then have our code magically make that work for the right hand, despite the right hand being different.

One pretty normal way of doing this (and the way I did it with kine_ccdik) would be, if you get a right hand, mirror all the observations, do the optimization, then when you're done un-mirror the final result. But this introduces another "mirror world" coordinate space that's pretty hard to think of, and really hard to get in and out of if you are using Poses or Isometries to do space transforms instead of Affines. We're using Poses. Also, it makes things very annoying if you want to optimize two hands at once (ie. to try to stop self-collision or something, which we aren't doing yet.)

Instead, in eval_hand_with_orientation, if we have a right hand, we mirror the X axis of each joint's *translation relative to its parent, in tracking space*. That's it. It seems too simple, but it works well. Then, after we're done optimizing, zldtt_ori_right does... something... to make the joints' tracking-relative orientations look correct. I don't perfectly understand why the change-of-basis operation is the correct operation, but it's not that surprising, as that transformation is the same one that you would do to get back from the "mirror world" to the regular world.
# Some todos. Not an exhaustive list; I'd never be done if I wrote them all
* Split out the "state tracker" bits from mercury::HandTracking into another struct - like, all the bits that it uses to remember if it saw the hand last frame, which views the hand was seen in, the hand size, whether it's calibrating the hand size, etc.
* Check that the thumb metacarpal neutral pose makes sense, and that the limits make sense. It seems like it's pretty often not getting the curl axis right.
* Robustness; rejecting bad hands
  * This one is really important, and we should have it:
    * Discard *newly detected hands that overlap too well with previously detected hands* based on intersection over union
  * If we detect a new hand in both views at once, use lineline.c to make sure that both detections are roughly pointing at the same spot in space
    * Oooh hmm; I wonder how this will work when you have more than two views. What's the least-squares version of finding the closest intersection?
  * Every frame, discard any hands that are overlapping too much, by IoU and by if the 3D keypoints are too close together. Roughly the same as #1 but you could have a different factor for it.
* Try optimizing both hands at once so that we can
  * Add a term for if any joint is too close to any other - this *might* have too high runtime impact though, hmm
  * Make the hand size calibration simpler
* Get a better understanding of how long the linear solver takes- O(n)? O(n^2)?
* EuRoC dataset recording/playback - where should that actually go in the tree?
* Metrics!
  * PCK - percentage of keypoints in ground truth where prediction exists and is reasonably close. Should be good to see if the whole pipeline is performing well, but not very good for trying to evaluate one part at a time.
  * Another metric to describe how accurate the correct keypoints are
  * Another to describe wobbliness?

