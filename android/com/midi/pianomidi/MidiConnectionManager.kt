package com.midi.pianomidi

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.media.midi.MidiDevice
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiManager
import android.media.midi.MidiReceiver
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.util.Log
import androidx.core.content.ContextCompat
import java.util.UUID

/**
 * Data class representing a MIDI device (either system MIDI or raw BLE device)
 */
data class MidiDeviceWrapper(
    val name: String,
    val type: DeviceType,
    val deviceInfo: MidiDeviceInfo? = null, // Null for an unconnected BLE device
    val bluetoothDevice: BluetoothDevice? = null, // Null for USB/Virtual devices
    val id: String // Unique ID for list tracking
)

enum class DeviceType { 
    USB, 
    BLUETOOTH, 
    VIRTUAL, 
    UNKNOWN 
}

/**
 * Data class representing a MIDI note event
 */
data class MidiNoteEvent(
    val note: Int,
    val velocity: Int,
    val isNoteOn: Boolean,
    val channel: Int,
    val timestamp: Long = System.currentTimeMillis()
)

/**
 * Callback interface for MIDI device discovery events
 */
interface MidiDeviceDiscoveryCallback {
    fun onDeviceFound(device: MidiDeviceWrapper)
    fun onDeviceRemoved(device: MidiDeviceWrapper)
    fun onScanComplete(devices: List<MidiDeviceWrapper>)
    fun onScanError(error: String)
}

/**
 * Callback interface for MIDI input events
 */
interface MidiInputCallback {
    fun onNoteOn(note: Int, velocity: Int, channel: Int)
    fun onNoteOff(note: Int, velocity: Int, channel: Int)
    fun onMidiMessage(message: ByteArray, timestamp: Long)
}

/**
 * Callback interface for connection state changes
 */
interface MidiConnectionCallback {
    fun onConnected(device: MidiDeviceWrapper)
    fun onDisconnected(device: MidiDeviceWrapper)
    fun onConnectionError(error: String)
    fun onReconnecting(device: MidiDeviceWrapper)
    fun onReconnected(device: MidiDeviceWrapper)
    fun onConfigRead(sensitivity: Int, theme: Int, transpose: Int)
}

/**
 * Manager class for handling MIDI device connections, scanning, and input events.
 * Supports USB, Bluetooth MIDI (paired), and BLE MIDI (unpaired) devices.
 * Also handles custom Superr Configuration Service.
 */
