/** @file
    @brief Header declaring a C++11 `finally` or "scope-guard" construct.

    Inspirations are many - Alexandrescu's original and C++11 ScopeGuard, and
    the Guideline Support Library's (GSL) `final_act`/`finally`
    https://github.com/Microsoft/GSL/blob/0cf947db7760bf5756e4cb0d47c72a257ed527c5/include/gsl_util.h
    come to mind, but this has been written and re-written numerous times
    since its introduction into the C++ global consciousness, and this
    implementation was written independently after I couldn't find a
    previous independent implementation I had written a few weeks earlier in
    an implementation file. -- Ryan Pavlik

    See UniqueDestructionActionWrapper for a "generalized" (in some sense)
    version of this.

    Originally written for use in OSVR for Sensics <http://sensics.com/osvr>,
    relicensed to BSL 1.0 with permission.

    This header is maintained as a part of 'util-headers' - you can always
    find the latest version online at https://github.com/rpavlik/util-headers

    This GUID can help identify the project: d1dbc94e-e863-49cf-bc08-ab4d9f486613

    This copy of the header is from the revision that Git calls
    1a8444782d15cb9458052e3d8251c4f5b8e808d5

    Commit date: "2022-03-11 12:11:32 -0600"

    @date 2016

    @author
    Ryan Pavlik
    <ryan.pavlik@gmail.com>
    <http://ryanpavlik.com>
*/

// Copyright 2016, Sensics, Inc.
//
// SPDX-License-Identifier: BSL-1.0
//
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef INCLUDED_Finally_h_GUID_D925FE58_9C57_448B_C0BB_19A42B3243BA
#define INCLUDED_Finally_h_GUID_D925FE58_9C57_448B_C0BB_19A42B3243BA

// Internal Includes
// - none

// Library/third-party includes
// - none

// Standard includes
#include <utility>

namespace util {
namespace detail {
    /// Allows you to run a callable something at the end of a scope.
    ///
    /// The class that provides the scope-guard behavior. Often not referred to
    /// by name because auto is useful here, and often not created by a direct
    /// constructor call because of the finally() convenience functions combined
    /// with lambdas.
    template <typename F> class FinalTask {
    public:
        /// Explicit constructor from something callable.
        explicit FinalTask(F f) : f_(std::move(f)) {}

        /// Move constructor - cancels the moved-from task.
        FinalTask(FinalTask &&other) : f_(std::move(other.f_)), do_(other.do_) {
            other.cancel();
        }

        /// non-copyable
        FinalTask(FinalTask const &) = delete;

        /// non-assignable
        FinalTask &operator=(FinalTask const &) = delete;

        /// Destructor - if we haven't been cancelled, do our callable thing.
        ~FinalTask() {
            if (do_) {
                f_();
            }
        }
        /// Cancel causes us to not do our final task on destruction.
        void cancel() { do_ = false; }

    private:
        /// Our callable task to do at destruction.
        F f_;
        /// Whether we should actually do it.
        bool do_ = true;
    };
} // namespace detail

/// Creation free function for final tasks to run on scope exit. Works great
/// when paired with lambdas (particularly with `[&]` reference capture).
/// Use like:
/// `auto f = finally([&]{ dothis(); });` to have `dothis()` called when `f`
/// goes out of scope, no matter how.
template <typename F> inline detail::FinalTask<F> finally(F &&f) {
    /// Perfect forwarding version.
    return detail::FinalTask<F>(std::forward<F>(f));
}

/// @overload
template <typename F> inline detail::FinalTask<F> finally(F const &f) {
    // Added this overload because GSL had it and GSL is supposed to be best
    // practices guidelines...
    return detail::FinalTask<F>(f);
}

} // namespace util

#endif // INCLUDED_Finally_h_GUID_D925FE58_9C57_448B_C0BB_19A42B3243BA
