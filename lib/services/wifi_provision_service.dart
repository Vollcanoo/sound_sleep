import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'ble_data_service.dart';

/// WiFi 配网状态
enum WifiProvisionState {
  idle,        // 未开始
  connecting,  // 正在连接 BLE
  sending,     // 正在发送凭据
  waiting,     // 等待设备连接 WiFi
  success,     // 配网成功
  failed,      // 配网失败
}

/// BLE WiFi 配网服务
///
/// 通过 Nordic UART Service (NUS) 向 ESP32 发送 WiFi 凭据 JSON，
/// 等待设备回复配网结果。
///
/// 协议:
///   手机→设备: {"cmd":"wifi_config","ssid":"xxx","pass":"xxx"}
///   设备→手机: {"status":"ok","msg":"wifi_connected"}
///              {"status":"error","msg":"connect_failed"}
class WifiProvisionService extends ChangeNotifier {
  BluetoothDevice? _device;
  BluetoothCharacteristic? _txCharacteristic;
  BluetoothCharacteristic? _rxCharacteristic;
  StreamSubscription? _notifySubscription;
  StreamSubscription? _connectionSubscription;

  WifiProvisionState _state = WifiProvisionState.idle;
  String _statusMessage = '';
  String _errorDetail = '';

  WifiProvisionState get state => _state;
  String get statusMessage => _statusMessage;
  String get errorDetail => _errorDetail;

