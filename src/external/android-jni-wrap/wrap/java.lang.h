// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "ObjectWrapperBase.h"

namespace wrap {
namespace java::lang {
class Class;
class ClassLoader;
} // namespace java::lang

} // namespace wrap

namespace wrap {
namespace java::lang {
/*!
 * Wrapper for java.lang.Class objects.
 */
class Class : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "java/lang/Class";
    }

    /*!
     * Wrapper for the forName static method
     *
     * Java prototype:
     * `public static java.lang.Class<?> forName(java.lang.String) throws
     * java.lang.ClassNotFoundException;`
     *
     * JNI signature: (Ljava/lang/String;)Ljava/lang/Class;
     *
     */
    static Class forName(std::string &stringParam);

    /*!
     * Wrapper for the forName static method
     *
     * Java prototype:
     * `public static java.lang.Class<?> forName(java.lang.String, boolean,
     * java.lang.ClassLoader) throws java.lang.ClassNotFoundException;`
     *
     * JNI signature:
     * (Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;
     *
     */
    static Class forName(std::string const &name, bool initialize,
                         jni::Object classLoader);

    //! @overload
    static Class forName(jstring name, bool initialize,
                         jni::Object classLoader);
    /*!
     * Wrapper for the forName static method
     *
     * Java prototype:
     * `public static java.lang.Class<?> forName(java.lang.Module,
     * java.lang.String);`
     *
     * JNI signature: (Ljava/lang/Module;Ljava/lang/String;)Ljava/lang/Class;
     *
     */
    static Class forName(jni::Object const &module, std::string const &name);

    /*!
     * Wrapper for the getCanonicalName method
     *
     * Java prototype:
     * `public java.lang.String getCanonicalName();`
     *
     * JNI signature: ()Ljava/lang/String;
     *
     */
    std::string getCanonicalName();

    /*!
     * Class metadata
     */
    struct Meta : public MetaBase {
        jni::method_t forName;
        jni::method_t forName1;
        jni::method_t forName2;
        jni::method_t getCanonicalName;

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
 * Wrapper for java.lang.ClassLoader objects.
 */
class ClassLoader : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "java/lang/ClassLoader";
    }

    /*!
     * Wrapper for the loadClass method
     *
     * Java prototype:
     * `public java.lang.Class<?> loadClass(java.lang.String) throws
     * java.lang.ClassNotFoundException;`
     *
     * JNI signature: (Ljava/lang/String;)Ljava/lang/Class;
     *
     */
    Class loadClass(std::string const &name);

    //! @overload
    Class loadClass(jstring name);

    /*!
     * Wrapper for the findLibrary method
     *
     * JNI signature: (Ljava/lang/String;)Ljava/lang/String;
     *
     */
    std::string findLibrary(std::string const &name);

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        jni::method_t loadClass;
        jni::method_t findLibrary;

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
 * Wrapper for java.lang.System objects.
 */
class System : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "java/lang/System";
    }

    /*!
     * Wrapper for the mapLibraryName static method
     *
     * Java prototype:
     * `public static native java.lang.String mapLibraryName(java.lang.String);`
     *
     * JNI signature: (Ljava/lang/String;)Ljava/lang/String;
     *
     */
    static std::string mapLibraryName(std::string const &name);

    /*!
     * Class metadata
     */
    struct Meta : public MetaBase {
        jni::method_t mapLibraryName;

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
} // namespace java::lang
} // namespace wrap
#include "java.lang.impl.h"
