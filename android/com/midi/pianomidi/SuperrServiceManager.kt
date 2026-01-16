package com.midi.pianomidi

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.util.UUID

/**
 * Manager for Superr Service BLE Characteristics
 * 
 * Service UUID: 12345678-1234-5678-1234-56789abc0000
 * Characteristics:
 * - Sensitivity (...0001): Byte (0-100), Default 50
 * - Theme (...0002): Byte (0=Aurora, 1=Fire, 2=Matrix)
 * - Transpose (...0003): Signed Byte (-12 to +12)
 */
class SuperrServiceManager private constructor(
    private val context: Context
) {
    companion object {
        private const val TAG = "SuperrServiceManager"
        
        // Service UUID
        val SUPERR_SERVICE_UUID: UUID = UUID.fromString("12345678-1234-5678-1234-56789abc0000")
        
        // Characteristic UUIDs
        val SENSITIVITY_CHAR_UUID: UUID = UUID.fromString("12345678-1234-5678-1234-56789abc0001")
        val THEME_CHAR_UUID: UUID = UUID.fromString("12345678-1234-5678-1234-56789abc0002")
        val TRANSPOSE_CHAR_UUID: UUID = UUID.fromString("12345678-1234-5678-1234-56789abc0003")
        
        @Volatile
        private var INSTANCE: SuperrServiceManager? = null
        
        fun getInstance(context: Context): SuperrServiceManager {
            return INSTANCE ?: synchronized(this) {
                val appCtx = context.applicationContext
                INSTANCE ?: SuperrServiceManager(appCtx).also { INSTANCE = it }
            }
        }
    }
    
    private var bluetoothGatt: BluetoothGatt? = null
    private var connectedDevice: BluetoothDevice? = null
    private val handler = Handler(Looper.getMainLooper())
    
    // Callback to notify UI of updates
    var onConfigUpdated: ((sensitivity: Int, theme: Int, transpose: Int) -> Unit)? = null
    
    // Cache values
    private var currentSensitivity: Int = 50
    private var currentTheme: Int = 0
    private var currentTranspose: Int = 0

    /**
     * Connect to a Bluetooth device's GATT service
     */
    @SuppressLint("MissingPermission")
    fun connectToDevice(device: BluetoothDevice) {
        if (bluetoothGatt != null) {
            if (connectedDevice?.address == device.address) {
                Log.d(TAG, "Already connected to device: ${device.address}")
                bluetoothGatt?.discoverServices()
                return
            } else {
                Log.d(TAG, "Disconnecting from previous device to connect to new one")
                disconnect()
            }
        }
        
        connectedDevice = device
        Log.d(TAG, "Connecting to GATT service on device: ${device.address}")
        
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                bluetoothGatt = device.connectGatt(
                    context,
                    false,
                    gattCallback,
                    BluetoothDevice.TRANSPORT_LE
                )
            } else {
                bluetoothGatt = device.connectGatt(context, false, gattCallback)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error connecting to GATT: ${e.message}", e)
            bluetoothGatt = null
            connectedDevice = null
        }
    }
    
    /**
     * Disconnect from GATT service
     */
    @SuppressLint("MissingPermission")
    fun disconnect() {
        bluetoothGatt?.let { gatt ->
            try {
                gatt.disconnect()
                gatt.close()
            } catch (e: Exception) {
                Log.e(TAG, "Error disconnecting GATT: ${e.message}", e)
            }
        }
        bluetoothGatt = null
        connectedDevice = null
    }
    
    /**
     * Read all config values from device
     */
    @SuppressLint("MissingPermission")
    fun readAllConfig() {
        val gatt = bluetoothGatt ?: return
        val service = gatt.getService(SUPERR_SERVICE_UUID) ?: return
        
        // Queue reads (simple delay generic approach for prototype)
        // Note: In production, use a command queue to ensure sequential execution
        val charSens = service.getCharacteristic(SENSITIVITY_CHAR_UUID)
        val charTheme = service.getCharacteristic(THEME_CHAR_UUID)
        val charTrans = service.getCharacteristic(TRANSPOSE_CHAR_UUID)
        
        if (charSens != null) handler.postDelayed({ gatt.readCharacteristic(charSens) }, 100)
        if (charTheme != null) handler.postDelayed({ gatt.readCharacteristic(charTheme) }, 300)
        if (charTrans != null) handler.postDelayed({ gatt.readCharacteristic(charTrans) }, 500)
    }
    
    /**
     * Set sensitivity (0-100)
     */
    @SuppressLint("MissingPermission")
    fun setSensitivity(value: Int) {
        val clampedValue = value.coerceIn(0, 100)
        writeCharacteristic(SENSITIVITY_CHAR_UUID, byteArrayOf(clampedValue.toByte()))
        Log.d(TAG, "Set sensitivity: $clampedValue")
        currentSensitivity = clampedValue
    }
    
    /**
     * Set theme (0=Aurora, 1=Fire, 2=Matrix)
     */
    @SuppressLint("MissingPermission")
    fun setTheme(theme: Int) {
        val clampedTheme = theme.coerceIn(0, 2)
        writeCharacteristic(THEME_CHAR_UUID, byteArrayOf(clampedTheme.toByte()))
        Log.d(TAG, "Set theme: $clampedTheme")
        currentTheme = clampedTheme
    }
    
    /**
     * Set transpose (-12 to +12)
     */
    @SuppressLint("MissingPermission")
    fun setTranspose(value: Int) {
        val clampedValue = value.coerceIn(-12, 12)
        // Convert to signed byte representation
        writeCharacteristic(TRANSPOSE_CHAR_UUID, byteArrayOf(clampedValue.toByte()))
        Log.d(TAG, "Set transpose: $clampedValue")
        currentTranspose = clampedValue
    }
    
    /**
     * Write to a characteristic
     */
    @SuppressLint("MissingPermission")
    private fun writeCharacteristic(characteristicUuid: UUID, value: ByteArray) {
        val gatt = bluetoothGatt ?: run {
            Log.w(TAG, "GATT not connected, cannot write characteristic")
            return
        }
        
        try {
            val service = gatt.getService(SUPERR_SERVICE_UUID) ?: return
            val characteristic = service.getCharacteristic(characteristicUuid) ?: return
            
            // Set value
            // Using legacy setValue for compatibility with the snippet provided style, 
            // but standard writeCharacteristic call
            characteristic.value = value
            
            // Write
            val result = gatt.writeCharacteristic(characteristic)
            Log.d(TAG, "Write ${characteristicUuid} result: $result")
            
        } catch (e: Exception) {
            Log.e(TAG, "Error writing characteristic: ${e.message}", e)
        }
    }
    
    /**
     * GATT Callback
     */
    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    Log.d(TAG, "GATT connected, discovering services...")
                    gatt.discoverServices()
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    Log.d(TAG, "GATT disconnected")
                    bluetoothGatt = null
                    connectedDevice = null
                }
            }
        }
        
        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val service = gatt.getService(SUPERR_SERVICE_UUID)
                if (service != null) {
                    Log.d(TAG, "Superr Service discovered. Reading current config...")
                    // Change: Instead of overwriting with default (50), READ current values
                    readAllConfig()
                }
            }
        }
        
        @Deprecated("Deprecated in Java")
        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val value = characteristic.value
                if (value != null && value.isNotEmpty()) {
                    val byteVal = value[0]
                    
                    when (characteristic.uuid) {
                        SENSITIVITY_CHAR_UUID -> currentSensitivity = byteVal.toInt() and 0xFF
                        THEME_CHAR_UUID -> currentTheme = byteVal.toInt() and 0xFF
                        TRANSPOSE_CHAR_UUID -> currentTranspose = byteVal.toInt()
                    }
                    
                    Log.d(TAG, "Read Config: Sens=$currentSensitivity, Theme=$currentTheme, Trans=$currentTranspose")
                    
                    // Notify UI (Main Thread)
                    handler.post {
                        onConfigUpdated?.invoke(currentSensitivity, currentTheme, currentTranspose)
                    }
                }
            }
        }
    }
}
