import 'package:flutter/foundation.dart';
import '../models/device.dart';
import '../services/device_service.dart';

class DeviceProvider extends ChangeNotifier {
  final DeviceService _deviceService;

  DeviceProvider(this._deviceService) {
    // Forward notifications from the service
    _deviceService.addListener(_onServiceChanged);
  }

  void _onServiceChanged() => notifyListeners();

  List<Device> get boundDevices => _deviceService.boundDevices;
  List<Device> get scannedDevices => _deviceService.scannedDevices;
  bool get isScanning => _deviceService.isScanning;
  bool get bleAvailable => _deviceService.bleAvailable;

  Future<void> scanDevices() => _deviceService.scanDevices();

  Future<void> bindDevice(Device device) => _deviceService.bindDevice(device);

  Future<void> connectForProvisioning(Device device) =>
      _deviceService.connectForProvisioning(device);

  Future<void> cancelProvisioning() => _deviceService.cancelProvisioning();

  Future<void> unbindDevice(String deviceId) =>
      _deviceService.unbindDevice(deviceId);

  @override
  void dispose() {
    _deviceService.removeListener(_onServiceChanged);
    super.dispose();
  }
}
