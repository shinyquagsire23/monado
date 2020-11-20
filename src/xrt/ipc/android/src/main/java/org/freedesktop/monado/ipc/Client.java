// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Implementation of the Monado AIDL server
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_android
 */


package org.freedesktop.monado.ipc;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;
import android.view.SurfaceHolder;

import androidx.annotation.Keep;

import org.freedesktop.monado.auxiliary.MonadoView;
import org.freedesktop.monado.auxiliary.NativeCounterpart;

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
     * Used to block native until we have our side of the socket pair.
     */
    private final Object connectSync = new Object();
    /**
     * Keep track of the ipc_client_android instance over on the native side.
     */
    private final NativeCounterpart nativeCounterpart;
    /**
     * Pointer to local IPC proxy: calling methods on it automatically transports arguments across binder IPC.
     * <p>
     * May be null!
     */
    @Keep
    public IMonado monado = null;
    /**
     * Indicates that we tried to connect but failed.
     * <p>
     * Used to distinguish a "not yet fully connected" null monado member from a "tried and failed"
     * null monado member.
     */
    @Keep
    public boolean failed = false;
    /**
     * "Our" side of the socket pair - the other side is sent to the server automatically on connection.
     */
    private ParcelFileDescriptor fd = null;
    /**
     * Context provided by app.
     */
    private Context context = null;
    /**
     * Context of the runtime package
     */
    private Context runtimePackageContext = null;
    /**
     * Intent for connecting to service
     */
    private Intent intent = null;

    private SurfaceHolder surfaceHolder;

    /**
     * Constructor
     *
     * @param nativePointer the corresponding native object's pointer.
     */
    @Keep
    public Client(long nativePointer) {
        this.nativeCounterpart = new NativeCounterpart(nativePointer);
        this.nativeCounterpart.markAsUsedByNativeCode();
    }

    private void shutdown() {
        monado = null;
        if (context != null) {
            context.unbindService(this);
        }
        if (intent != null) {
            context.stopService(intent);
        }
        intent = null;

        //! @todo do we close this first?
        fd = null;
    }

    /**
     * Let the native code notify us that it is no longer using this class.
     */
    @Keep
    public void markAsDiscardedByNative() {
        nativeCounterpart.markAsDiscardedByNative(TAG);
        shutdown();
    }

    /**
     * Bind to the Monado IPC service, and block until it is fully connected.
     * <p>
     * The IPC client code on Android should load this class (from the right package), instantiate
     * this class (retaining a reference to it!), and call this method.
     *
     * @param context_    Context to use to make the connection. (We get the application context
     *                    from it.)
     * @param packageName The package name containing the Monado runtime. The caller is guaranteed
     *                    to know this because it had to load this class from that package.
     *                    There's a define in xrt_config_android.h to use for this.
     * @return the fd number - do not close! (dup if you want to be able to close it) Returns -1 if
     * something went wrong.
     * <p>
     * Various builds, variants, etc. will have different package names, but we must specify the
     * package name explicitly to avoid violating security restrictions.
     */
    @Keep
    public int blockingConnect(Context context_, String packageName) {
        Log.i(TAG, "blockingConnect");

        Activity activity = (Activity) context_;

        MonadoView monadoView = MonadoView.attachToActivity(activity);
        surfaceHolder = monadoView.waitGetSurfaceHolder(2000);

        synchronized (connectSync) {
            if (!bind(context_, packageName)) {
                Log.e(TAG, "Bind failed immediately");
                // Bind failed immediately
                return -1;
            }
            try {
                while (fd == null) {
                    connectSync.wait();
                }
            } catch (InterruptedException e) {
                Log.e(TAG, "Interrupted: " + e.toString());
                return -1;
            }
        }
        return fd.getFd();
    }

    /**
     * Bind to the Monado IPC service - this asynchronously starts connecting (and launching the
     * service if it's not already running)
     *
     * @param context_    Context to use to make the connection. (We get the application context
     *                    from it.)
     * @param packageName The package name containing the Monado runtime. The caller is guaranteed
     *                    to know this because it had to load this class from that package.
     *                    There's a define in xrt_config_android.h to use for this.
     *                    <p>
     *                    Various builds, variants, etc. will have different package names, but we
     *                    must specify the package name explicitly to avoid violating security
     *                    restrictions.
     */
    public boolean bind(Context context_, String packageName) {
        Log.i(TAG, "bind");
        context = context_.getApplicationContext();
        if (context == null) {
            // in case app context returned null
            context = context_;
        }
        try {
            runtimePackageContext = context.createPackageContext(packageName,
                    Context.CONTEXT_IGNORE_SECURITY | Context.CONTEXT_INCLUDE_CODE);
        } catch (PackageManager.NameNotFoundException e) {
            e.printStackTrace();
            Log.e(TAG, "bind: Could not find package " + packageName);
            return false;
        }

        Intent intent = new Intent(BuildConfig.SERVICE_ACTION)
                .setPackage(packageName);

        if (context.startForegroundService(intent) == null) {
            Log.e(TAG, "startForegroundService: Service " + intent.toString() + " does not exist!");
            return false;
        }
        if (!context.bindService(intent,
                this,
                Context.BIND_AUTO_CREATE
                        | Context.BIND_IMPORTANT
                        | Context.BIND_INCLUDE_CAPABILITIES
                        | Context.BIND_ABOVE_CLIENT)) {
            Log.e(TAG, "bindService: Service " + intent.toString() + " could not be found to bind!");
            return false;
        }
        // does not bind right away! This takes some time.
        return true;
    }

    /**
     * Some on-failure cleanup.
     */
    private void handleFailure() {
        failed = true;
        shutdown();
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
        Log.i(TAG, "onServiceConnected");
        monado = IMonado.Stub.asInterface(service);

        try {
            monado.passAppSurface(surfaceHolder.getSurface());
        } catch (RemoteException e) {
            e.printStackTrace();
            Log.e(TAG, "Could not pass app surface: " + e.toString());
        }

        ParcelFileDescriptor theirs;
        ParcelFileDescriptor ours;
        try {
            ParcelFileDescriptor[] fds = ParcelFileDescriptor.createSocketPair();
            ours = fds[0];
            theirs = fds[1];
        } catch (IOException e) {
            e.printStackTrace();
            Log.e(TAG, "could not create socket pair: " + e.toString());
            handleFailure();
            return;
        }

        Thread thread = new Thread(() -> {
            try {
                while(true) {
                    monado.connect(theirs);
                }
            } catch (RemoteException e) {
                e.printStackTrace();
                Log.e(TAG, "could not call IMonado.connect: " + e.toString());
                handleFailure();
            }
        });
        thread.start();

        synchronized (connectSync) {
            Log.e(TAG, String.format("Notifing connectSync with fd %d", ours.getFd()));
            fd = ours;
            connectSync.notify();
        }
    }

    /**
     * Handle asynchronous disconnect.
     *
     * @param name should match the intent above, but not used.
     */
    @Override
    public void onServiceDisconnected(ComponentName name) {
        Log.i(TAG, "onServiceDisconnected");
        shutdown();
        //! @todo tell native that the world is crumbling, then close the fd here.
    }

    /*
     * @todo do we need to watch for a disconnect here?
     *   https://stackoverflow.com/questions/18078914/notify-an-android-service-when-a-bound-client-disconnects
     *
     * Our existing native disconnect handling might be sufficient.
     */
}
