import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class Device {
  final String id;
  final String name;
  final String macAddress;
  final String type;
  final String firmwareVersion;
  final int batteryLevel;
  final bool isConnected;
  final bool isWifiConnected;
  final DateTime? boundAt;
  final BluetoothDevice? bleDevice; // real BLE reference

  Device({
    required this.id,
    required this.name,
    required this.macAddress,
    this.type = '睡眠监测仪',
    this.firmwareVersion = 'v1.0.0',
    this.batteryLevel = 100,
    this.isConnected = false,
    this.isWifiConnected = false,
    this.boundAt,
    this.bleDevice,
  });

  Device copyWith({
    bool? isConnected,
    bool? isWifiConnected,
    int? batteryLevel,
    DateTime? boundAt,
    BluetoothDevice? bleDevice,
  }) {
    return Device(
      id: id,
      name: name,
      macAddress: macAddress,
      type: type,
      firmwareVersion: firmwareVersion,
      batteryLevel: batteryLevel ?? this.batteryLevel,
      isConnected: isConnected ?? this.isConnected,
      isWifiConnected: isWifiConnected ?? this.isWifiConnected,
      boundAt: boundAt ?? this.boundAt,
      bleDevice: bleDevice ?? this.bleDevice,
    );
  }
}
