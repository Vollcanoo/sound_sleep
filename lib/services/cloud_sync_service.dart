import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import '../models/sleep_record.dart';
import '../models/posture_event.dart';
import 'cloudbase_db.dart';

/// CloudBase SQL 数据同步服务
///
/// 负责将本地睡眠数据上传到腾讯云 CloudBase SQL 数据库，
/// 以及从云端拉取历史记录。
class CloudSyncService {
  final CloudBaseDB _db = CloudBaseDB();

  final List<_PendingUpload> _pendingUploads = [];
  Timer? _retryTimer;
  static const int _maxRetries = 3;
  static const Duration _retryDelay = Duration(seconds: 5);

  /// 上传一条完整的睡眠记录及其关联数据
  Future<void> uploadSleepRecord(SleepRecord record) async {
    int? recordId;

    // Step 1: 插入 sleep_records 并获取 record_id（带重试）
    try {
      recordId = await _insertAndGetRecordId(record);
    } catch (e) {
      debugPrint('[CloudSync] sleep_records 插入失败: $e');
      _enqueueRetry(record);
      return;
    }

    if (recordId == null) {
      debugPrint('[CloudSync] 无法获取 record_id，加入重试队列');
      _enqueueRetry(record);
      return;
    }

    debugPrint('[CloudSync] sleep_records 上传成功, record_id=$recordId');

    // Step 2: 独立上传各子表，互不影响
    await _uploadSubTables(recordId, record);
  }

  /// 插入 sleep_records 并获取自增 id，最多重试 3 次查询
  Future<int?> _insertAndGetRecordId(SleepRecord record) async {
    final success = await _db.insert('sleep_records', {
      'date': record.date.toIso8601String().substring(0, 10),
      'bed_time': record.bedTime.toIso8601String(),
      'wake_time': record.wakeTime.toIso8601String(),
      'duration_minutes': record.durationMinutes,
      'sleep_score': record.sleepScore,
      'get_up_count': record.getUpCount,
      'device_id': 'device_esp32',
      'created_at': DateTime.now().toIso8601String(),
    });

    if (!success) return null;

    // 带重试的 record_id 查询
    for (int attempt = 0; attempt < _maxRetries; attempt++) {
      if (attempt > 0) {
        await Future.delayed(Duration(milliseconds: 500 * attempt));
      }

      final inserted = await _db.query(
        'sleep_records',
        where:
            'date=eq.${record.date.toIso8601String().substring(0, 10)}&bed_time=eq.${record.bedTime.toIso8601String()}',
        orderBy: 'id.desc',
        limit: 1,
      );

      if (inserted.isNotEmpty && inserted.first['id'] != null) {
        return inserted.first['id'] as int;
      }

      debugPrint(
          '[CloudSync] record_id 查询第${attempt + 1}次未找到，重试...');
    }

    // 最后尝试：按 device_id 查最新一条
    final fallback = await _db.query(
      'sleep_records',
      where: 'device_id=eq.device_esp32',
      orderBy: 'id.desc',
      limit: 1,
    );

    if (fallback.isNotEmpty && fallback.first['id'] != null) {
      debugPrint('[CloudSync] fallback 查询到 record_id');
      return fallback.first['id'] as int;
    }

    return null;
  }

  /// 独立上传各子表，每个子表失败不影响其他子表
  Future<void> _uploadSubTables(int recordId, SleepRecord record) async {
    // 各子表独立上传，互不阻塞
    final results = await Future.wait([
      _safeUpload('snoring_events',
          () => _uploadSnoringEvents(recordId, record.snoringEvents)),
      _safeUpload('posture_segments',
          () => _uploadPostureSegments(recordId, record.postureSegments)),
      _safeUpload('pressure_segments',
          () => _uploadPressureSegments(recordId, record.pressureSegments)),
    ]);

    if (record.aiAnalysis != null) {
      await _safeUpload(
          'ai_analyses', () => _uploadAiAnalysis(recordId, record.aiAnalysis!));
    }

    final allOk = results.every((ok) => ok);
    if (allOk) {
      debugPrint('[CloudSync] 全部子表上传成功: ${record.date}');
    } else {
      debugPrint('[CloudSync] 部分子表上传失败: ${record.date}');
    }
  }

