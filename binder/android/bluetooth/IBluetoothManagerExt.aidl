/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth;

import android.bluetooth.IBluetoothManager;

/**
 * System private API for talking with new Bluetooth service.
 *
 * {@hide}
 */
interface IBluetoothManagerExt
{
    @JavaPassthrough(annotation="@android.annotation.RequiresNoPermission")
    IBluetoothManager getBluetoothManager();
}
