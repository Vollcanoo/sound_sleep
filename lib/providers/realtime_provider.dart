import 'dart:async';
import 'package:flutter/foundation.dart';
import '../models/posture_event.dart';
import '../models/realtime_snore_reading.dart';
import '../models/sleep_record.dart';
import '../models/snore_result.dart';
import '../services/ble_data_service.dart';
import '../services/snore_api_service.dart';
import '../services/sleep_record_generator.dart';
import '../services/sleep_service.dart';

enum PumpMode { llm, local }

/// 管理实时睡眠监测状态
///
/// 桥接 BLE 传感器数据 → 睡眠记录生成 → 存储
class RealtimeProvider extends ChangeNotifier {
  final BleDataService _bleService;
  final SnoreApiService _snoreService;
  final SleepService _sleepService;

  StreamSubscription? _readingSubscription;
  StreamSubscription? _snoreReadingSubscription;

  // ── 监测状态 ──
  bool _isMonitoring = false;
  PostureReading? _currentReading;
  RealtimeSnoreReading? _currentSnoreReading;
  DateTime? _monitoringStart;
  PumpMode _pumpMode = PumpMode.llm;

  // ── 累积的会话数据 ──
  final List<PostureReading> _sessionReadings = [];
  final List<RealtimeSnoreReading> _sessionSnoreReadings = [];

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
  RealtimeSnoreReading? get currentSnoreReading => _currentSnoreReading;
  DateTime? get monitoringStart => _monitoringStart;
  int get readingCount => _sessionReadings.length;
  bool get isOnBed => _currentReading?.isOnBed ?? false;
  String get currentPostureLabel => _currentReading?.posture.label ?? '未知';
  double get currentPressure => _currentReading?.totalPressure ?? 0;
  PumpMode get pumpMode => _pumpMode;

  Future<void> setPumpMode(PumpMode mode) async {
    if (_pumpMode == mode) return;
    final oldMode = _pumpMode;
    _pumpMode = mode;
    notifyListeners();
    final command = mode == PumpMode.llm ? 'pump_mode_llm' : 'pump_mode_local';
    final success = await _bleService.sendCommand(command);
    if (!success) {
      _pumpMode = oldMode;
      notifyListeners();
    }
  }

  /// 开始监测（订阅 BLE 数据流）
  Future<bool> startMonitoring() async {
    if (_isMonitoring) return true;

    if (!await _bleService.sendCommand('monitor_start')) {
      return false;
    }

    // 同步当前 pump mode 到 ESP32，防止重启后状态不一致
    await Future.delayed(const Duration(milliseconds: 100));
    final modeCmd = _pumpMode == PumpMode.llm ? 'pump_mode_llm' : 'pump_mode_local';
    if (!await _bleService.sendCommand(modeCmd)) {
      debugPrint('pump mode 同步失败，ESP32 将使用默认模式');
    }

    _beginMonitoringSession();
    return true;
  }

  void _beginMonitoringSession() {
    _isMonitoring = true;
    _monitoringStart = DateTime.now();
    _sessionReadings.clear();
    _sessionSnoreReadings.clear();

    _readingSubscription = _bleService.readingStream.listen((reading) {
      _currentReading = reading;
      _sessionReadings.add(reading);
      if (_sessionReadings.length > 43200) {
        _sessionReadings.removeAt(0);
      }
      notifyListeners();
    });
    _snoreReadingSubscription = _bleService.snoreReadingStream.listen((
      reading,
    ) {
      _currentSnoreReading = reading;
      _sessionSnoreReadings.add(reading);
      if (_sessionSnoreReadings.length > 43200) {
        _sessionSnoreReadings.removeAt(0);
      }
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
    _snoreReadingSubscription?.cancel();
    _snoreReadingSubscription = null;

    if (_sessionReadings.isEmpty) {
      notifyListeners();
      return null;
    }

    // 从云端获取打鼾检测结果
    final realtimeSnoreReadings = List<RealtimeSnoreReading>.from(
      _sessionSnoreReadings,
    );
    SnoreResult? snoreResult;
    if (realtimeSnoreReadings.isEmpty) {
      try {
        snoreResult = await _snoreService.fetchSnoreResult(
          patientId: userId,
          date: DateTime.now(),
        );
      } catch (e) {
        debugPrint('Failed to fetch snore result: $e');
      }
    }

    // 从真实传感器数据生成睡眠记录
    final record = SleepRecordGenerator.generate(
      readings: _sessionReadings,
      userId: userId,
      snoreResult: snoreResult,
      realtimeSnoringEvents: realtimeSnoreReadings.isEmpty
          ? null
          : _buildRealtimeSnoringEvents(realtimeSnoreReadings),
    );

    // 存储到 SleepService
    _sleepService.addRecord(record);

    _sessionReadings.clear();
    _sessionSnoreReadings.clear();
    _currentReading = null;
    _currentSnoreReading = null;
    notifyListeners();

    return record;
  }

  /// 从概率估算分贝值（仅在 ESP32 未发送 rms_db 时使用）
  ///
  /// 映射区间: probability 0.5→40dB, 1.0→75dB（线性插值）
  static double _estimateDecibel(double probability) {
    const minDb = 40.0;
    const maxDb = 75.0;
    final clamped = probability.clamp(0.5, 1.0);
    return minDb + (clamped - 0.5) / 0.5 * (maxDb - minDb);
  }

  List<SnoringEvent> _buildRealtimeSnoringEvents(
    List<RealtimeSnoreReading> readings,
  ) {
    final detected = readings.where((reading) => reading.detected).toList()
      ..sort((a, b) => a.timestamp.compareTo(b.timestamp));
    if (detected.isEmpty) return [];

    final events = <SnoringEvent>[];
    var start = detected.first.timestamp;
    var end = start.add(
      Duration(milliseconds: (detected.first.windowSeconds * 1000).round()),
    );
    var probabilitySum = detected.first.probability;
    var dbSum = detected.first.rmsDb;
    var count = 1;

    for (final reading in detected.skip(1)) {
      final readingEnd = reading.timestamp.add(
        Duration(milliseconds: (reading.windowSeconds * 1000).round()),
      );
      const gapTolerance = Duration(seconds: 2);
      if (!reading.timestamp.isAfter(end.add(gapTolerance))) {
        if (readingEnd.isAfter(end)) end = readingEnd;
        probabilitySum += reading.probability;
        dbSum += reading.rmsDb;
        count++;
        continue;
      }

      final avgProb = probabilitySum / count;
      final avgDb = dbSum / count;
      events.add(
        SnoringEvent(
          startTime: start,
          endTime: end,
          avgDecibel: avgDb > 0 ? avgDb : _estimateDecibel(avgProb),
          avgProbability: avgProb,
        ),
      );
      start = reading.timestamp;
      end = readingEnd;
      probabilitySum = reading.probability;
      dbSum = reading.rmsDb;
      count = 1;
    }

    final avgProb = probabilitySum / count;
    final avgDb = dbSum / count;
    events.add(
      SnoringEvent(
        startTime: start,
        endTime: end,
        avgDecibel: avgDb > 0 ? avgDb : _estimateDecibel(avgProb),
        avgProbability: avgProb,
      ),
    );
    return events;
  }

  @override
  void dispose() {
    _readingSubscription?.cancel();
    _snoreReadingSubscription?.cancel();
    super.dispose();
  }
}
