// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "android.app.h"

namespace wrap {
namespace android::content {
class ComponentName;
class Context;
} // namespace android::content

} // namespace wrap

namespace wrap {
namespace android::service::vr {
/*!
 * Wrapper for android.service.vr.VrListenerService objects.
 */
class VrListenerService : public app::Service {
  public:
    using Service::Service;
    static constexpr const char *getTypeName() noexcept {
        return "android/service/vr/VrListenerService";
    }

    /*!
     * Wrapper for the isVrModePackageEnabled static method
     *
     * Java prototype:
     * `public static final boolean
     * isVrModePackageEnabled(android.content.Context,
     * android.content.ComponentName);`
     *
     * JNI signature:
     * (Landroid/content/Context;Landroid/content/ComponentName;)Z
     *
     */
    static bool
    isVrModePackageEnabled(content::Context const &context,
                           content::ComponentName const &componentName);

    /*!
     * Class metadata
     */
    struct Meta : public MetaBase {
        jni::method_t isVrModePackageEnabled;

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
} // namespace android::service::vr
} // namespace wrap
#include "android.service.vr.impl.h"
