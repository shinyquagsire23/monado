// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "ObjectWrapperBase.h"

namespace wrap {
namespace dalvik::system {
class DexClassLoader;
} // namespace dalvik::system

namespace java::lang {
class ClassLoader;
} // namespace java::lang

} // namespace wrap

namespace wrap {
namespace dalvik::system {
/*!
 * Wrapper for dalvik.system.DexClassLoader objects.
 */
class DexClassLoader : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "dalvik/system/DexClassLoader";
    }

    /*!
     * Wrapper for a constructor
     *
     * Java prototype:
     * `public dalvik.system.DexClassLoader(java.lang.String, java.lang.String,
     * java.lang.String, java.lang.ClassLoader);`
     *
     * JNI signature:
     * (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V
     *
     */
    static DexClassLoader construct(std::string const &searchPath,
                                    std::string const &nativeSearchPath,
                                    jni::Object parentClassLoader);

    /*!
     * Class metadata
     */
    struct Meta : public MetaBase {
        jni::method_t init;

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
} // namespace dalvik::system
} // namespace wrap
#include "dalvik.system.impl.h"
