import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import '../models/snore_result.dart';
import '../models/sleep_record.dart';

class SnoreApiService {
  static const String _baseUrl = '';

  Future<SnoreResult?> fetchSnoreResult({
    required String patientId,
    required DateTime date,
  }) async {
    if (_baseUrl.isEmpty) {
      return null;
    }

    try {
      final dateStr =
          '${date.year}-${date.month.toString().padLeft(2, '0')}-${date.day.toString().padLeft(2, '0')}';
      final url = Uri.parse('$_baseUrl/results/$patientId/$dateStr');

      final response = await http
          .get(url, headers: {'Accept': 'application/json'})
          .timeout(const Duration(seconds: 15));

      if (response.statusCode == 200) {
        final json = jsonDecode(response.body) as Map<String, dynamic>;
        return SnoreResult.fromJson(json);
      } else if (response.statusCode == 404) {
        debugPrint('未找到打鼾检测结果: $patientId / $dateStr');
        return null;
      } else {
        debugPrint('打鼾 API 错误: ${response.statusCode} ${response.body}');
        return null;
      }
    } catch (e) {
      debugPrint('打鼾 API 请求失败: $e');
      return null;
    }
  }

  List<SnoringEvent> convertToSnoringEvents(
    SnoreResult result,
    DateTime recordingStartTime,
  ) {
    return result.events.map((event) {
      final startTime = recordingStartTime.add(
        Duration(milliseconds: (event.eventStartSeconds * 1000).round()),
      );
      final endTime = recordingStartTime.add(
        Duration(milliseconds: (event.eventEndSeconds * 1000).round()),
      );

      final avgDecibel = 30.0 + event.meanProbability * 40.0;

      return SnoringEvent(
        startTime: startTime,
        endTime: endTime,
        avgDecibel: double.parse(avgDecibel.toStringAsFixed(1)),
      );
    }).toList();
  }
}
