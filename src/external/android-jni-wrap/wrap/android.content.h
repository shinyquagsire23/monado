// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "ObjectWrapperBase.h"
#include <string>

namespace wrap {
namespace android::content {
class ComponentName;
class Context;
class Intent;
} // namespace android::content

namespace android::content::pm {
class PackageManager;
} // namespace android::content::pm

namespace android::os {
class Bundle;
} // namespace android::os

namespace java::lang {
class Class;
class ClassLoader;
} // namespace java::lang

} // namespace wrap

namespace wrap {
namespace android::content {
/*!
 * Wrapper for android.content.Context objects.
 */
class Context : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/content/Context";
    }

    /*!
     * Getter for the DISPLAY_SERVICE static field value
     *
     * Java prototype:
     * `public static final java.lang.String DISPLAY_SERVICE;`
     *
     * JNI signature: Ljava/lang/String;
     *
     */
    static std::string DISPLAY_SERVICE();

    /*!
     * Getter for the WINDOW_SERVICE static field value
     *
     * Java prototype:
     * `public static final java.lang.String WINDOW_SERVICE;`
     *
     * JNI signature: Ljava/lang/String;
     *
     */
    static std::string WINDOW_SERVICE();

    /*!
     * Wrapper for the getPackageManager method
     *
     * Java prototype:
     * `public abstract android.content.pm.PackageManager getPackageManager();`
     *
     * JNI signature: ()Landroid/content/pm/PackageManager;
     *
     */
    pm::PackageManager getPackageManager();

    /*!
     * Wrapper for the getApplicationContext method
     *
     * Java prototype:
     * `public abstract android.content.Context getApplicationContext();`
     *
     * JNI signature: ()Landroid/content/Context;
     *
     */
    Context getApplicationContext();

    /*!
     * Wrapper for the getClassLoader method
     *
     * Java prototype:
     * `public abstract java.lang.ClassLoader getClassLoader();`
     *
     * JNI signature: ()Ljava/lang/ClassLoader;
     *
     */
    java::lang::ClassLoader getClassLoader();

    /*!
     * Wrapper for the startActivity method
     *
     * Java prototype:
     * `public abstract void startActivity(android.content.Intent);`
     *
     * JNI signature: (Landroid/content/Intent;)V
     *
     */
    void startActivity(Intent const &intent);

    /*!
     * Wrapper for the startActivity method
     *
     * Java prototype:
     * `public abstract void startActivity(android.content.Intent,
     * android.os.Bundle);`
     *
     * JNI signature: (Landroid/content/Intent;Landroid/os/Bundle;)V
     *
     */
    void startActivity(Intent const &intent, os::Bundle const &bundle);

    /*!
     * Wrapper for the createPackageContext method
     *
     * Java prototype:
     * `public abstract android.content.Context
     * createPackageContext(java.lang.String, int) throws
     * android.content.pm.PackageManager$NameNotFoundException;`
     *
     * JNI signature: (Ljava/lang/String;I)Landroid/content/Context;
     *
     */
    Context createPackageContext(std::string const &packageName, int32_t flags);

    enum {
        CONTEXT_INCLUDE_CODE = 1,
        CONTEXT_IGNORE_SECURITY = 2,
    };

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        impl::StaticFieldId<std::string> DISPLAY_SERVICE;
        impl::StaticFieldId<std::string> WINDOW_SERVICE;
        jni::method_t getPackageManager;
        jni::method_t getApplicationContext;
        jni::method_t getClassLoader;
        jni::method_t startActivity;
        jni::method_t startActivity1;
        jni::method_t createPackageContext;

        /*!
         * Singleton accessor
         */
        static Meta &data(bool deferDrop = false) {
            static Meta instance{deferDrop};
            return instance;
        }

      private:
        explicit Meta(bool deferDrop);
    };
};
/*!
 * Wrapper for android.content.ComponentName objects.
 */
