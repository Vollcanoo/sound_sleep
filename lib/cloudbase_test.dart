import 'dart:convert';
import 'package:http/http.dart' as http;

const String envId = 'sleepmonitor-d0g3037pfb475a0f9';
const String accessToken =
    'eyJhbGciOiJSUzI1NiIsImtpZCI6IjlkMWRjMzFlLWI0ZDAtNDQ4Yi1hNzZmLWIwY2M2M2Q4MTQ5OCJ9.eyJpc3MiOiJodHRwczovL3NsZWVwbW9uaXRvci1kMGczMDM3cGZiNDc1YTBmOS5hcC1zaGFuZ2hhaS50Y2ItYXBpLnRlbmNlbnRjbG91ZGFwaS5jb20iLCJzdWIiOiJhbm9uIiwiYXVkIjoic2xlZXBtb25pdG9yLWQwZzMwMzdwZmI0NzVhMGY5IiwiZXhwIjo0MDg4MTQyOTcxLCJpYXQiOjE3ODQ0NTk3NzEsIm5vbmNlIjoiUXprRGFTTzNRYnlQWWo1b2s4XzczUSIsImF0X2hhc2giOiJRemtEYVNPM1FieVBZajVvazhfNzNRIiwibmFtZSI6IkFub255bW91cyIsInNjb3BlIjoiYW5vbnltb3VzIiwicHJvamVjdF9pZCI6InNsZWVwbW9uaXRvci1kMGczMDM3cGZiNDc1YTBmOSIsIm1ldGEiOnsicGxhdGZvcm0iOiJQdWJsaXNoYWJsZUtleSJ9LCJ1c2VyX3R5cGUiOiIiLCJjbGllbnRfdHlwZSI6ImNsaWVudF91c2VyIiwiaXNfc3lzdGVtX2FkbWluIjpmYWxzZX0.SgsiLubj64j9hg90CZpBaelCHsNjTJ-0FeAU3iUlE64i9FKvdMnnmSmffnL6hW7RDsnMNKXd-qFPE_vKqTyMKDgQkuOY1riPl1q3ck5660Bhkua_KpCxApjQh6SMvDFdGbAI6CIROzFA_aqCBrgYndC1O_jZQViZ_qoc6jYOF-KojhdHxb7iw3TM6XWmN-FunUFu101kQu9kE5d-OHDSuL2PGFveb-qB1kHBQ3psWVHNHQwKutCyHJLHNfclIJjxOjcSPgI3JPSk8fcpYcmPcchLwBZV2BP_oF0pTmzd3JVLb2ULSP4DgFyjNvIn95jI3bj_l51zcXcPJTqyaKSaIw';

final String baseUrl = 'https://$envId.api.tcloudbasegateway.com';

final Map<String, String> headers = {
  'Content-Type': 'application/json',
  'Accept': 'application/json',
  'Authorization': 'Bearer $accessToken',
};

Future<void> main() async {
  print('=== CloudBase SQL 数据库连通性测试 v2 ===\n');
  print('Base URL: $baseUrl\n');

  // 测试1: 插入（带 _openid）
  print('--- 测试1: 插入测试数据（带 _openid） ---');
  await httpPost('/v1/rdb/rest/sleep_records', {
    '_openid': 'anon',
    'date': '2026-07-19',
    'bed_time': '2026-07-18T23:30:00+08:00',
    'wake_time': '2026-07-19T07:15:00+08:00',
    'duration_minutes': 465,
    'sleep_score': 85,
    'get_up_count': 1,
    'device_id': 'TEST_DEVICE_001',
    'created_at': DateTime.now().toIso8601String(),
  });

  // 测试3: 查询
  print('\n--- 测试3: 查询确认 ---');
  await httpGet('/v1/rdb/rest/sleep_records?limit=5');

  print('\n=== 测试完成 ===');
}

Future<void> httpGet(String path) async {
  final url = Uri.parse('$baseUrl$path');
  print('GET $path');
  try {
    final resp = await http.get(url, headers: headers);
    print('  状态码: ${resp.statusCode}');
    print('  响应: ${resp.body}');
  } catch (e) {
    print('  异常: $e');
  }
}

Future<void> httpPost(String path, Map<String, dynamic> body) async {
  final url = Uri.parse('$baseUrl$path');
  print('POST $path');
  print('  Body: ${jsonEncode(body)}');
  try {
    final resp = await http.post(url, headers: headers, body: jsonEncode(body));
    print('  状态码: ${resp.statusCode}');
    print('  响应: ${resp.body}');
  } catch (e) {
    print('  异常: $e');
  }
}
