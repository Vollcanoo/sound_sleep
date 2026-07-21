import 'dart:async';
import 'package:flutter/foundation.dart';
import '../models/posture_event.dart';
import '../models/sleep_record.dart';
import '../models/snore_result.dart';
import '../services/ble_data_service.dart';
import '../services/snore_api_service.dart';
import '../services/sleep_record_generator.dart';
import '../services/sleep_service.dart';

/// 管理实时睡眠监测状态
///
/// 桥接 BLE 传感器数据 → 睡眠记录生成 → 存储
class RealtimeProvider extends ChangeNotifier {
  final BleDataService _bleService;
  final SnoreApiService _snoreService;
  final SleepService _sleepService;

  StreamSubscription? _readingSubscription;

  // ── 监测状态 ──
  bool _isMonitoring = false;
  PostureReading? _currentReading;
  DateTime? _monitoringStart;

  // ── 累积的会话数据 ──
  final List<PostureReading> _sessionReadings = [];

  // ── 设备连接状态 ──
  bool _isDeviceConnected = false;

  // ignore: prefer_initializing_formals — can't use this._ with named params + constructor body
  RealtimeProvider({
    required BleDataService bleService,
    required SnoreApiService snoreService,
    required SleepService sleepService,
  }) : _bleService = bleService, // ignore: prefer_initializing_formals
       _snoreService = snoreService, // ignore: prefer_initializing_formals
       _sleepService = sleepService {
    // ignore: prefer_initializing_formals
    // 监听 BLE 连接状态变化
    _bleService.connectionStream.listen((connected) {
      _isDeviceConnected = connected;
      notifyListeners();
    });
  }

  // ── Getters ──

  bool get isMonitoring => _isMonitoring;
  bool get isDeviceConnected => _isDeviceConnected;
  PostureReading? get currentReading => _currentReading;
  DateTime? get monitoringStart => _monitoringStart;
  int get readingCount => _sessionReadings.length;
  bool get isOnBed => _currentReading?.isOnBed ?? false;
  String get currentPostureLabel => _currentReading?.posture.label ?? '未知';
  double get currentPressure => _currentReading?.totalPressure ?? 0;

  /// 开始监测（订阅 BLE 数据流）
  Future<bool> startMonitoring() async {
    if (_isMonitoring) return true;

    if (!await _bleService.sendCommand('monitor_start')) {
      return false;
    }

    _beginMonitoringSession();
    return true;
  }

  void _beginMonitoringSession() {
    _isMonitoring = true;
    _monitoringStart = DateTime.now();
    _sessionReadings.clear();

    _readingSubscription = _bleService.readingStream.listen((reading) {
      _currentReading = reading;
      _sessionReadings.add(reading);
      notifyListeners();
    });

    notifyListeners();
  }

  /// 停止监测并生成睡眠报告
  ///
  /// 返回生成的 [SleepRecord]，若没有有效数据则返回 null。
  Future<SleepRecord?> stopMonitoringAndGenerateReport(String userId) async {
    if (_isMonitoring) {
      await _bleService.sendCommand('monitor_stop');
    }
    _isMonitoring = false;
    _readingSubscription?.cancel();
    _readingSubscription = null;

    if (_sessionReadings.isEmpty) {
      notifyListeners();
      return null;
    }

    // 从云端获取打鼾检测结果
    SnoreResult? snoreResult;
    try {
      snoreResult = await _snoreService.fetchSnoreResult(
        patientId: userId,
        date: DateTime.now(),
      );
    } catch (e) {
      debugPrint('Failed to fetch snore result: $e');
    }

    // 从真实传感器数据生成睡眠记录
    final record = SleepRecordGenerator.generate(
      readings: _sessionReadings,
      userId: userId,
      snoreResult: snoreResult,
    );

    // 存储到 SleepService
    _sleepService.addRecord(record);

    _sessionReadings.clear();
    _currentReading = null;
    notifyListeners();

    return record;
  }

  /// 启动演示/模拟监测模式
  Future<void> startDemoMonitoring() async {
    _bleService.startMockStream();
    _beginMonitoringSession();
  }

  /// 停止演示监测
  Future<SleepRecord?> stopDemoMonitoring(String userId) async {
    _bleService.stopMockStream();
    return stopMonitoringAndGenerateReport(userId);
  }

  @override
  void dispose() {
    _readingSubscription?.cancel();
    super.dispose();
  }
}
