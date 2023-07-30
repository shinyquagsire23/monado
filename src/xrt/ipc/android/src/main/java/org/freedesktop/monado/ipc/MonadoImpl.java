// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Implementation of the Monado AIDL server
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_android
 */

package org.freedesktop.monado.ipc;

import android.content.Context;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.provider.Settings;
import android.util.Log;
import android.view.Surface;

import androidx.annotation.Keep;
import androidx.annotation.NonNull;

import java.io.IOException;

/**
 * Java implementation of the IMonado IPC interface.
 *
 * <p>(This is the server-side code.)
 *
 * <p>All this does is delegate calls to native JNI implementations. The warning suppression is
 * because Android Studio tends to have a hard time finding the (very real) implementations of these
 * in service-lib.
 */
@Keep
public class MonadoImpl extends IMonado.Stub {

    private static final String TAG = "MonadoImpl";

    static {
        // Load the shared library with the native parts of this class
        // This is the service-lib target.
        System.loadLibrary("monado-service");
    }

    private final Context context;

    public MonadoImpl(@NonNull Context context) {
        this.context = context.getApplicationContext();
        nativeStartServer(this.context);
    }

    @Override
    public void connect(@NonNull ParcelFileDescriptor parcelFileDescriptor) throws RemoteException {
        int fd = parcelFileDescriptor.getFd();
        Log.i(TAG, "connect: given fd " + fd);
        if (nativeAddClient(fd) != 0) {
            Log.e(TAG, "Failed to transfer client fd ownership!");
            try {
                parcelFileDescriptor.close();
            } catch (IOException e) {
                // do nothing, probably already closed.
            }

            // throw an exception so that client can gracefully fail
            throw new IllegalStateException("server not available");
        } else {
            Log.i(TAG, "connect: fd ownership transferred");
            parcelFileDescriptor.detachFd();
        }
    }

    @Override
    public void passAppSurface(Surface surface) {
        Log.i(TAG, "passAppSurface");
        if (surface == null) {
            Log.e(TAG, "Received a null Surface from the client!");
            return;
        }
        nativeAppSurface(surface);
    }

    @Override
    public boolean canDrawOverOtherApps() {
        Log.i(TAG, "canDrawOverOtherApps");
        return Settings.canDrawOverlays(context);
    }

    public void shutdown() {
        Log.i(TAG, "shutdown");
        nativeShutdownServer();
    }

    /**
     * Native method that starts server.
     *
     * <p>This is essentially the entry point for the monado service on Android.
     *
     * <p>
     *
     * @param context Context object.
     */
    @SuppressWarnings("JavaJniMissingFunction")
    private native void nativeStartServer(@NonNull Context context);

    /**
     * Native handling of receiving a surface: should convert it to an ANativeWindow then do stuff
     * with it.
     *
     * <p>Ignore warnings that this function is missing: it is not, it is just in a different
     * module. See `src/xrt/targets/service-lib/service_target.cpp` for the implementation.
     *
     * @param surface The surface to pass to native code
     * @todo figure out a good way to have a client ID
     */
    @SuppressWarnings("JavaJniMissingFunction")
    private native void nativeAppSurface(@NonNull Surface surface);

    /**
     * Native handling of receiving an FD for a new client: the FD should be used to start up the
     * rest of the native IPC code on that socket.
     *
     * <p>Ignore warnings that this function is missing: it is not, it is just in a different
     * module. See `src/xrt/targets/service-lib/service_target.cpp` for the implementation.
     *
     * @param fd The incoming file descriptor: ownership is transferred to native code here.
     * @return 0 on success, anything else means the fd wasn't sent and ownership not transferred.
     * @todo figure out a good way to have a client ID
     */
    @SuppressWarnings("JavaJniMissingFunction")
    private native int nativeAddClient(int fd);

    /**
     * Native method that handles shutdown server.
     *
     * @return 0 on success; -1 means that server didn't start.
     */
    @SuppressWarnings("JavaJniMissingFunction")
    private native int nativeShutdownServer();
}
