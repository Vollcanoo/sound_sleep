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

  /// 上传一条完整的睡眠记录及其关联数据
  Future<void> uploadSleepRecord(SleepRecord record) async {
    try {
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

      if (!success) {
        throw Exception('插入 sleep_records 失败');
      }

      // 查询刚插入的记录获取自增 id
      final inserted = await _db.query(
        'sleep_records',
        where: 'date=eq.${record.date.toIso8601String().substring(0, 10)}&bed_time=eq.${record.bedTime.toIso8601String()}',
        orderBy: 'id.desc',
        limit: 1,
      );
      if (inserted.isEmpty) return;
      final recordId = inserted.first['id'] as int;

      // 上传子表数据
      await _uploadSnoringEvents(recordId, record.snoringEvents);
      await _uploadPostureSegments(recordId, record.postureSegments);
      await _uploadPressureSegments(recordId, record.pressureSegments);

      if (record.aiAnalysis != null) {
        await _uploadAiAnalysis(recordId, record.aiAnalysis!);
      }

      debugPrint('[CloudSync] 上传成功: ${record.date}');
    } catch (e) {
      debugPrint('[CloudSync] 上传失败: $e');
      rethrow;
    }
  }

  Future<void> _uploadSnoringEvents(
      int recordId, List<SnoringEvent> events) async {
    for (final event in events) {
      await _db.insert('snoring_events', {
        'record_id': recordId,
        'start_time': event.startTime.toIso8601String(),
        'end_time': event.endTime.toIso8601String(),
        'avg_decibel': event.avgDecibel,
        'avg_probability': event.avgProbability ?? 0.0,
      });
    }
  }

  Future<void> _uploadPostureSegments(
      int recordId, List<PostureSegment> segments) async {
    for (final seg in segments) {
      await _db.insert('posture_segments', {
        'record_id': recordId,
        'start_time': seg.startTime.toIso8601String(),
        'end_time': seg.endTime.toIso8601String(),
        'posture': seg.posture.name,
        'avg_confidence': seg.avgConfidence,
      });
    }
  }

  Future<void> _uploadPressureSegments(
      int recordId, List<PressureSegment> segments) async {
    for (final seg in segments) {
      await _db.insert('pressure_segments', {
        'record_id': recordId,
        'start_time': seg.startTime.toIso8601String(),
        'end_time': seg.endTime.toIso8601String(),
        'level': 'medium',
        'avg_value': 0.0,
      });
    }
  }

  Future<void> _uploadAiAnalysis(int recordId, AiAnalysis analysis) async {
    await _db.insert('ai_analyses', {
      'record_id': recordId,
      'summary': analysis.summary,
      'suggestions': jsonEncode(analysis.suggestions),
      'created_at': analysis.analyzedAt.toIso8601String(),
    });
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

    return AiAnalysis(
      summary: r['summary'] as String? ?? '',
      insights: [],
      suggestions: suggestions,
      analyzedAt: r['created_at'] != null
          ? DateTime.parse(r['created_at'] as String)
          : DateTime.now(),
    );
  }
}
