// Copyright 2022, Qualcomm Innovation Center, Inc.
// SPDX-License-Identifier: BSL-1.0
// Author: Jarvis Huang
// Inline implementations: do not include on its own!

#pragma once

namespace wrap {
namespace java::io {

inline std::string File::getAbsolutePath() const {
    assert(!isNull());
    return object().call<std::string>(Meta::data().getAbsolutePath);
}

} // namespace java::io
} // namespace wrap
