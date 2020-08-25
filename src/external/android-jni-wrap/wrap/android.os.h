// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "ObjectWrapperBase.h"

namespace wrap {
namespace android::os {
class ParcelFileDescriptor;
} // namespace android::os

} // namespace wrap

namespace wrap {
namespace android::os {
/*!
 * Wrapper for android.os.BaseBundle objects.
 */
class BaseBundle : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/os/BaseBundle";
    }

    /*!
     * Wrapper for the containsKey method
     *
     * Java prototype:
     * `public boolean containsKey(java.lang.String);`
     *
     * JNI signature: (Ljava/lang/String;)Z
     *
     */
    bool containsKey(std::string const &key);

    /*!
     * Wrapper for the getString method
     *
     * Java prototype:
     * `public java.lang.String getString(java.lang.String);`
     *
     * JNI signature: (Ljava/lang/String;)Ljava/lang/String;
     *
     */
    std::string getString(std::string const &key);

    /*!
     * Wrapper for the getString method
     *
     * Java prototype:
     * `public java.lang.String getString(java.lang.String, java.lang.String);`
     *
     * JNI signature: (Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;
     *
     */
    std::string getString(std::string const &key,
                          std::string const &defaultValue);

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        jni::method_t containsKey;
        jni::method_t getString;
        jni::method_t getString1;

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
/*!
 * Wrapper for android.os.Bundle objects.
 */
class Bundle : public BaseBundle {
  public:
    using BaseBundle::BaseBundle;
    static constexpr const char *getTypeName() noexcept {
        return "android/os/Bundle";
    }

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {

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

/*!
 * Wrapper for android.os.ParcelFileDescriptor objects.
 */
class ParcelFileDescriptor : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/os/ParcelFileDescriptor";
    }

    /*!
     * Wrapper for the adoptFd static method
     *
     * Java prototype:
     * `public static android.os.ParcelFileDescriptor adoptFd(int);`
     *
     * JNI signature: (I)Landroid/os/ParcelFileDescriptor;
     *
     */
    static ParcelFileDescriptor adoptFd(int fd);

    /*!
     * Wrapper for the getFd method
     *
     * Java prototype:
     * `public int getFd();`
     *
     * JNI signature: ()I
     *
     */
    int getFd() const;

    /*!
     * Wrapper for the detachFd method
     *
     * Java prototype:
     * `public int detachFd();`
     *
     * JNI signature: ()I
     *
     */
    int detachFd();

    /*!
     * Wrapper for the close method
     *
     * Java prototype:
     * `public void close() throws java.io.IOException;`
     *
     * JNI signature: ()V
     *
     */
    void close();

    /*!
     * Wrapper for the checkError method
     *
     * Java prototype:
     * `public void checkError() throws java.io.IOException;`
     *
     * JNI signature: ()V
     *
     */
    void checkError() const;

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        jni::method_t adoptFd;
        jni::method_t getFd;
        jni::method_t detachFd;
        jni::method_t close;
        jni::method_t checkError;

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
} // namespace android::os
} // namespace wrap
#include "android.os.impl.h"
