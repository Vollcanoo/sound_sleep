import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../services/ble_data_service.dart';
import '../../services/wifi_provision_service.dart';
import '../../theme/app_theme.dart';

/// WiFi 配置界面
///
/// 通过已有的 BLE 连接向 ESP32 发送 WiFi 凭据完成配网。
class WifiConfigScreen extends StatefulWidget {
  const WifiConfigScreen({super.key});

  @override
  State<WifiConfigScreen> createState() => _WifiConfigScreenState();
}

class _WifiConfigScreenState extends State<WifiConfigScreen> {
  final _ssidController = TextEditingController();
  final _passwordController = TextEditingController();
  final _formKey = GlobalKey<FormState>();
  late final WifiProvisionService _provisionService;

  bool _obscurePassword = true;

  @override
  void initState() {
    super.initState();
    final bleDataService = context.read<BleDataService>();
    _provisionService = WifiProvisionService(bleDataService: bleDataService);
    _provisionService.addListener(_onStateChanged);
  }

  @override
  void dispose() {
    _provisionService.removeListener(_onStateChanged);
    _provisionService.dispose();
    _ssidController.dispose();
    _passwordController.dispose();
    super.dispose();
  }

  void _onStateChanged() {
    if (mounted) setState(() {});
  }

  Future<void> _sendCredentials() async {
    if (!_formKey.currentState!.validate()) return;

    final ssid = _ssidController.text.trim();
    final password = _passwordController.text;

    final success = await _provisionService.sendWifiCredentials(ssid, password);

    if (mounted && success) {
      await Future.delayed(const Duration(seconds: 2));
      if (mounted) {
        Navigator.of(context).pop(true);
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('WiFi 配置'),
        leading: IconButton(
          icon: const Icon(Icons.arrow_back),
          onPressed: () {
            if (mounted) Navigator.of(context).pop(false);
          },
        ),
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            _buildDeviceCard(),
            const SizedBox(height: 24),
            if (_provisionService.state != WifiProvisionState.idle)
              _buildStatusCard(),
            if (_provisionService.state != WifiProvisionState.idle)
              const SizedBox(height: 24),
            if (_provisionService.state != WifiProvisionState.success)
              _buildWifiForm(),
            if (_provisionService.state == WifiProvisionState.success)
              _buildSuccessCard(),
          ],
        ),
      ),
    );
  }

  // PLACEHOLDER_WIDGETS

  Widget _buildDeviceCard() {
    final bleService = context.read<BleDataService>();
    return Card(
      elevation: 0,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(12),
        side: BorderSide(color: Colors.grey.shade200),
      ),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Row(
          children: [
            Container(
              width: 48,
              height: 48,
              decoration: BoxDecoration(
                color: AppTheme.primaryBlue.withValues(alpha: 0.1),
                borderRadius: BorderRadius.circular(12),
              ),
              child: Icon(
                Icons.bluetooth_connected,
                color: AppTheme.primaryBlue,
                size: 24,
              ),
            ),
            const SizedBox(width: 16),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text(
                    'SleepMonitor',
                    style: TextStyle(
                      fontSize: 16,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                  const SizedBox(height: 4),
                  Text(
                    bleService.isConnected ? '已连接' : '未连接',
                    style: TextStyle(
                      fontSize: 13,
                      color: bleService.isConnected ? Colors.green : Colors.red,
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildStatusCard() {
    final state = _provisionService.state;
    final isError = state == WifiProvisionState.failed;
    final isProgress = state == WifiProvisionState.sending ||
        state == WifiProvisionState.waiting;

    return Card(
      elevation: 0,
      color: isError
          ? Colors.red.withValues(alpha: 0.05)
          : Colors.blue.withValues(alpha: 0.05),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(12),
        side: BorderSide(
          color: isError
              ? Colors.red.withValues(alpha: 0.2)
              : Colors.blue.withValues(alpha: 0.2),
        ),
      ),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Row(
          children: [
            if (isProgress)
              const SizedBox(
                width: 20,
                height: 20,
                child: CircularProgressIndicator(strokeWidth: 2),
              ),
            if (isError)
              const Icon(Icons.error_outline, color: Colors.red, size: 20),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    _provisionService.statusMessage,
                    style: TextStyle(
                      fontSize: 14,
                      fontWeight: FontWeight.w500,
                      color: isError ? Colors.red.shade700 : Colors.blue.shade700,
                    ),
                  ),
                  if (_provisionService.errorDetail.isNotEmpty) ...[
                    const SizedBox(height: 4),
                    Text(
                      _provisionService.errorDetail,
                      style: TextStyle(
                        fontSize: 12,
                        color: Colors.red.shade400,
                      ),
                    ),
                  ],
                ],
              ),
            ),
            if (isError)
              TextButton(
                onPressed: () => _provisionService.reset(),
                child: const Text('重试'),
              ),
          ],
        ),
      ),
    );
  }

  // PLACEHOLDER_FORM_AND_SUCCESS

  Widget _buildWifiForm() {
    final isDisabled =
        _provisionService.state == WifiProvisionState.sending ||
        _provisionService.state == WifiProvisionState.waiting;

    return Form(
      key: _formKey,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Container(
            padding: const EdgeInsets.all(12),
            decoration: BoxDecoration(
              color: Colors.amber.withValues(alpha: 0.08),
              borderRadius: BorderRadius.circular(8),
            ),
            child: Row(
              children: [
                Icon(Icons.info_outline, size: 18, color: Colors.amber.shade700),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(
                    '请输入 2.4GHz WiFi 信息，设备不支持 5GHz 网络',
                    style: TextStyle(fontSize: 13, color: Colors.amber.shade800),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 20),
          TextFormField(
            controller: _ssidController,
            enabled: !isDisabled,
            decoration: InputDecoration(
              labelText: 'WiFi 名称 (SSID)',
              hintText: '输入 WiFi 名称',
              prefixIcon: const Icon(Icons.wifi),
              border: OutlineInputBorder(
                borderRadius: BorderRadius.circular(12),
              ),
            ),
            validator: (value) {
              if (value == null || value.trim().isEmpty) return '请输入 WiFi 名称';
              if (value.trim().length > 32) return 'WiFi 名称不能超过 32 个字符';
              return null;
            },
          ),
          const SizedBox(height: 16),
          TextFormField(
            controller: _passwordController,
            enabled: !isDisabled,
            obscureText: _obscurePassword,
            decoration: InputDecoration(
              labelText: 'WiFi 密码',
              hintText: '输入 WiFi 密码',
              prefixIcon: const Icon(Icons.lock_outline),
              suffixIcon: IconButton(
                icon: Icon(
                  _obscurePassword ? Icons.visibility_off : Icons.visibility,
                ),
                onPressed: () {
                  setState(() => _obscurePassword = !_obscurePassword);
                },
              ),
              border: OutlineInputBorder(
                borderRadius: BorderRadius.circular(12),
              ),
            ),
            validator: (value) {
              if (value == null || value.isEmpty) return '请输入 WiFi 密码';
              if (value.length < 8) return 'WiFi 密码至少 8 位 (WPA2)';
              return null;
            },
          ),
          const SizedBox(height: 24),
          SizedBox(
            height: 48,
            child: ElevatedButton.icon(
              onPressed: isDisabled ? null : _sendCredentials,
              icon: const Icon(Icons.send),
              label: Text(
                isDisabled ? '正在配网...' : '发送配置到设备',
                style: const TextStyle(fontSize: 16),
              ),
              style: ElevatedButton.styleFrom(
                backgroundColor: AppTheme.primaryBlue,
                foregroundColor: Colors.white,
                shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(12),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildSuccessCard() {
    return Center(
      child: Column(
        children: [
          Container(
            width: 64,
            height: 64,
            decoration: BoxDecoration(
              color: Colors.green.withValues(alpha: 0.15),
              shape: BoxShape.circle,
            ),
            child: const Icon(Icons.check_circle, color: Colors.green, size: 40),
          ),
          const SizedBox(height: 16),
          const Text(
            '配网成功！',
            style: TextStyle(
              fontSize: 20,
              fontWeight: FontWeight.w600,
              color: Colors.green,
            ),
          ),
          const SizedBox(height: 8),
          Text(
            '设备已连接到 WiFi 网络\n正在返回...',
            textAlign: TextAlign.center,
            style: TextStyle(fontSize: 14, color: Colors.grey.shade600),
          ),
        ],
      ),
    );
  }
}
