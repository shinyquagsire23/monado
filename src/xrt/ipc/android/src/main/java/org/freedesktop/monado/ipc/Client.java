// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Implementation of the Monado AIDL server
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_android
 */


package org.freedesktop.monado.ipc;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;

import androidx.annotation.Keep;

import java.io.IOException;

/**
 * Provides the client-side code to initiate connection to Monado IPC service.
 * <p>
 * This class will get loaded into the OpenXR client application by our native code.
 */
@Keep
public class Client implements ServiceConnection {
    private static final String TAG = "monado-ipc-client";

    /**
     * Context provided by app.
     */
    private Context context;

    /**
     * Pointer to local IPC proxy: calling methods on it automatically transports arguments across binder IPC.
     * <p>
     * May be null!
     */
    public IMonado monado;

    /**
     * "Our" side of the socket pair - the other side is sent to the server automatically on connection.
     */
    public ParcelFileDescriptor fd;

    /**
     * Indicates that we tried to connect but failed.
     * <p>
     * Used to distinguish a "not yet fully connected" null monado member from a "tried and failed"
     * null monado member.
     */
    public boolean failed = false;

    /**
     * Bind to the Monado IPC service - this asynchronously starts connecting (and launching the
     * service if it's not already running)
     * <p>
     * The IPC client code on Android should load this class (from the right package), instantiate
     * this class, and call this method.
     *
     * @param context_    Context to use to make the connection. (We get the application context
     *                    from it.)
     * @param packageName The package name containing the Monado runtime. The caller is guaranteed
     *                    to know this because it had to load this class from that package.
     *                    (Often "org.freedesktop.monado.openxr.out_of_process" for now, at least)
     * @todo how to get the right package name here? Do we have to go so far as to re-enumerate ourselves?
     * <p>
     * Various builds, variants, etc. will have different package names, but we must specify the
     * package name explicitly to avoid violating security restrictions.
     */
    public void bind(Context context_, String packageName) {
        context = context_.getApplicationContext();
        if (context == null) {
            // in case app context returned null
            context = context_;
        }
        context.bindService(
                new Intent("org.freedesktop.monado.CONNECT")
                        .setPackage(packageName),
                this, Context.BIND_AUTO_CREATE);
        // does not bind right away! This takes some time.
    }

    /**
     * Some on-failure cleanup.
     */
    private void handleFailure() {
        failed = true;
        if (context != null) context.unbindService(this);
        monado = null;
    }

    /**
     * Handle the asynchronous connection of the binder IPC.
     * <p>
     * This sets up the class member `monado`, as well as the member `fd`. It calls
     * `IMonado.connect()` automatically. The client still needs to call `IMonado.passAppSurface()`
     * on `monado`.
     *
     * @param name    should match the intent above, but not used.
     * @param service the associated service, which we cast in this function.
     */
    @Override
    public void onServiceConnected(ComponentName name, IBinder service) {
        monado = IMonado.Stub.asInterface(service);
        ParcelFileDescriptor theirs;
        try {
            ParcelFileDescriptor[] fds = ParcelFileDescriptor.createSocketPair();
            fd = fds[0];
            theirs = fds[1];
        } catch (IOException e) {
            e.printStackTrace();
            Log.e(TAG, "could not create socket pair: " + e.toString());
            handleFailure();
            return;
        }
        try {
            monado.connect(theirs);
        } catch (RemoteException e) {
            e.printStackTrace();
            Log.e(TAG, "could not call IMonado.connect: " + e.toString());
            handleFailure();
        }
    }

    /**
     * Handle asynchronous disconnect.
     *
     * @param name should match the intent above, but not used.
     */
    @Override
    public void onServiceDisconnected(ComponentName name) {
        monado = null;
    }

    /*
     * @todo do we need to watch for a disconnect here?
     *   https://stackoverflow.com/questions/18078914/notify-an-android-service-when-a-bound-client-disconnects
     *
     * Our existing native disconnect handling might be sufficient.
     */
}
