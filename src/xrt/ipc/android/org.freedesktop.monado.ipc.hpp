// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Partially-generated wrapper for the
 * `org.freedesktop.monado.ipc` Java package.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_android
 */

#pragma once

#include "wrap/ObjectWrapperBase.h"

namespace wrap {
namespace android::content {
	class Context;
} // namespace android::content

namespace android::view {
	class Surface;
} // namespace android::view

namespace org::freedesktop::monado::ipc {
	class Client;
	class IMonado;
} // namespace org::freedesktop::monado::ipc

} // namespace wrap

namespace wrap {
namespace org::freedesktop::monado::ipc {
	/*!
	 * Wrapper for org.freedesktop.monado.ipc.Client objects.
	 */
	class Client : public ObjectWrapperBase
	{
	public:
		using ObjectWrapperBase::ObjectWrapperBase;
		static constexpr const char *
		getTypeName() noexcept
		{
			return "org/freedesktop/monado/ipc/Client";
		}

		static constexpr const char *
		getFullyQualifiedTypeName() noexcept
		{
			return "org.freedesktop.monado.ipc.Client";
		}

		/*!
		 * Getter for the monado field value
		 *
		 * Java prototype:
		 * `public org.freedesktop.monado.ipc.IMonado monado;`
		 *
		 * JNI signature: Lorg/freedesktop/monado/ipc/IMonado;
		 *
		 */
		IMonado
		getMonado() const;

		/*!
		 * Getter for the failed field value
		 *
		 * Java prototype:
		 * `public boolean failed;`
		 *
		 * JNI signature: Z
		 *
		 */
		bool
		getFailed() const;

		/*!
		 * Wrapper for a constructor
		 *
		 * Java prototype:
		 * `public org.freedesktop.monado.ipc.Client(long);`
		 *
		 * JNI signature: (J)V
		 *
		 */
		static Client
		construct(void *nativePointer);

		/*!
		 * Wrapper for the markAsDiscardedByNative method
		 *
		 * Java prototype:
		 * `public void markAsDiscardedByNative();`
		 *
		 * JNI signature: ()V
		 *
		 */
		void
		markAsDiscardedByNative();

		/*!
		 * Wrapper for the blockingConnect method
		 *
		 * Java prototype:
		 * `public int blockingConnect(android.content.Context,
		 * java.lang.String);`
		 *
		 * JNI signature: (Landroid/content/Context;Ljava/lang/String;)I
		 *
		 */
		int32_t
		blockingConnect(android::content::Context const &context, std::string const &packageName);

		/*!
		 * Initialize the static metadata of this wrapper with a known
		 * (non-null) Java class.
		 */
		static void
		staticInitClass(jni::jclass clazz)
		{
			Meta::data(clazz);
		}

		/*!
		 * Class metadata
		 */
		struct Meta : public MetaBase
		{
			impl::WrappedFieldId<IMonado> monado;
			impl::FieldId<bool> failed;
			jni::method_t init;
			jni::method_t markAsDiscardedByNative;
			jni::method_t blockingConnect;

			/*!
			 * Singleton accessor
			 */
			static Meta &
			data(jni::jclass clazz = nullptr)
			{
				static Meta instance{clazz};
				return instance;
			}

		private:
			Meta(jni::jclass clazz = nullptr);
		};
	};
	/*!
	 * Wrapper for org.freedesktop.monado.ipc.IMonado objects.
	 */
	class IMonado : public ObjectWrapperBase
	{
	public:
		using ObjectWrapperBase::ObjectWrapperBase;
		static constexpr const char *
		getTypeName() noexcept
		{
			return "org/freedesktop/monado/ipc/IMonado";
		}

		/*!
		 * Wrapper for the passAppSurface method
		 *
		 * Java prototype:
		 * `public abstract void passAppSurface(android.view.Surface)
		 * throws android.os.RemoteException;`
		 *
		 * JNI signature: (Landroid/view/Surface;)V
		 *
		 */
		void
		passAppSurface(android::view::Surface const &surface);

		/*!
		 * Class metadata
		 */
		struct Meta : public MetaBase
		{
			jni::method_t passAppSurface;

			/*!
			 * Singleton accessor
			 */
			static Meta &
			data()
			{
				static Meta instance;
				return instance;
			}

		private:
			Meta(jni::jclass clazz = nullptr);
		};
	};
} // namespace org::freedesktop::monado::ipc
} // namespace wrap
#include "org.freedesktop.monado.ipc.impl.hpp"
