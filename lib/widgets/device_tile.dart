import 'package:flutter/material.dart';
import '../models/device.dart';
import '../theme/app_theme.dart';

class DeviceTile extends StatelessWidget {
  final Device device;
  final bool isBound;
  final VoidCallback? onTap;

  const DeviceTile({
    super.key,
    required this.device,
    this.isBound = true,
    this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 6),
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(16),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Row(
            children: [
              // Device icon
              Container(
                width: 48,
                height: 48,
                decoration: BoxDecoration(
                  color: (device.isConnected
                          ? AppTheme.primaryBlue
                          : Colors.grey.shade400)
                      .withValues(alpha: 0.1),
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Icon(
                  device.type == '呼吸监测仪' ? Icons.air : Icons.sensors,
                  color: device.isConnected
                      ? AppTheme.primaryBlue
                      : Colors.grey.shade400,
                ),
              ),
              const SizedBox(width: 16),
              // Device info
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      device.name,
                      style: const TextStyle(
                        fontSize: 16,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      isBound
                          ? '${device.type} · ${device.firmwareVersion}'
                          : device.macAddress,
                      style: TextStyle(
                        fontSize: 13,
                        color: Colors.grey.shade600,
                      ),
                    ),
                  ],
                ),
              ),
              // Status / action
              if (isBound) ...[
                // Battery
                Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Icon(
                      _batteryIcon(device.batteryLevel),
                      size: 20,
                      color: device.batteryLevel > 20
                          ? Colors.green
                          : Colors.red,
                    ),
                    const SizedBox(width: 4),
                    Text(
                      '${device.batteryLevel}%',
                      style: TextStyle(
                        fontSize: 13,
                        color: Colors.grey.shade600,
                      ),
                    ),
                  ],
                ),
                const SizedBox(width: 12),
                // Connection status dot
                Container(
                  width: 10,
                  height: 10,
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    color: device.isConnected ? Colors.green : Colors.grey.shade400,
                  ),
                ),
              ] else ...[
                // Bind button
                OutlinedButton(
                  onPressed: onTap,
                  style: OutlinedButton.styleFrom(
                    padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                    minimumSize: Size.zero,
                  ),
                  child: const Text('绑定', style: TextStyle(fontSize: 13)),
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }

  IconData _batteryIcon(int level) {
    if (level > 80) return Icons.battery_full;
    if (level > 60) return Icons.battery_5_bar;
    if (level > 40) return Icons.battery_4_bar;
    if (level > 20) return Icons.battery_2_bar;
    return Icons.battery_alert;
  }
}
