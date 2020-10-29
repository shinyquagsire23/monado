// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Implementation of the Monado AIDL server
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_android
 */


package org.freedesktop.monado.ipc;

import android.os.ParcelFileDescriptor;
import android.view.Surface;

import androidx.annotation.Keep;

import org.freedesktop.monado.ipc.IMonado.Stub;

/**
 * Java implementation of the IMonado IPC interface.
 *
 * All this does is delegate calls to native JNI implementations
 */
@Keep
public class MonadoImpl extends IMonado.Stub {

    public void connect(ParcelFileDescriptor parcelFileDescriptor) {
        nativeAddClient(parcelFileDescriptor);
    }

    public void passAppSurface(Surface surface) {
        nativeAppSurface(surface);
    }

    /**
     * Native handling of receiving a surface: should convert it to an ANativeWindow then do stuff
     * with it.
     *
     * Ignore Android Studio complaining that this function is missing: it is not, it is just in a
     * different module. See `src/xrt/targets/service-lib/lib.cpp` for the implementation.
     * (Ignore the warning saying that file isn't included in the build: it is, Android Studio
     * is just confused.)
     *
     * @param surface
     * @todo figure out a good way to make the MonadoImpl pointer a client ID
     */
    private native void nativeAppSurface(Surface surface);

    /**
     * Native handling of receiving an FD for a new client: the FD should be used to start up the
     * rest of the native IPC code on that socket.
     *
     * This is essentially the entry point for the monado service on Android: if it's already
     * running, this will be called in it. If it's not already running, a process will be created,
     * and this will be the first native code executed in that process.
     *
     * Ignore Android Studio complaining that this function is missing: it is not, it is just in a
     * different module. See `src/xrt/targets/service-lib/lib.cpp` for the implementation.
     * (Ignore the warning saying that file isn't included in the build: it is, Android Studio
     * is just confused.)
     *
     * @param surface
     * @todo figure out a good way to make the MonadoImpl pointer a client ID
     */
    private native void nativeAddClient(ParcelFileDescriptor parcelFileDescriptor);

    static {
        // Load the shared library with the native parts of this class
        // This is the service-lib target.
        System.loadLibrary("monado-service");
    }
}
