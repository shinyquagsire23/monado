// Copyright 2022, Qualcomm Innovation Center, Inc.
// SPDX-License-Identifier: BSL-1.0
// Author: Jarvis Huang

#pragma once

#include "ObjectWrapperBase.h"

namespace wrap {
namespace java::io {
/*!
 * Wrapper for java.io.File objects.
 */
class File : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "java/io/File";
    }

    /*!
     * Wrapper for the getAbsolutePath method
     *
     * Java prototype:
     * `public java.lang.String getAbsolutePath();`
     *
     * JNI signature: ()Ljava/lang/String;
     *
     */
    std::string getAbsolutePath() const;

    /*!
     * Class metadata
     */
    struct Meta : public MetaBase {
        jni::method_t getAbsolutePath ;

        /*!
         * Singleton accessor
         */
        static Meta &data() {
            static Meta instance{};
            return instance;
        }

      private:
        Meta();
    };
};

} // namespace java::io
} // namespace wrap
#include "java.io.impl.h"
