import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'ble_data_service.dart';

enum WifiProvisionState { idle, sending, waiting, success, failed }

/// Sends Wi-Fi credentials over the existing BLE UART connection.
///
/// The UART also carries posture and snore telemetry.  Provisioning therefore
/// only acts on newline-delimited JSON acknowledgements containing both
/// `status` and `msg`; unrelated telemetry cannot complete this operation.
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

  Future<bool> sendWifiCredentials(String ssid, String password) async {
    if (!_bleDataService.isConnected) {
      _setState(WifiProvisionState.failed, '设备未连接');
      _errorDetail = '请先连接蓝牙设备。';
      return false;
    }

    _setState(WifiProvisionState.sending, '正在发送 Wi-Fi 配置...');
    final completer = Completer<bool>();
    var replyBuffer = '';

    try {
      await _responseSubscription?.cancel();
      _responseSubscription = _bleDataService.rawResponseStream.listen((data) {
        replyBuffer += utf8.decode(data, allowMalformed: true);

        while (replyBuffer.contains('\n')) {
          final newline = replyBuffer.indexOf('\n');
          final frame = replyBuffer.substring(0, newline).trim();
          replyBuffer = replyBuffer.substring(newline + 1);
          if (frame.isEmpty) continue;

          try {
            final reply = jsonDecode(frame);
            if (reply is! Map<String, dynamic>) continue;

            final status = reply['status'];
            final msg = reply['msg'];
            if (status is! String || msg is! String) continue;

            debugPrint('Wi-Fi provision reply: $reply');
            if (status == 'ok' && msg == 'credentials_saved') {
              _setState(
                WifiProvisionState.waiting,
                '凭据已保存，正在连接 Wi-Fi...',
              );
            } else if (status == 'ok' && msg == 'wifi_connected') {
              if (!completer.isCompleted) completer.complete(true);
            } else if (status == 'error') {
              _errorDetail = msg;
              if (!completer.isCompleted) completer.complete(false);
            }
          } catch (_) {
            // Ignore malformed and unrelated UART frames.
          }
        }
      });

      final sent = await _bleDataService.sendRawBytes(
        utf8.encode(jsonEncode({'cmd': 'wifi_config', 'ssid': ssid, 'pass': password})),
      );
      if (!sent) {
        _setState(WifiProvisionState.failed, '发送 Wi-Fi 配置失败');
        _errorDetail = 'BLE 特征写入失败。';
        return false;
      }

      _setState(WifiProvisionState.waiting, '等待设备连接 Wi-Fi...');
      final success = await completer.future.timeout(
        const Duration(seconds: 30),
        onTimeout: () {
          _errorDetail = 'timeout';
          return false;
        },
      );

      if (success) {
        _setState(WifiProvisionState.success, '配网成功！设备已连接 Wi-Fi');
        return true;
      }

      _setState(WifiProvisionState.failed, '设备无法连接 Wi-Fi');
      return false;
    } catch (e) {
      _setState(WifiProvisionState.failed, '配置 Wi-Fi 失败');
      _errorDetail = e.toString();
      debugPrint('Wi-Fi provisioning error: $e');
      return false;
    } finally {
      await _responseSubscription?.cancel();
      _responseSubscription = null;
    }
  }

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
