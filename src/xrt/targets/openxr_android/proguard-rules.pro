# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
# see http://developer.android.com/guide/developing/tools/proguard.html
# Trying to keep most of them in source code annotations and let Gradle do the work

# For library auto-detection in AboutLibraries
-keep class .R
-keep class **.R$* {
    <fields>;
}
