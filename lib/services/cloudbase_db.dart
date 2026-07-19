import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import '../config/cloudbase_config.dart';

/// CloudBase SQL 数据库 HTTP 客户端
///
/// 通过 /v1/rdb/rest/ RESTful API 操作 MySQL 数据库
class CloudBaseDB {
  final Map<String, String> _headers = {
    'Content-Type': 'application/json',
    'Accept': 'application/json',
    'Authorization': 'Bearer ${CloudBaseConfig.accessToken}',
  };

  String _tablePath(String table) =>
      '${CloudBaseConfig.baseUrl}/v1/rdb/rest/$table';

  /// 插入一条记录，返回是否成功
  Future<bool> insert(String table, Map<String, dynamic> data) async {
    // CloudBase 行级安全策略要求 _openid 字段
    final body = Map<String, dynamic>.from(data);
    body['_openid'] = 'anon';

    try {
      final resp = await http.post(
        Uri.parse(_tablePath(table)),
        headers: _headers,
        body: jsonEncode(body),
      );
      if (resp.statusCode >= 200 && resp.statusCode < 300) {
        return true;
      }
      debugPrint('[CloudBaseDB] 插入失败 $table: ${resp.statusCode} ${resp.body}');
      return false;
    } catch (e) {
      debugPrint('[CloudBaseDB] 插入异常 $table: $e');
      return false;
    }
  }

  /// 查询记录列表
  ///
  /// [where] 过滤条件，格式如 "record_id=eq.5"
  /// [orderBy] 排序，如 "date.desc"
  /// [limit] 返回数量限制
  Future<List<Map<String, dynamic>>> query(
    String table, {
    String? where,
    String? orderBy,
    int limit = 100,
  }) async {
    final params = <String>['limit=$limit'];
    if (where != null) params.add(where);
    if (orderBy != null) params.add('order=$orderBy');

    final url = '${_tablePath(table)}?${params.join('&')}';

    try {
      final resp = await http.get(Uri.parse(url), headers: _headers);
      if (resp.statusCode >= 200 && resp.statusCode < 300) {
        if (resp.body.isEmpty) return [];
        final decoded = jsonDecode(resp.body);
        if (decoded is List) {
          return decoded.cast<Map<String, dynamic>>();
        }
        return [];
      }
      debugPrint('[CloudBaseDB] 查询失败 $table: ${resp.statusCode} ${resp.body}');
      return [];
    } catch (e) {
      debugPrint('[CloudBaseDB] 查询异常 $table: $e');
      return [];
    }
  }

  /// 删除记录
  ///
  /// [where] 过滤条件，如 "id=eq.5"
  Future<bool> delete(String table, {required String where}) async {
    final url = '${_tablePath(table)}?$where';
    try {
      final resp = await http.delete(Uri.parse(url), headers: _headers);
      if (resp.statusCode >= 200 && resp.statusCode < 300) {
        return true;
      }
      debugPrint('[CloudBaseDB] 删除失败 $table: ${resp.statusCode} ${resp.body}');
      return false;
    } catch (e) {
      debugPrint('[CloudBaseDB] 删除异常 $table: $e');
      return false;
    }
  }
}