  /// 安全执行上传，捕获异常，返回是否成功
  Future<bool> _safeUpload(String tableName, Future<void> Function() fn) async {
    try {
      await fn();
      debugPrint('[CloudSync] ✓ $tableName 上传成功');
      return true;
    } catch (e) {
      debugPrint('[CloudSync] ✗ $tableName 上传失败: $e');
      return false;
    }
  }

  Future<void> _uploadSnoringEvents(
      int recordId, List<SnoringEvent> events) async {
    for (final event in events) {
      final ok = await _db.insert('snoring_events', {
        'record_id': recordId,
        'start_time': event.startTime.toIso8601String(),
        'end_time': event.endTime.toIso8601String(),
        'avg_decibel': event.avgDecibel,
        'avg_probability': event.avgProbability ?? 0.0,
      });
      if (!ok) {
        debugPrint('[CloudSync] snoring_event 单条插入失败，继续其余');
      }
    }
  }

  Future<void> _uploadPostureSegments(
      int recordId, List<PostureSegment> segments) async {
    for (final seg in segments) {
      final ok = await _db.insert('posture_segments', {
        'record_id': recordId,
        'start_time': seg.startTime.toIso8601String(),
        'end_time': seg.endTime.toIso8601String(),
        'posture': seg.posture.name,
        'avg_confidence': seg.avgConfidence,
      });
      if (!ok) {
        debugPrint('[CloudSync] posture_segment 单条插入失败，继续其余');
      }
    }
  }

  Future<void> _uploadPressureSegments(
      int recordId, List<PressureSegment> segments) async {
    for (final seg in segments) {
      final ok = await _db.insert('pressure_segments', {
        'record_id': recordId,
        'start_time': seg.startTime.toIso8601String(),
        'end_time': seg.endTime.toIso8601String(),
        'level': 'medium',
        'avg_value': 0.0,
      });
      if (!ok) {
        debugPrint('[CloudSync] pressure_segment 单条插入失败，继续其余');
      }
    }
  }

  Future<void> _uploadAiAnalysis(int recordId, AiAnalysis analysis) async {
    await _db.insert('ai_analyses', {
      'record_id': recordId,
      'summary': analysis.summary,
      'insights': jsonEncode(analysis.insights),
      'suggestions': jsonEncode(analysis.suggestions),
      'created_at': analysis.analyzedAt.toIso8601String(),
    });
  }

  // ── 重试机制 ──

  void _enqueueRetry(SleepRecord record) {
    final existing = _pendingUploads.where((p) => p.record.id == record.id);
    if (existing.isNotEmpty) {
      debugPrint('[CloudSync] 记录 ${record.id} 已在重试队列中');
      return;
    }
    _pendingUploads.add(_PendingUpload(record: record));
    _scheduleRetry();
  }

  void _scheduleRetry() {
    if (_retryTimer?.isActive ?? false) return;
    if (_pendingUploads.isEmpty) return;

    _retryTimer = Timer(_retryDelay, () async {
      if (_pendingUploads.isEmpty) return;

      final pending = List<_PendingUpload>.from(_pendingUploads);
      for (final item in pending) {
        item.attempts++;
        debugPrint(
            '[CloudSync] 重试上传 ${item.record.id} (第${item.attempts}次)');

        try {
          await uploadSleepRecord(item.record);
          _pendingUploads.remove(item);
          debugPrint('[CloudSync] 重试成功: ${item.record.id}');
        } catch (e) {
          debugPrint('[CloudSync] 重试失败: $e');
          if (item.attempts >= _maxRetries) {
            _pendingUploads.remove(item);
            debugPrint(
                '[CloudSync] 达到最大重试次数，放弃: ${item.record.id}');
          }
        }
      }

      if (_pendingUploads.isNotEmpty) {
        _scheduleRetry();
      }
    });
  }

  void dispose() {
    _retryTimer?.cancel();
  }

  /// 删除一条睡眠记录及其关联数据
  Future<void> deleteSleepRecord(String recordId) async {
    // recordId 格式为 "cloud_123"，提取数字部分
    final idStr = recordId.startsWith('cloud_')
        ? recordId.substring(6)
        : recordId;
    final numericId = int.tryParse(idStr);
    if (numericId == null) return;

    try {
      await _db.delete('ai_analyses', where: 'record_id=eq.$numericId');
      await _db.delete('snoring_events', where: 'record_id=eq.$numericId');
      await _db.delete('posture_segments', where: 'record_id=eq.$numericId');
      await _db.delete('pressure_segments', where: 'record_id=eq.$numericId');
      await _db.delete('sleep_records', where: 'id=eq.$numericId');
      debugPrint('[CloudSync] 删除成功: $recordId');
    } catch (e) {
      debugPrint('[CloudSync] 删除失败: $e');
      rethrow;
    }
  }

