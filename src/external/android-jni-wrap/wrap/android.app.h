// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "android.content.h"

namespace wrap {
namespace android::content {
class ComponentName;
} // namespace android::content

} // namespace wrap

namespace wrap {
namespace android::app {
/*!
 * Wrapper for android.app.Service objects.
 */
class Service : public content::Context {
  public:
    using Context::Context;
    static constexpr const char *getTypeName() noexcept {
        return "android/app/Service";
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
 * Wrapper for android.app.Activity objects.
 */
class Activity : public content::Context {
  public:
    using Context::Context;
    static constexpr const char *getTypeName() noexcept {
        return "android/app/Activity";
    }

    /*!
     * Wrapper for the getSystemService method
     *
     * Java prototype:
     * `public java.lang.Object getSystemService(java.lang.String);`
     *
     * JNI signature: (Ljava/lang/String;)Ljava/lang/Object;
     *
     */
    jni::Object getSystemService(std::string const &name);

    /*!
     * Wrapper for the setVrModeEnabled method
     *
     * Java prototype:
     * `public void setVrModeEnabled(boolean, android.content.ComponentName)
     * throws android.content.pm.PackageManager$NameNotFoundException;`
     *
     * JNI signature: (ZLandroid/content/ComponentName;)V
     *
     */
    void setVrModeEnabled(bool enabled,
                          content::ComponentName const &requestedComponent);

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        jni::method_t getSystemService;
        jni::method_t setVrModeEnabled;

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
} // namespace android::app
} // namespace wrap
#include "android.app.impl.h"
