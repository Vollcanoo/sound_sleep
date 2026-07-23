import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'ble_data_service.dart';

/// WiFi 配网状态
enum WifiProvisionState {
  idle,        // 未开始
  sending,     // 正在发送凭据
  waiting,     // 等待设备连接 WiFi
  success,     // 配网成功
  failed,      // 配网失败
}

/// BLE WiFi 配网服务
///
/// 复用 BleDataService 的现有 BLE 连接向 ESP32 发送 WiFi 凭据 JSON，
/// 等待设备回复配网结果。不再创建独立 BLE 连接。
///
/// 协议:
///   手机→设备: {"cmd":"wifi_config","ssid":"xxx","pass":"xxx"}
///   设备→手机: {"status":"ok","msg":"wifi_connected"}
///              {"status":"error","msg":"connect_failed"}
class WifiProvisionService extends ChangeNotifier {
  final BleDataService _bleDataService;
  StreamSubscription? _responseSubscription;

  WifiProvisionState _state = WifiProvisionState.idle;
  String _statusMessage = '';
  String _errorDetail = '';

  WifiProvisionService({required BleDataService bleDataService})
      : _bleDataService = bleDataService;

  WifiProvisionState get state => _state;
  String get statusMessage => _statusMessage;
  String get errorDetail => _errorDetail;

  /// 发送 WiFi 凭据到设备，等待配网结果
  Future<bool> sendWifiCredentials(String ssid, String password) async {
    if (!_bleDataService.isConnected) {
      _setState(WifiProvisionState.failed, '设备未连接');
      _errorDetail = '请先连接 BLE 设备';
      return false;
    }

    _setState(WifiProvisionState.sending, '正在发送 WiFi 配置...');

    try {
      final json = jsonEncode({
        'cmd': 'wifi_config',
        'ssid': ssid,
        'pass': password,
      });

      final completer = Completer<Map<String, dynamic>>();
      String replyBuffer = '';

      _responseSubscription = _bleDataService.rawResponseStream.listen((data) {
        final chunk = utf8.decode(data, allowMalformed: true);
        replyBuffer += chunk;
        try {
          final reply = jsonDecode(replyBuffer) as Map<String, dynamic>;
          if (!completer.isCompleted) {
            completer.complete(reply);
          }
        } catch (_) {}
      });

      final sent = await _bleDataService.sendRawBytes(utf8.encode(json));
      if (!sent) {
        _setState(WifiProvisionState.failed, '发送失败');
        _errorDetail = '写入 BLE 特征失败';
        return false;
      }
      debugPrint('WiFi Provision: 已发送 — $json');

      _setState(WifiProvisionState.waiting, '等待设备连接 WiFi...');

      final reply = await completer.future.timeout(
        const Duration(seconds: 30),
        onTimeout: () => {'status': 'error', 'msg': 'timeout'},
      );
      debugPrint('WiFi Provision: 收到回复 — $reply');

      final status = reply['status'] as String? ?? '';
      final msg = reply['msg'] as String? ?? '';

      if (status == 'ok' && msg == 'credentials_saved') {
        _setState(WifiProvisionState.waiting, '凭据已保存，等待 WiFi 连接...');
        replyBuffer = '';
        final nextCompleter = Completer<Map<String, dynamic>>();

        await _responseSubscription?.cancel();
        _responseSubscription = _bleDataService.rawResponseStream.listen((data) {
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
        _setState(WifiProvisionState.success, '配网成功！设备已连接 WiFi');
        return true;
      } else if (status == 'error') {
        _setState(WifiProvisionState.failed, '配网失败: $msg');
        _errorDetail = msg;
        return false;
      } else {
        _setState(WifiProvisionState.failed, '设备返回未知响应');
        _errorDetail = reply.toString();
        return false;
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
      await _responseSubscription?.cancel();
      _responseSubscription = null;
    }
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
    _responseSubscription?.cancel();
    super.dispose();
  }
}
