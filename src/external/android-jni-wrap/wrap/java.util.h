// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "ObjectWrapperBase.h"

namespace wrap {
namespace java::util {
/*!
 * Wrapper for java.util.List objects.
 */
class List : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "java/util/List";
    }

    /*!
     * Wrapper for the size method
     *
     * Java prototype:
     * `public abstract int size();`
     *
     * JNI signature: ()I
     *
     */
    int32_t size() const;

    /*!
     * Wrapper for the get method
     *
     * Java prototype:
     * `public abstract E get(int);`
     *
     * JNI signature: (I)Ljava/lang/Object;
     *
     */
    jni::Object get(int32_t index) const;

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        jni::method_t size;
        jni::method_t get;

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
} // namespace java::util
} // namespace wrap
#include "java.util.impl.h"
