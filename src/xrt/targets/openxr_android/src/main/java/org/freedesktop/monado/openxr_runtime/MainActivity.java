// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simple main activity for Android.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

package org.freedesktop.monado.openxr_runtime;

import android.os.Bundle;
import android.text.method.LinkMovementMethod;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;
import androidx.fragment.app.FragmentManager;
import androidx.fragment.app.FragmentTransaction;

public class MainActivity extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        // Make our Monado link clickable
        ((TextView) findViewById(R.id.textPowered)).setMovementMethod(LinkMovementMethod.getInstance());

        FragmentManager fragmentManager = getSupportFragmentManager();
        FragmentTransaction fragmentTransaction = fragmentManager.beginTransaction();

        @VrModeStatus.Status
        int status = VrModeStatus.detectStatus(this, BuildConfig.APPLICATION_ID);
        VrModeStatus statusFrag = VrModeStatus.newInstance(status);
        fragmentTransaction.add(R.id.statusFrame, statusFrag, null);
        fragmentTransaction.commit();
    }
}
