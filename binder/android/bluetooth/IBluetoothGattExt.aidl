/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth;

import android.bluetooth.IBluetoothGatt;

/**
 * API for interacting with new BLE / GATT
 * @hide
 */
interface IBluetoothGattExt
{
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)")
    IBluetoothGatt getBluetoothGatt();
}