class MidiConnectionManager private constructor(
    private val context: Context,
    private val midiManager: MidiManager
) {
    companion object {
        private const val TAG = "MidiConnectionManager"
        
        // Standard MIDI Service UUID (BLE MIDI)
        private val MIDI_SERVICE_UUID = UUID.fromString("03B80E5A-EDE8-4B33-A751-6CE34EC4C700")
        
        // Custom Configuration Service UUID
        val CONFIG_SERVICE_UUID: UUID = UUID.fromString("12345678-1234-5678-1234-56789abc0000")
        val CHAR_SENSITIVITY_UUID: UUID = UUID.fromString("12345678-1234-5678-1234-56789abc0001")
        val CHAR_THEME_UUID: UUID = UUID.fromString("12345678-1234-5678-1234-56789abc0002")
        val CHAR_TRANSPOSE_UUID: UUID = UUID.fromString("12345678-1234-5678-1234-56789abc0003")
        
        // Protoype Device Name
        private const val SUPERR_DEVICE_NAME = "Superr_MIDI"
        
        @Volatile private var INSTANCE: MidiConnectionManager? = null
        
        fun getInstance(context: Context): MidiConnectionManager {
            return INSTANCE ?: synchronized(this) {
                val appCtx = context.applicationContext
                val midiManager = appCtx.getSystemService(Context.MIDI_SERVICE) as MidiManager
                INSTANCE ?: MidiConnectionManager(appCtx, midiManager).also { INSTANCE = it }
            }
        }
    }
    
    private val handler = Handler(Looper.getMainLooper())
    private val discoveredDevices = mutableListOf<MidiDeviceWrapper>()
    private var isScanning = false
    
    // Callbacks
    private var discoveryCallback: MidiDeviceDiscoveryCallback? = null
    private var connectionCallback: MidiConnectionCallback? = null
    private var inputCallback: MidiInputCallback? = null
    
    // BLE Components
    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val bluetoothAdapter = bluetoothManager.adapter
    private val bleScanner = bluetoothAdapter?.bluetoothLeScanner
    
    // Active Connection
    private var activeDevice: MidiDevice? = null
    private var activeWrapper: MidiDeviceWrapper? = null
    private val outputPorts = mutableListOf<android.media.midi.MidiOutputPort>()
    
    // GATT Connection for Configuration
    private var bluetoothGatt: BluetoothGatt? = null
    private var configService: BluetoothGattService? = null
    private var charSensitivity: BluetoothGattCharacteristic? = null
    private var charTheme: BluetoothGattCharacteristic? = null
    private var charTranspose: BluetoothGattCharacteristic? = null

    // System MIDI Device Callback (USB / Paired BLE)
    private val deviceCallback = object : MidiManager.DeviceCallback() {
        override fun onDeviceAdded(info: MidiDeviceInfo) {
            handler.post {
                handleSystemDeviceFound(info)
            }
        }
        
        override fun onDeviceRemoved(info: MidiDeviceInfo) {
            handler.post {
                handleSystemDeviceRemoved(info)
            }
        }
    }
    
    // BLE Scan Callback (Unpaired BLE Devices)
    private val bleScanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device
            val deviceName = device.name ?: "Unknown MIDI"
            val deviceAddress = device.address
            
            // Avoid duplicates
            if (discoveredDevices.any { it.id == deviceAddress }) return
            
            val wrapper = MidiDeviceWrapper(
                name = deviceName,
                type = DeviceType.BLUETOOTH,
                bluetoothDevice = device,
                deviceInfo = null, // No MidiDeviceInfo yet!
                id = deviceAddress
            )
            
            Log.d(TAG, "BLE Device Found: $deviceName ($deviceAddress)")
            discoveredDevices.add(wrapper)
            handler.post { 
                discoveryCallback?.onDeviceFound(wrapper) 
            }
        }
        
        override fun onScanFailed(errorCode: Int) {
            Log.e(TAG, "BLE Scan Failed: $errorCode")
            handler.post { 
                discoveryCallback?.onScanError("BLE Scan Failed: $errorCode") 
            }
        }
    }
    
    // GATT Callback for Configuration Service
    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.d(TAG, "GATT Connected. Discovering services...")
                gatt.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.d(TAG, "GATT Disconnected")
                bluetoothGatt = null
                configService = null
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                configService = gatt.getService(CONFIG_SERVICE_UUID)
                if (configService != null) {
                    Log.d(TAG, "Config Service Found!")
                    charSensitivity = configService?.getCharacteristic(CHAR_SENSITIVITY_UUID)
                    charTheme = configService?.getCharacteristic(CHAR_THEME_UUID)
                    charTranspose = configService?.getCharacteristic(CHAR_TRANSPOSE_UUID)
                    
                    // Read initial values
                    readFullConfig()
                } else {
                    Log.w(TAG, "Config Service NOT found on this device.")
                }
            }
        }

        override fun onCharacteristicRead(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
             if (status == BluetoothGatt.GATT_SUCCESS) {
                 val value = characteristic.value
                 if (value != null && value.isNotEmpty()) {
                     Log.d(TAG, "Read Characteristic ${characteristic.uuid}: ${value[0]}")
                     // You could broadcast this update if needed
                 }
             }
        }

        override fun onCharacteristicWrite(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.d(TAG, "Write Successful: ${characteristic.uuid}")
            } else {
                Log.e(TAG, "Write Failed: $status")
            }
        }
    }
    
    init {
        midiManager.registerDeviceCallback(deviceCallback, handler)
    }
    
    fun setDiscoveryCallback(cb: MidiDeviceDiscoveryCallback) { 
        discoveryCallback = cb 
    }
    
    fun setConnectionCallback(cb: MidiConnectionCallback) { 
        connectionCallback = cb 
    }
    
    fun setInputCallback(cb: MidiInputCallback) { 
        inputCallback = cb 
    }
    
    /**
     * Scan for available MIDI devices (USB, paired Bluetooth, and unpaired BLE)
     */
    @SuppressLint("MissingPermission")
    fun scanForDevices() {
        if (isScanning) {
            Log.w(TAG, "Scan already in progress")
            return
        }
        
        isScanning = true
        discoveredDevices.clear()
        Log.d(TAG, "Starting Scan (System + BLE)...")
        
        // 1. Scan System Devices (USB / Already Paired)
        val systemDevices = midiManager.devices
        Log.d(TAG, "Found ${systemDevices.size} system MIDI device(s)")
        systemDevices.forEach { handleSystemDeviceFound(it) }
        
        // 2. Start BLE Scan for new devices
        if (bluetoothAdapter != null && bluetoothAdapter.isEnabled) {
            if (!hasPermissions()) {
                Log.e(TAG, "Missing Bluetooth Permissions!")
                handler.post {
                    discoveryCallback?.onScanError("Missing Bluetooth Permissions")
                }
                isScanning = false
                return
            }
            
            try {
                // Filter for MIDI Service OR our custom Device Name
                val filters = listOf(
                    ScanFilter.Builder()
                        .setServiceUuid(ParcelUuid(MIDI_SERVICE_UUID))
                        .build(),
                    ScanFilter.Builder()
                        .setDeviceName(SUPERR_DEVICE_NAME)
                        .build()
                )
                val settings = ScanSettings.Builder()
                    .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                    .build()
                
                bleScanner?.startScan(filters, settings, bleScanCallback)
                Log.d(TAG, "BLE scan started")
                
                // Stop scanning after 10 seconds
                handler.postDelayed({ stopScan() }, 10000)
            } catch (e: Exception) {
                Log.e(TAG, "Error starting BLE scan: ${e.message}", e)
                handler.post {
                    discoveryCallback?.onScanError("BLE scan error: ${e.message}")
                }
                stopScan()
            }
        } else {
            Log.w(TAG, "Bluetooth not enabled")
            handler.postDelayed({ stopScan() }, 1000) // Finish quickly if no BLE
        }
    }
    
    @SuppressLint("MissingPermission")
    fun stopScan() {
        if (!isScanning) return
        isScanning = false
        
        if (bluetoothAdapter?.isEnabled == true && hasPermissions()) {
            try {
                bleScanner?.stopScan(bleScanCallback)
                Log.d(TAG, "BLE scan stopped")
            } catch (e: Exception) {
                Log.e(TAG, "Error stopping BLE scan: ${e.message}", e)
            }
        }
        
        Log.d(TAG, "Scan Complete. Found ${discoveredDevices.size} device(s).")
        handler.post {
            discoveryCallback?.onScanComplete(discoveredDevices.toList())
        }
    }
    
    /**
     * Connect to a MIDI device
     */
    @SuppressLint("MissingPermission")
    fun connectToDevice(wrapper: MidiDeviceWrapper) {
        stopScan() // Stop scanning when connecting
        
        Log.d(TAG, "Connecting to ${wrapper.name} (Type: ${wrapper.type})...")
        
        // Disconnect from current device if connected
        disconnect()
        
        // CASE 1: Raw BLE Device (Found via Scan)
        if (wrapper.bluetoothDevice != null && wrapper.deviceInfo == null) {
            Log.d(TAG, "Opening Raw Bluetooth Device via openBluetoothDevice()...")
            
            if (!hasPermissions()) {
                val errorMsg = "Missing Bluetooth permissions to connect"
                Log.e(TAG, errorMsg)
                handler.post {
                    connectionCallback?.onConnectionError(errorMsg)
                }
                return
            }
            
            try {
                // Open MIDI Device
                midiManager.openBluetoothDevice(
                    wrapper.bluetoothDevice,
                    { device -> 
                        handler.post {
                            handleDeviceOpened(device, wrapper)
                        }
                    },
                    handler
                )
                
                // ALSO Connect GATT for Config Service
                Log.d(TAG, "Connecting GATT for Config Service...")
                wrapper.bluetoothDevice.connectGatt(context, false, gattCallback)
                
            } catch (e: Exception) {
                val errorMsg = "Error opening Bluetooth device: ${e.message}"
                Log.e(TAG, errorMsg, e)
                handler.post {
                    connectionCallback?.onConnectionError(errorMsg)
                }
            }
        }
        // CASE 2: System Device (USB or Paired)
        else if (wrapper.deviceInfo != null) {
            Log.d(TAG, "Opening System Device via openDevice()...")
            
            try {
                midiManager.openDevice(
                    wrapper.deviceInfo,
                    { device -> 
                        handler.post {
                            handleDeviceOpened(device, wrapper)
                        }
                    },
                    handler
                )
            } catch (e: Exception) {
                val errorMsg = "Error opening system device: ${e.message}"
                Log.e(TAG, errorMsg, e)
                handler.post {
                    connectionCallback?.onConnectionError(errorMsg)
                }
            }
        } else {
            val errorMsg = "Invalid device wrapper: no deviceInfo or bluetoothDevice"
            Log.e(TAG, errorMsg)
            handler.post {
                connectionCallback?.onConnectionError(errorMsg)
            }
        }
    }
    
    /**
     * Handle device opened successfully
     */
    private fun handleDeviceOpened(device: MidiDevice?, wrapper: MidiDeviceWrapper) {
        if (device == null) {
            Log.e(TAG, "Failed to open device!")
            connectionCallback?.onConnectionError("Failed to open device (returned null)")
            return
        }
        
        Log.d(TAG, "MidiDevice opened: ${device.info}")
        activeDevice = device
        activeWrapper = wrapper
        
        // Open Output Ports (This receives data FROM the device)
        val info = device.info
        val portCount = info.outputPortCount
        
        if (portCount > 0) {
           Log.d(TAG, "Opening ${portCount} Output Ports (to receive MIDI)")
           for (i in 0 until portCount) {
               val outputPort = device.openOutputPort(i)
               if (outputPort != null) {
                   outputPort.connect(object : MidiReceiver() {
                       override fun onSend(msg: ByteArray?, offset: Int, count: Int, timestamp: Long) {
                           if (msg == null) return
                           // Forward to callback
                           if (count > 0) {
                               inputCallback?.onMidiMessage(msg.sliceArray(offset until offset + count), timestamp)
                               
                               // Parse Note On/Off for convenience
                               // Simple parser for standard MIDI channel voice messages
                               val status = msg[offset].toInt() and 0xFF
                               val cmd = status and 0xF0
                               val channel = status and 0x0F
                               
                               if (count >= 3) {
                                   val note = msg[offset + 1].toInt() and 0x7F
                                   val velocity = msg[offset + 2].toInt() and 0x7F
                                   
                                   if (cmd == 0x90 && velocity > 0) {
                                       inputCallback?.onNoteOn(note, velocity, channel)
                                   } else if (cmd == 0x80 || (cmd == 0x90 && velocity == 0)) {
                                       inputCallback?.onNoteOff(note, velocity, channel)
                                   }
                               }
                           }
                       }
                   })
                   outputPorts.add(outputPort)
                   Log.d(TAG, "Port $i opened successfully")
               } else {
                   Log.w(TAG, "Failed to open Output Port $i")
               }
           }
        }
        
        connectionCallback?.onConnected(wrapper)
        Log.d(TAG, "Device Connected Successfully!")
    }
    
    fun disconnect() {
        Log.d(TAG, "Disconnecting...")
        
        // Close MIDI Ports
        outputPorts.forEach { 
            try { it.close() } catch(e: Exception) { /* ignore */ } 
        }
        outputPorts.clear()
        
        // Close MIDI Device
        activeDevice?.close()
        activeDevice = null
        activeWrapper = null
        
        // Close GATT
        bluetoothGatt?.disconnect()
        bluetoothGatt?.close()
        bluetoothGatt = null
        configService = null
        
        connectionCallback?.onDisconnected(activeWrapper ?: MidiDeviceWrapper("Unknown", DeviceType.UNKNOWN, id=""))
    }
    
    // ==========================================
    // CUSTOM CONFIGURATION METHODS
    // ==========================================
    
    @SuppressLint("MissingPermission")
    fun readFullConfig() {
        if (bluetoothGatt == null) return
        
        // Helper to queue reads (since Android reads are sequential)
        // For simplicity, we just trigger them with delays or rely on a proper queue manager.
        // Here we just print a log that we intend to read. To implement properly, use a command queue.
        Log.d(TAG, "Requesting Config Read...")
        
        // Note: Real implementation needs a queue. readCharacteristic is async/exclusive.
        // As a quick hack for the prototype:
        handler.postDelayed({ bluetoothGatt?.readCharacteristic(charSensitivity) }, 100)
        handler.postDelayed({ bluetoothGatt?.readCharacteristic(charTheme) }, 400)
        handler.postDelayed({ bluetoothGatt?.readCharacteristic(charTranspose) }, 700)
    }

    @SuppressLint("MissingPermission")
    fun setSensitivity(value: Int) { // 0-100
        val char = charSensitivity ?: return
        val clamped = value.coerceIn(0, 100)
        char.value = byteArrayOf(clamped.toByte())
        bluetoothGatt?.writeCharacteristic(char)
        Log.d(TAG, "Writing Sensitivity: $clamped")
    }
    
    @SuppressLint("MissingPermission")
    fun setTheme(value: Int) { // 0, 1, 2
        val char = charTheme ?: return
        val clamped = value.coerceIn(0, 2)
        char.value = byteArrayOf(clamped.toByte())
        bluetoothGatt?.writeCharacteristic(char)
        Log.d(TAG, "Writing Theme: $clamped")
    }
    
    @SuppressLint("MissingPermission")
    fun setTranspose(value: Int) { // -12 to 12
        val char = charTranspose ?: return
        val clamped = value.coerceIn(-12, 12)
        char.value = byteArrayOf(clamped.toByte())
        bluetoothGatt?.writeCharacteristic(char)
        Log.d(TAG, "Writing Transpose: $clamped")
    }
    
    // ==========================================
    // INTERNAL HELPERS
    // ==========================================
    
    private fun handleSystemDeviceFound(info: MidiDeviceInfo) {
        val bundle = info.properties
        val name = bundle.getString(MidiDeviceInfo.PROPERTY_NAME) ?: "USB/System MIDI"
        
        // Skip private or already discovered devices
        if (discoveredDevices.any { it.deviceInfo?.id == info.id }) return
        
        val type = if (name.contains("Superr") || info.type == MidiDeviceInfo.TYPE_BLUETOOTH) 
            DeviceType.BLUETOOTH else DeviceType.USB
            
        val wrapper = MidiDeviceWrapper(
            name = name,
            type = type,
            deviceInfo = info,
            id = "sys:${info.id}" // Prefixed ID for system devices
        )
        
        discoveredDevices.add(wrapper)
        Log.d(TAG, "System MIDI Device Found: $name")
        discoveryCallback?.onDeviceFound(wrapper)
    }
    
    private fun handleSystemDeviceRemoved(info: MidiDeviceInfo) {
         val index = discoveredDevices.indexOfFirst { it.deviceInfo?.id == info.id }
         if (index != -1) {
             val removed = discoveredDevices.removeAt(index)
             discoveryCallback?.onDeviceRemoved(removed)
         }
    }
    
    private fun hasPermissions(): Boolean {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED &&
                   ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
        } else {
            return ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        }
    }
}