  /// 连接到 ESP32 设备并发现 NUS 服务
  Future<void> connect(BluetoothDevice device) async {
    _device = device;
    _setState(WifiProvisionState.connecting, '正在连接设备...');

    try {
      // 连接 BLE
      await device.connect(timeout: const Duration(seconds: 10));

      // 监听连接状态
      _connectionSubscription = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          debugPrint('WiFi Provision: BLE 已断开');
        }
      });

      // 发现服务
      final services = await device.discoverServices();
      final uartService = services.firstWhere(
        (s) => s.uuid.toString().toLowerCase() == BleUuids.uartService,
        orElse: () => throw Exception('未找到 UART 服务，请确认设备是 SleepMonitor'),
      );

      // 获取 TX (ESP32→手机, Notify) 和 RX (手机→ESP32, Write) 特征
      _txCharacteristic = uartService.characteristics.firstWhere(
        (c) => c.uuid.toString().toLowerCase() == BleUuids.uartTx,
        orElse: () => throw Exception('未找到 TX 特征'),
      );
      _rxCharacteristic = uartService.characteristics.firstWhere(
        (c) => c.uuid.toString().toLowerCase() == BleUuids.uartRx,
        orElse: () => throw Exception('未找到 RX 特征'),
      );

      // 启用 TX 通知（接收设备回复）
      await _txCharacteristic!.setNotifyValue(true);

      _setState(WifiProvisionState.idle, '已连接，可以发送 WiFi 配置');
      debugPrint('WiFi Provision: BLE 连接成功');
    } catch (e) {
      _setState(WifiProvisionState.failed, '连接设备失败');
      _errorDetail = e.toString();
      debugPrint('WiFi Provision: 连接失败 — $e');
      rethrow;
    }
  }

  /// 发送 WiFi 凭据到设备，等待配网结果
  ///
  /// 返回 true 表示设备已成功连接 WiFi
  Future<bool> sendWifiCredentials(String ssid, String password) async {
    if (_rxCharacteristic == null || _txCharacteristic == null) {
      _setState(WifiProvisionState.failed, '未连接设备');
      return false;
    }

    _setState(WifiProvisionState.sending, '正在发送 WiFi 配置...');

    try {
      // 构造 JSON
      final json = jsonEncode({
        'cmd': 'wifi_config',
        'ssid': ssid,
        'pass': password,
      });

      // 设置回复监听（带超时）
      final completer = Completer<Map<String, dynamic>>();
      String replyBuffer = '';

      _notifySubscription = _txCharacteristic!.onValueReceived.listen((data) {
        final chunk = utf8.decode(data, allowMalformed: true);
        replyBuffer += chunk;

        // 尝试解析完整 JSON
        try {
          final reply = jsonDecode(replyBuffer) as Map<String, dynamic>;
          if (!completer.isCompleted) {
            completer.complete(reply);
          }
        } catch (_) {
          // JSON 不完整，等待更多数据
        }
      });

      // 发送 WiFi 凭据
      final bytes = utf8.encode(json);
      await _rxCharacteristic!.write(bytes, withoutResponse: true);
      debugPrint('WiFi Provision: 已发送 — $json');

      _setState(WifiProvisionState.waiting, '等待设备连接 WiFi...');

      // 等待设备回复（首次回复是 credentials_saved，第二次是 wifi_connected/connect_failed）
      // 设备可能会发送两条回复:
      //   1. {"status":"ok","msg":"credentials_saved"} — 凭据已保存
      //   2. {"status":"ok","msg":"wifi_connected"} — WiFi 已连接
      // 或直接发送 wifi_connected
      // 超时 30 秒（设备需要时间连接 WiFi）

      bool credentialsSaved = false;

      while (true) {
        final reply = await completer.future.timeout(
          const Duration(seconds: 30),
          onTimeout: () => {'status': 'error', 'msg': 'timeout'},
        );

        debugPrint('WiFi Provision: 收到回复 — $reply');

        final status = reply['status'] as String? ?? '';
        final msg = reply['msg'] as String? ?? '';

        if (status == 'ok' && msg == 'credentials_saved') {
          // 凭据已保存，继续等待 WiFi 连接结果
          credentialsSaved = true;
          _setState(WifiProvisionState.waiting, '凭据已保存，等待 WiFi 连接...');
          // 重置 completer 等待下一条消息
          replyBuffer = '';
          final nextCompleter = Completer<Map<String, dynamic>>();

          await _notifySubscription?.cancel();
          _notifySubscription = _txCharacteristic!.onValueReceived.listen((data) {
            final chunk = utf8.decode(data, allowMalformed: true);
            replyBuffer += chunk;
            try {
              final nextReply = jsonDecode(replyBuffer) as Map<String, dynamic>;
              if (!nextCompleter.isCompleted) {
                nextCompleter.complete(nextReply);
              }
            } catch (_) {}
          });

          final nextReply = await nextCompleter.future.timeout(
            const Duration(seconds: 30),
            onTimeout: () => {'status': 'error', 'msg': 'wifi_timeout'},
          );

          final nextStatus = nextReply['status'] as String? ?? '';
          final nextMsg = nextReply['msg'] as String? ?? '';

          if (nextStatus == 'ok' && nextMsg == 'wifi_connected') {
            _setState(WifiProvisionState.success, '配网成功！设备已连接 WiFi');
            return true;
          } else {
            _setState(WifiProvisionState.failed, '设备连接 WiFi 失败');
            _errorDetail = nextMsg;
            return false;
          }
        } else if (status == 'ok' && msg == 'wifi_connected') {
          // 直接返回成功
          _setState(WifiProvisionState.success, '配网成功！设备已连接 WiFi');
          return true;
        } else if (status == 'error') {
          _setState(WifiProvisionState.failed, '配网失败: $msg');
          _errorDetail = msg;
          return false;
        } else {
          // 未知回复
          _setState(WifiProvisionState.failed, '设备返回未知响应');
          _errorDetail = reply.toString();
          return false;
        }
      }
    } catch (e) {
      if (e is TimeoutException) {
        _setState(WifiProvisionState.failed, '等待设备响应超时');
        _errorDetail = '设备未在 30 秒内回复';
      } else {
        _setState(WifiProvisionState.failed, '发送失败');
        _errorDetail = e.toString();
      }
      debugPrint('WiFi Provision: 错误 — $e');
      return false;
    } finally {
      await _notifySubscription?.cancel();
      _notifySubscription = null;
    }
  }

  /// 断开 BLE 连接
  Future<void> disconnect() async {
    await _notifySubscription?.cancel();
    _notifySubscription = null;

    await _connectionSubscription?.cancel();
    _connectionSubscription = null;

    if (_txCharacteristic != null) {
      try {
        await _txCharacteristic!.setNotifyValue(false);
      } catch (_) {}
    }

    if (_device != null) {
      try {
        await _device!.disconnect();
      } catch (_) {}
    }

    _device = null;
    _txCharacteristic = null;
    _rxCharacteristic = null;
    _setState(WifiProvisionState.idle, '');

    debugPrint('WiFi Provision: 已断开');
  }

  /// 重置状态（允许重试）
  void reset() {
    _setState(WifiProvisionState.idle, '');
    _errorDetail = '';
  }

  void _setState(WifiProvisionState newState, String message) {
    _state = newState;
    _statusMessage = message;
    notifyListeners();
  }

  @override
  void dispose() {
    disconnect();
    super.dispose();
  }
}