class ComponentName : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/content/ComponentName";
    }

    /*!
     * Wrapper for a constructor
     *
     * Java prototype:
     * `public android.content.ComponentName(java.lang.String,
     * java.lang.String);`
     *
     * JNI signature: (Ljava/lang/String;Ljava/lang/String;)V
     *
     */
    static ComponentName construct(std::string const &pkg,
                                   std::string const &cls);

    /*!
     * Wrapper for a constructor
     *
     * Java prototype:
     * `public android.content.ComponentName(android.content.Context,
     * java.lang.String);`
     *
     * JNI signature: (Landroid/content/Context;Ljava/lang/String;)V
     *
     */
    static ComponentName construct(Context const &pkg, std::string const &cls);

    /*!
     * Wrapper for a constructor
     *
     * Java prototype:
     * `public android.content.ComponentName(android.content.Context,
     * java.lang.Class<?>);`
     *
     * JNI signature: (Landroid/content/Context;Ljava/lang/Class;)V
     *
     */
    static ComponentName construct(Context const &pkg,
                                   java::lang::Class const &cls);

    /*!
     * Wrapper for a constructor
     *
     * Java prototype:
     * `public android.content.ComponentName(android.os.Parcel);`
     *
     * JNI signature: (Landroid/os/Parcel;)V
     *
     */
    static ComponentName construct(jni::Object const &parcel);

    /*!
     * Class metadata
     */
    struct Meta : public MetaBase {
        jni::method_t init;
        jni::method_t init1;
        jni::method_t init2;
        jni::method_t init3;

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
 * Wrapper for android.content.Intent objects.
 */
class Intent : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/content/Intent";
    }

    /*!
     * Getter for the FLAG_ACTIVITY_NEW_TASK static field value
     *
     * Java prototype:
     * `public static final int FLAG_ACTIVITY_NEW_TASK;`
     *
     * JNI signature: I
     *
     */
    static int32_t FLAG_ACTIVITY_NEW_TASK();

#if 0
    // disabled because of zero-length array warning in jnipp
    /*!
     * Wrapper for a constructor
     *
     * Java prototype:
     * `public android.content.Intent();`
     *
     * JNI signature: ()V
     *
     */
    static Intent construct();
#endif

    /*!
     * Wrapper for a constructor
     *
     * Java prototype:
     * `public android.content.Intent(android.content.Intent);`
     *
     * JNI signature: (Landroid/content/Intent;)V
     *
     */
    static Intent construct(Intent &intent);

    /*!
     * Wrapper for a constructor
     *
     * Java prototype:
     * `public android.content.Intent(java.lang.String);`
     *
     * JNI signature: (Ljava/lang/String;)V
     *
     */
    static Intent construct(std::string const &action);

    /*!
     * Wrapper for a constructor
     *
     * Java prototype:
     * `public android.content.Intent(java.lang.String, android.net.Uri);`
     *
     * JNI signature: (Ljava/lang/String;Landroid/net/Uri;)V
     *
     */
    static Intent construct(std::string const &action, jni::Object const &uri);

    /*!
     * Wrapper for a constructor
     *
     * Java prototype:
     * `public android.content.Intent(android.content.Context,
     * java.lang.Class<?>);`
     *
     * JNI signature: (Landroid/content/Context;Ljava/lang/Class;)V
     *
     */
    static Intent construct(Context const &context,
                            java::lang::Class const &classParam);

    /*!
     * Wrapper for a constructor
     *
     * Java prototype:
     * `public android.content.Intent(java.lang.String, android.net.Uri,
     * android.content.Context, java.lang.Class<?>);`
     *
     * JNI signature:
     * (Ljava/lang/String;Landroid/net/Uri;Landroid/content/Context;Ljava/lang/Class;)V
     *
     */
    static Intent construct(std::string const &action, jni::Object const &uri,
                            Context const &context,
                            java::lang::Class const &classParam);

    /*!
     * Wrapper for the setFlags method
     *
     * Java prototype:
     * `public android.content.Intent setFlags(int);`
     *
     * JNI signature: (I)Landroid/content/Intent;
     *
     */
    Intent setFlags(int32_t flags);

    /*!
     * Class metadata
     */
    struct Meta : public MetaBase {
        impl::StaticFieldId<int32_t> FLAG_ACTIVITY_NEW_TASK;
        jni::method_t init;
        jni::method_t init1;
        jni::method_t init2;
        jni::method_t init3;
        jni::method_t init4;
        jni::method_t init5;
        jni::method_t setFlags;

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
} // namespace android::content
} // namespace wrap
#include "android.content.impl.h"
