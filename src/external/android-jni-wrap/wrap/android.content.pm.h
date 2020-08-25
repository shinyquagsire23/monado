// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "ObjectWrapperBase.h"
#include "android.os.h"
#include <string>

namespace wrap {
namespace android::content {
class Intent;
} // namespace android::content

namespace android::content::pm {
class ApplicationInfo;
class PackageInfo;
class ServiceInfo;
} // namespace android::content::pm

namespace android::os {
class Bundle;
} // namespace android::os

namespace java::util {
class List;
} // namespace java::util

} // namespace wrap

namespace wrap {
namespace android::content::pm {
/*!
 * Wrapper for android.content.pm.PackageItemInfo objects.
 */
class PackageItemInfo : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/content/pm/PackageItemInfo";
    }

    /*!
     * Getter for the metaData field value
     *
     * Java prototype:
     * `public android.os.Bundle metaData;`
     *
     * JNI signature: Landroid/os/Bundle;
     *
     */
    os::Bundle getMetaData() const;

    /*!
     * Getter for the name field value
     *
     * Java prototype:
     * `public java.lang.String name;`
     *
     * JNI signature: Ljava/lang/String;
     *
     */
    std::string getName() const;

    /*!
     * Getter for the packageName field value
     *
     * Java prototype:
     * `public java.lang.String packageName;`
     *
     * JNI signature: Ljava/lang/String;
     *
     */
    std::string getPackageName() const;

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        impl::WrappedFieldId<os::Bundle> metaData;
        impl::FieldId<std::string> name;
        impl::FieldId<std::string> packageName;

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
 * Wrapper for android.content.pm.ComponentInfo objects.
 */
class ComponentInfo : public PackageItemInfo {
  public:
    using PackageItemInfo::PackageItemInfo;
    static constexpr const char *getTypeName() noexcept {
        return "android/content/pm/ComponentInfo";
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
 * Wrapper for android.content.pm.ServiceInfo objects.
 */
class ServiceInfo : public PackageItemInfo {
  public:
    using PackageItemInfo::PackageItemInfo;
    static constexpr const char *getTypeName() noexcept {
        return "android/content/pm/ServiceInfo";
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
 * Wrapper for android.content.pm.ApplicationInfo objects.
 */
class ApplicationInfo : public PackageItemInfo {
  public:
    using PackageItemInfo::PackageItemInfo;
    static constexpr const char *getTypeName() noexcept {
        return "android/content/pm/ApplicationInfo";
    }

    /*!
     * Getter for the nativeLibraryDir field value
     *
     * Java prototype:
     * `public java.lang.String nativeLibraryDir;`
     *
     * JNI signature: Ljava/lang/String;
     *
     */
    std::string getNativeLibraryDir() const;

    /*!
     * Getter for the publicSourceDir field value
     *
     * Java prototype:
     * `public java.lang.String publicSourceDir;`
     *
     * JNI signature: Ljava/lang/String;
     *
     */
    std::string getPublicSourceDir() const;

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        impl::FieldId<std::string> nativeLibraryDir;
        impl::FieldId<std::string> publicSourceDir;

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
 * Wrapper for android.content.pm.PackageInfo objects.
 */
class PackageInfo : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/content/pm/PackageInfo";
    }

    /*!
     * Getter for the applicationInfo field value
     *
     * Java prototype:
     * `public android.content.pm.ApplicationInfo applicationInfo;`
     *
     * JNI signature: Landroid/content/pm/ApplicationInfo;
     *
     */
    ApplicationInfo getApplicationInfo() const;

    /*!
     * Getter for the packageName field value
     *
     * Java prototype:
     * `public java.lang.String packageName;`
     *
     * JNI signature: Ljava/lang/String;
     *
     */
    std::string getPackageName() const;

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        impl::WrappedFieldId<ApplicationInfo> applicationInfo;
        impl::FieldId<std::string> packageName;

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
 * Wrapper for android.content.pm.ResolveInfo objects.
 */
class ResolveInfo : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/content/pm/ResolveInfo";
    }

    /*!
     * Getter for the serviceInfo field value
     *
     * Java prototype:
     * `public android.content.pm.ServiceInfo serviceInfo;`
     *
     * JNI signature: Landroid/content/pm/ServiceInfo;
     *
     */
    ServiceInfo getServiceInfo() const;

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        impl::WrappedFieldId<ServiceInfo> serviceInfo;

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
 * Wrapper for android.content.pm.PackageManager objects.
 */
class PackageManager : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/content/pm/PackageManager";
    }

    /*!
     * Wrapper for the getPackageInfo method
     *
     * Java prototype:
     * `public abstract android.content.pm.PackageInfo
     * getPackageInfo(java.lang.String, int) throws
     * android.content.pm.PackageManager$NameNotFoundException;`
     *
     * JNI signature: (Ljava/lang/String;I)Landroid/content/pm/PackageInfo;
     *
     */
    PackageInfo getPackageInfo(std::string const &name, int32_t flags);

#if 0
    // Ambiguous overload until we wrap VersionedPackage
    /*!
     * Wrapper for the getPackageInfo method
     *
     * Java prototype:
     * `public abstract android.content.pm.PackageInfo
     * getPackageInfo(android.content.pm.VersionedPackage, int) throws
     * android.content.pm.PackageManager$NameNotFoundException;`
     *
     * JNI signature:
     * (Landroid/content/pm/VersionedPackage;I)Landroid/content/pm/PackageInfo;
     *
     */
    PackageInfo getPackageInfo(jni::Object &versionedPackage, int32_t flags);

#endif

    /*!
     * Wrapper for the getApplicationInfo method
     *
     * Java prototype:
     * `public abstract android.content.pm.ApplicationInfo
     * getApplicationInfo(java.lang.String, int) throws
     * android.content.pm.PackageManager$NameNotFoundException;`
     *
     * JNI signature: (Ljava/lang/String;I)Landroid/content/pm/ApplicationInfo;
     *
     */
    ApplicationInfo getApplicationInfo(std::string const &packageName,
                                       int32_t flags);

    /*!
     * Wrapper for the queryIntentServices method
     *
     * Java prototype:
     * `public abstract java.util.List<android.content.pm.ResolveInfo>
     * queryIntentServices(android.content.Intent, int);`
     *
     * JNI signature: (Landroid/content/Intent;I)Ljava/util/List;
     *
     */
    java::util::List queryIntentServices(Intent &intent, int32_t intParam);

    enum {
        GET_META_DATA = 128,
        GET_SHARED_LIBRARY_FILES = 1024,
    };

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        jni::method_t getPackageInfo;
        jni::method_t getPackageInfo1;
        jni::method_t getApplicationInfo;
        jni::method_t queryIntentServices;

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
} // namespace android::content::pm
} // namespace wrap
#include "android.content.pm.impl.h"
