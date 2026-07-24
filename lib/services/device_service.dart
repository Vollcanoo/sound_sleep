import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../models/device.dart';
import 'ble_data_service.dart';

class DeviceService extends ChangeNotifier {
  static const _boundDevicesKey = 'bound_devices';

  final BleDataService _bleDataService;
  final List<Device> _boundDevices = [];
  final List<Device> _scannedDevices = [];
  bool _isScanning = false;
  bool _bleAvailable = false;
  String? _connectedMacAddress;
  StreamSubscription<bool>? _connectionSub;

  DeviceService(this._bleDataService) {
    _initBle();
    _loadBoundDevices();
    _connectionSub = _bleDataService.connectionStream.listen(_onBleConnectionChanged);
    _bleDataService.addListener(_onBleDataChanged);
  }

  @override
  void dispose() {
    _connectionSub?.cancel();
    _bleDataService.removeListener(_onBleDataChanged);
    super.dispose();
  }

  void _onBleConnectionChanged(bool connected) {
    bool changed = false;
    for (int i = 0; i < _boundDevices.length; i++) {
      final isActive = _connectedMacAddress == null ||
          _boundDevices[i].macAddress == _connectedMacAddress;
      if (isActive && _boundDevices[i].isConnected != connected) {
        _boundDevices[i] = _boundDevices[i].copyWith(
          isConnected: connected,
          isWifiConnected: connected && _bleDataService.isWifiConnected,
        );
        changed = true;
      }
    }
    if (changed) notifyListeners();
  }

  void _onBleDataChanged() {
    if (!_bleDataService.isConnected) return;
    bool changed = false;
    for (int i = 0; i < _boundDevices.length; i++) {
      final isActive = _connectedMacAddress == null ||
          _boundDevices[i].macAddress == _connectedMacAddress;
      if (isActive &&
          _boundDevices[i].isWifiConnected != _bleDataService.isWifiConnected) {
        _boundDevices[i] = _boundDevices[i].copyWith(
          isConnected: true,
          isWifiConnected: _bleDataService.isWifiConnected,
        );
        changed = true;
      }
    }
    if (changed) notifyListeners();
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

  Future<void> _loadBoundDevices() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_boundDevicesKey);
    if (raw != null) {
      final List<dynamic> list = jsonDecode(raw);
      for (final item in list) {
        final map = item as Map<String, dynamic>;
        _boundDevices.add(
          Device(
            id: map['id'] as String,
            name: map['name'] as String,
            macAddress: map['macAddress'] as String,
            type: map['type'] as String? ?? '睡眠监测仪',
            firmwareVersion: map['firmwareVersion'] as String? ?? 'v1.0.0',
            batteryLevel: (map['batteryLevel'] as int?) ?? 100,
            isConnected: false,
            boundAt: map['boundAt'] != null
                ? DateTime.parse(map['boundAt'] as String)
                : null,
          ),
        );
      }
      notifyListeners();
    }
  }

  Future<void> _persistBoundDevices() async {
    final prefs = await SharedPreferences.getInstance();
    final list = _boundDevices
        .map(
          (d) => {
            'id': d.id,
            'name': d.name,
            'macAddress': d.macAddress,
            'type': d.type,
            'firmwareVersion': d.firmwareVersion,
            'batteryLevel': d.batteryLevel,
            'boundAt': d.boundAt?.toIso8601String(),
          },
        )
        .toList();
    await prefs.setString(_boundDevicesKey, jsonEncode(list));
  }

  Future<void> scanDevices() async {
    _isScanning = true;
    _scannedDevices.clear();
    notifyListeners();

    if (_bleAvailable) {
      await _scanReal();
    }

    _isScanning = false;
    notifyListeners();
  }

  Future<void> _scanReal() async {
    try {
      final adapterState = await FlutterBluePlus.adapterState.first;
      if (adapterState != BluetoothAdapterState.on) {
        return;
      }

      final completer = Completer<void>();
      final subscription = FlutterBluePlus.onScanResults.listen((results) {
        for (final r in results) {
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

      await FlutterBluePlus.startScan(timeout: const Duration(seconds: 4));
      await Future.delayed(const Duration(seconds: 5));

      await subscription.cancel();
      if (!completer.isCompleted) completer.complete();
    } catch (e) {
      debugPrint('BLE scan error: $e');
    }
  }

  Future<Device> bindDevice(Device device) async {
    _connectedMacAddress = device.macAddress;
    if (device.bleDevice != null && !_bleDataService.isConnected) {
      try {
        await _bleDataService.connectAndSubscribe(device.bleDevice!);
      } catch (e) {
        debugPrint('BLE connect error: $e');
        rethrow;
      }
    }

    final bound = device.copyWith(
      isConnected: true,
      isWifiConnected: _bleDataService.isWifiConnected,
      boundAt: DateTime.now(),
    );
    _scannedDevices.removeWhere((d) => d.id == device.id);
    _boundDevices.add(bound);
    notifyListeners();
    await _persistBoundDevices();
    return bound;
  }

  Future<void> connectForProvisioning(Device device) async {
    final bleDevice = device.bleDevice;
    if (bleDevice == null) {
      throw StateError('Selected device has no BLE handle');
    }
    _connectedMacAddress = device.macAddress;
    if (_bleDataService.isConnected) return;

    await _bleDataService.connectAndSubscribe(bleDevice);
  }

  Future<void> unbindDevice(String deviceId) async {
    final device = _boundDevices.firstWhere((d) => d.id == deviceId);

    if (device.bleDevice != null) {
      try {
        await _bleDataService.sendCommand('monitor_stop');
        await _bleDataService.sendCommand('wifi_clear');
        await Future.delayed(const Duration(milliseconds: 500));
        await _bleDataService.disconnect();
      } catch (e) {
        debugPrint('BLE disconnect error: $e');
      }
    }

    _boundDevices.removeWhere((d) => d.id == deviceId);
    notifyListeners();
    await _persistBoundDevices();
  }

  Future<bool> reconnectDevice(String deviceId) async {
    if (!_bleAvailable) return false;

    final idx = _boundDevices.indexWhere((d) => d.id == deviceId);
    if (idx == -1) return false;
    final device = _boundDevices[idx];
    if (device.bleDevice != null && _bleDataService.isConnected) return true;

    try {
      final adapterState = await FlutterBluePlus.adapterState.first;
      if (adapterState != BluetoothAdapterState.on) return false;

      BluetoothDevice? found;
      final subscription = FlutterBluePlus.onScanResults.listen((results) {
        for (final r in results) {
          if (r.device.remoteId.str == device.macAddress) {
            found = r.device;
          }
        }
      });

      await FlutterBluePlus.startScan(timeout: const Duration(seconds: 4));
      await Future.delayed(const Duration(seconds: 5));
      await subscription.cancel();

      if (found == null) return false;

      _connectedMacAddress = device.macAddress;
      await _bleDataService.connectAndSubscribe(found!);
      _boundDevices[idx] = device.copyWith(
        isConnected: true,
        isWifiConnected: _bleDataService.isWifiConnected,
        bleDevice: found,
      );
      notifyListeners();
      return true;
    } catch (e) {
      debugPrint('Reconnect error: $e');
      return false;
    }
  }
}
