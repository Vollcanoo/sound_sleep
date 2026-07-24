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
      _setState(WifiProvisionState.failed, 'Device is not connected');
      _errorDetail = 'Connect to the BLE device first.';
      return false;
    }

    _setState(WifiProvisionState.sending, 'Sending Wi-Fi configuration...');
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
                'Credentials saved; connecting to Wi-Fi...',
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
        _setState(WifiProvisionState.failed, 'Failed to send Wi-Fi configuration');
        _errorDetail = 'BLE characteristic write failed.';
        return false;
      }

      _setState(WifiProvisionState.waiting, 'Waiting for the device to connect to Wi-Fi...');
      final success = await completer.future.timeout(
        const Duration(seconds: 30),
        onTimeout: () {
          _errorDetail = 'timeout';
          return false;
        },
      );

      if (success) {
        _setState(WifiProvisionState.success, 'Wi-Fi connected successfully');
        return true;
      }

      _setState(WifiProvisionState.failed, 'The device could not connect to Wi-Fi');
      return false;
    } catch (e) {
      _setState(WifiProvisionState.failed, 'Failed to configure Wi-Fi');
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
