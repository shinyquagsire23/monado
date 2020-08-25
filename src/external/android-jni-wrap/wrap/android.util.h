// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "ObjectWrapperBase.h"

namespace wrap {
namespace android::util {
/*!
 * Wrapper for android.util.DisplayMetrics objects.
 */
class DisplayMetrics : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/util/DisplayMetrics";
    }

    /*!
     * Getter for the heightPixels field value
     *
     * Java prototype:
     * `public int heightPixels;`
     *
     * JNI signature: I
     *
     */
    int32_t getHeightPixels() const;

    /*!
     * Getter for the widthPixels field value
     *
     * Java prototype:
     * `public int widthPixels;`
     *
     * JNI signature: I
     *
     */
    int32_t getWidthPixels() const;

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        impl::FieldId<int32_t> heightPixels;
        impl::FieldId<int32_t> widthPixels;

        /*!
         * Singleton accessor
         */
        static Meta &data() {
            static Meta instance;
            return instance;
        }

      private:
        Meta();
    };
};
} // namespace android::util
} // namespace wrap
#include "android.util.impl.h"
