import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../models/device.dart';
import 'ble_data_service.dart';

class DeviceService extends ChangeNotifier {
  final BleDataService _bleDataService;
  final List<Device> _boundDevices = [];
  final List<Device> _scannedDevices = [];
  bool _isScanning = false;
  bool _bleAvailable = false;

  DeviceService(this._bleDataService) {
    _initBle();
  }

  List<Device> get boundDevices => List.unmodifiable(_boundDevices);
  List<Device> get scannedDevices => List.unmodifiable(_scannedDevices);
  bool get isScanning => _isScanning;
  bool get bleAvailable => _bleAvailable;

  Future<void> _initBle() async {
    try {
      _bleAvailable = await FlutterBluePlus.isSupported;
    } catch (_) {
      _bleAvailable = false;
    }
  }

  /// Scan for BLE devices. Uses real BLE on supported platforms,
  /// falls back to mock data otherwise.
  Future<void> scanDevices() async {
    _isScanning = true;
    _scannedDevices.clear();
    notifyListeners();

    if (_bleAvailable) {
      await _scanReal();
    } else {
      await _scanMock();
    }

    _isScanning = false;
    notifyListeners();
  }

  /// Real BLE scan using flutter_blue_plus
  Future<void> _scanReal() async {
    try {
      // Check Bluetooth adapter state
      final adapterState = await FlutterBluePlus.adapterState.first;
      if (adapterState != BluetoothAdapterState.on) {
        // BLE is off — fall back to mock
        await _scanMock();
        return;
      }

      // Listen for scan results
      final completer = Completer<void>();
      final subscription = FlutterBluePlus.onScanResults.listen((results) {
        for (final r in results) {
          // Android often leaves platformName empty during scanning. The ESP32
          // advertises its name and NUS UUID in AdvertisementData instead.
          const sleepMonitorName = 'SleepMonitor';
          const uartServiceUuid = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
          final advertisedName = r.advertisementData.advName.trim();
          final deviceName = advertisedName.isNotEmpty
              ? advertisedName
              : r.device.platformName;
          final hasUartService = r.advertisementData.serviceUuids.any(
            (uuid) => uuid.toString().toLowerCase() == uartServiceUuid,
          );
          final isSleepMonitor =
              deviceName == sleepMonitorName || hasUartService;

          if (!isSleepMonitor) continue;

          final macAddress = r.device.remoteId.str;

          // Skip already-bound devices and duplicates
          if (_boundDevices.any((d) => d.macAddress == macAddress)) continue;
          if (_scannedDevices.any((d) => d.macAddress == macAddress)) continue;

          _scannedDevices.add(
            Device(
              id: 'ble_${macAddress.replaceAll(':', '')}',
              name: deviceName.isNotEmpty ? deviceName : sleepMonitorName,
              macAddress: macAddress,
              type: '蓝牙设备',
              bleDevice: r.device,
            ),
          );
          notifyListeners();
        }
      });

      // Start scan for 4 seconds
      await FlutterBluePlus.startScan(timeout: const Duration(seconds: 4));
      await Future.delayed(const Duration(seconds: 5));

      await subscription.cancel();
      if (!completer.isCompleted) completer.complete();
    } catch (e) {
      debugPrint('BLE scan error: $e');
      // Fallback to mock on error
      await _scanMock();
    }
  }

  /// Mock scan fallback (for desktop/emulator)
  Future<void> _scanMock() async {
    await Future.delayed(const Duration(seconds: 2));
    _scannedDevices.addAll([
      Device(
        id: 'dev_002',
        name: 'SleepGuard Mini',
        macAddress: 'AA:BB:CC:DD:EE:02',
        type: '睡眠监测仪',
        firmwareVersion: 'v1.5.0',
        batteryLevel: 92,
      ),
      Device(
        id: 'dev_003',
        name: 'SleepGuard Lite',
        macAddress: 'AA:BB:CC:DD:EE:03',
        type: '睡眠监测仪',
        firmwareVersion: 'v1.2.1',
        batteryLevel: 65,
      ),
      Device(
        id: 'dev_004',
        name: 'BreathSense S1',
        macAddress: 'AA:BB:CC:DD:EE:04',
        type: '呼吸监测仪',
        firmwareVersion: 'v1.0.2',
        batteryLevel: 88,
      ),
    ]);
  }

  /// Bind (connect) a device
  Future<Device> bindDevice(Device device) async {
    if (device.bleDevice != null) {
      try {
        await _bleDataService.connectAndSubscribe(device.bleDevice!);
      } catch (e) {
        debugPrint('BLE connect error: $e');
        rethrow;
      }
    } else {
      await Future.delayed(const Duration(seconds: 1));
    }

    final bound = device.copyWith(isConnected: true, boundAt: DateTime.now());
    _scannedDevices.removeWhere((d) => d.id == device.id);
    _boundDevices.add(bound);
    notifyListeners();
    return bound;
  }

  /// Unbind (disconnect) a device
  Future<void> unbindDevice(String deviceId) async {
    final device = _boundDevices.firstWhere((d) => d.id == deviceId);

    if (device.bleDevice != null) {
      try {
        await _bleDataService.sendCommand('monitor_stop');
        await _bleDataService.disconnect();
      } catch (e) {
        debugPrint('BLE disconnect error: $e');
      }
    }

    _boundDevices.removeWhere((d) => d.id == deviceId);
    notifyListeners();
  }
}
