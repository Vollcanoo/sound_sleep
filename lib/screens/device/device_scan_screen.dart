import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../providers/device_provider.dart';
import '../../theme/app_theme.dart';
import '../../widgets/device_tile.dart';

class DeviceScanScreen extends StatefulWidget {
  const DeviceScanScreen({super.key});

  @override
  State<DeviceScanScreen> createState() => _DeviceScanScreenState();
}

class _DeviceScanScreenState extends State<DeviceScanScreen>
    with SingleTickerProviderStateMixin {
  late AnimationController _pulseController;

  @override
  void initState() {
    super.initState();
    _pulseController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1500),
    )..repeat();

    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) {
        context.read<DeviceProvider>().scanDevices();
      }
    });
  }

  @override
  void dispose() {
    _pulseController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final deviceProvider = context.watch<DeviceProvider>();

    return Scaffold(
      appBar: AppBar(
        title: const Text('搜索设备'),
        actions: [
          if (!deviceProvider.isScanning)
            IconButton(
              icon: const Icon(Icons.refresh),
              onPressed: () => deviceProvider.scanDevices(),
            ),
        ],
      ),
      body: Column(
        children: [
          // BLE status banner
          Container(
            width: double.infinity,
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
            color: deviceProvider.bleAvailable
                ? Colors.green.withValues(alpha: 0.08)
                : Colors.orange.withValues(alpha: 0.08),
            child: Row(
              children: [
                Icon(
                  deviceProvider.bleAvailable
                      ? Icons.bluetooth
                      : Icons.bluetooth_disabled,
                  size: 16,
                  color: deviceProvider.bleAvailable
                      ? Colors.green
                      : Colors.orange,
                ),
                const SizedBox(width: 8),
                Text(
                  deviceProvider.bleAvailable
                      ? '蓝牙已就绪，正在扫描真实设备'
                      : '蓝牙不可用，使用模拟设备演示',
                  style: TextStyle(
                    fontSize: 13,
                    color: deviceProvider.bleAvailable
                        ? Colors.green.shade700
                        : Colors.orange.shade700,
                  ),
                ),
              ],
            ),
          ),

          // Scanning animation
          if (deviceProvider.isScanning)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 32),
              child: Column(
                children: [
                  SizedBox(
                    width: 100,
                    height: 100,
                    child: AnimatedBuilder(

                      animation: _pulseController,
                      builder: (context, child) {
                        return Stack(
                          alignment: Alignment.center,
                          children: [
                            // Pulse rings
                            for (int i = 0; i < 3; i++)
                              Transform.scale(
                                scale: 0.5 +
                                    ((_pulseController.value + i * 0.33) %
                                            1.0) *
                                        0.8,
                                child: Container(
                                  width: 100,
                                  height: 100,
                                  decoration: BoxDecoration(
                                    shape: BoxShape.circle,
                                    border: Border.all(
                                      color: AppTheme.primaryBlue.withValues(
                                        alpha: (1.0 -
                                                ((_pulseController.value +
                                                        i * 0.33) %
                                                    1.0)) *
                                            0.4,
                                      ),
                                      width: 2,
                                    ),
                                  ),
                                ),
                              ),
                            // Center icon
                            Container(
                              width: 48,
                              height: 48,
                              decoration: BoxDecoration(
                                color: AppTheme.primaryBlue,
                                shape: BoxShape.circle,
                              ),
                              child: const Icon(
                                Icons.bluetooth_searching,
                                color: Colors.white,
                                size: 24,
                              ),
                            ),
                          ],
                        );
                      },
                    ),
                  ),
                  const SizedBox(height: 16),
                  Text(
                    '正在搜索附近的蓝牙设备...',
                    style: TextStyle(
                      fontSize: 15,
                      color: Colors.grey.shade600,
                    ),
                  ),
                ],
              ),
            ),

          // No results
          if (!deviceProvider.isScanning &&
              deviceProvider.scannedDevices.isEmpty)
            Expanded(
              child: Center(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Icon(Icons.bluetooth_disabled,
                        size: 64, color: Colors.grey.shade300),
                    const SizedBox(height: 16),
                    Text(
                      '未发现可用设备',
                      style: TextStyle(
                          fontSize: 16, color: Colors.grey.shade500),
                    ),
                    const SizedBox(height: 8),
                    Text(
                      '请确保设备已开启并在附近',
                      style: TextStyle(
                          fontSize: 13, color: Colors.grey.shade400),
                    ),
                    const SizedBox(height: 24),
                    ElevatedButton.icon(
                      onPressed: () => deviceProvider.scanDevices(),
                      icon: const Icon(Icons.refresh),
                      label: const Text('重新搜索'),
                    ),
                  ],
                ),
              ),
            ),

          // Results list
          if (!deviceProvider.isScanning &&
              deviceProvider.scannedDevices.isNotEmpty)
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Padding(
                    padding: const EdgeInsets.fromLTRB(20, 16, 20, 8),
                    child: Text(
                      '发现 ${deviceProvider.scannedDevices.length} 个设备',
                      style: const TextStyle(
                        fontSize: 15,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                  ),
                  Expanded(
                    child: ListView.builder(
                      itemCount: deviceProvider.scannedDevices.length,
                      itemBuilder: (context, index) {
                        final device =
                            deviceProvider.scannedDevices[index];
                        return DeviceTile(
                          device: device,
                          isBound: false,
                          onTap: () async {
                            await deviceProvider.bindDevice(device);
                            if (context.mounted) {
                              ScaffoldMessenger.of(context).showSnackBar(
                                SnackBar(
                                  content:
                                      Text('${device.name} 绑定成功'),
                                  backgroundColor: Colors.green,
                                ),
                              );
                            }
                          },
                        );
                      },
                    ),
                  ),
                ],
              ),
            ),
        ],
      ),
    );
  }
}