  /// 从云端拉取睡眠记录
  Future<List<SleepRecord>> fetchRecords({int limit = 30}) async {
    final rows = await _db.query(
      'sleep_records',
      orderBy: 'date.desc',
      limit: limit,
    );

    final records = <SleepRecord>[];
    for (final row in rows) {
      final recordId = row['id'] as int;
      records.add(await _toSleepRecord(row, recordId));
    }
    return records;
  }

  Future<SleepRecord> _toSleepRecord(
      Map<String, dynamic> row, int recordId) async {
    final snoringEvents = await _fetchSnoringEvents(recordId);
    final postureSegments = await _fetchPostureSegments(recordId);
    final pressureSegments = await _fetchPressureSegments(recordId);
    final aiAnalysis = await _fetchAiAnalysis(recordId);

    return SleepRecord(
      id: 'cloud_$recordId',
      userId: row['_openid'] as String? ?? 'anon',
      date: DateTime.parse(row['date'] as String),
      bedTime: DateTime.parse(row['bed_time'] as String),
      wakeTime: DateTime.parse(row['wake_time'] as String),
      sleepScore: (row['sleep_score'] as num?)?.toInt() ?? 0,
      getUpCount: (row['get_up_count'] as num?)?.toInt() ?? 0,
      snoringEvents: snoringEvents,
      postureSegments: postureSegments,
      pressureSegments: pressureSegments,
      aiAnalysis: aiAnalysis,
    );
  }

  Future<List<SnoringEvent>> _fetchSnoringEvents(int recordId) async {
    final rows = await _db.query(
      'snoring_events',
      where: 'record_id=eq.$recordId',
    );
    return rows.map((r) => SnoringEvent(
      startTime: DateTime.parse(r['start_time'] as String),
      endTime: DateTime.parse(r['end_time'] as String),
      avgDecibel: (r['avg_decibel'] as num?)?.toDouble() ?? 0,
      avgProbability: (r['avg_probability'] as num?)?.toDouble(),
    )).toList();
  }

  Future<List<PostureSegment>> _fetchPostureSegments(int recordId) async {
    final rows = await _db.query(
      'posture_segments',
      where: 'record_id=eq.$recordId',
    );
    return rows.map((r) => PostureSegment(
      startTime: DateTime.parse(r['start_time'] as String),
      endTime: DateTime.parse(r['end_time'] as String),
      posture: PostureType.fromString(r['posture'] as String? ?? ''),
      avgConfidence: (r['avg_confidence'] as num?)?.toDouble() ?? 0,
    )).toList();
  }

  Future<List<PressureSegment>> _fetchPressureSegments(int recordId) async {
    final rows = await _db.query(
      'pressure_segments',
      where: 'record_id=eq.$recordId',
    );
    return rows.map((r) => PressureSegment(
      startTime: DateTime.parse(r['start_time'] as String),
      endTime: DateTime.parse(r['end_time'] as String),
    )).toList();
  }

  Future<AiAnalysis?> _fetchAiAnalysis(int recordId) async {
    final rows = await _db.query(
      'ai_analyses',
      where: 'record_id=eq.$recordId',
      limit: 1,
    );
    if (rows.isEmpty) return null;
    final r = rows.first;

    List<String> suggestions = [];
    if (r['suggestions'] != null) {
      try {
        suggestions = List<String>.from(jsonDecode(r['suggestions'] as String));
      } catch (_) {
        suggestions = [r['suggestions'] as String];
      }
    }

    List<String> insights = [];
    if (r['insights'] != null) {
      try {
        insights = List<String>.from(jsonDecode(r['insights'] as String));
      } catch (_) {
        insights = [r['insights'] as String];
      }
    }

    return AiAnalysis(
      summary: r['summary'] as String? ?? '',
      insights: insights,
      suggestions: suggestions,
      analyzedAt: r['created_at'] != null
          ? DateTime.parse(r['created_at'] as String)
          : DateTime.now(),
    );
  }
}

class _PendingUpload {
  final SleepRecord record;
  int attempts = 0;

  _PendingUpload({required this.record});
}
