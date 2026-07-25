import 'dart:convert';
import 'package:crypto/crypto.dart';
import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../models/user.dart';
import '../config/cloudbase_config.dart';
import 'cloudbase_db.dart';

class AuthService {
  static const _currentUserKey = 'auth_current_user';

  final CloudBaseDB _db = CloudBaseDB();
  User? _currentUser;
  late final SharedPreferences _prefs;

  User? get currentUser => _currentUser;
  bool get isLoggedIn => _currentUser != null;

  Future<void> init() async {
    _prefs = await SharedPreferences.getInstance();
    _loadCurrentUser();
  }

  void _loadCurrentUser() {
    final raw = _prefs.getString(_currentUserKey);
    if (raw != null) {
      _currentUser = User.fromJson(jsonDecode(raw));
    }
  }

  Future<void> _cacheCurrentUser(User? user) async {
    if (user != null) {
      await _prefs.setString(_currentUserKey, jsonEncode(user.toJson()));
    } else {
      await _prefs.remove(_currentUserKey);
    }
  }

  String _hashPassword(String password) {
    final bytes = utf8.encode(password);
    return sha256.convert(bytes).toString();
  }

  Future<User> login(String username, String password) async {
    if (!CloudBaseConfig.isConfigured) {
      throw Exception('云服务未配置');
    }

    final rows = await _db.query(
      'users',
      where: 'username=eq.$username',
      limit: 1,
    );

    if (rows.isEmpty) {
      throw Exception('用户名或密码错误');
    }

    final row = rows.first;
    final storedHash = row['password_hash'] as String?;
    if (storedHash == null || storedHash != _hashPassword(password)) {
      throw Exception('用户名或密码错误');
    }

    final user = _userFromRow(row);
    _currentUser = user;
    await _cacheCurrentUser(user);
    return user;
  }

  Future<User> register({
    required String username,
    required String password,
    String? email,
    String? phone,
  }) async {
    if (!CloudBaseConfig.isConfigured) {
      throw Exception('云服务未配置');
    }

    final existing = await _db.query(
      'users',
      where: 'username=eq.$username',
      limit: 1,
    );
    if (existing.isNotEmpty) {
      throw Exception('用户名已存在');
    }

    final userId = 'user_${DateTime.now().millisecondsSinceEpoch}';
    final now = DateTime.now().toIso8601String();

    final success = await _db.insert('users', {
      'user_id': userId,
      'username': username,
      'password_hash': _hashPassword(password),
      'email': email,
      'phone': phone,
      'nickname': username,
      'created_at': now,
    });

    if (!success) {
      throw Exception('注册失败，请稍后重试');
    }

    final user = User(
      id: userId,
      username: username,
      email: email,
      phone: phone,
      nickname: username,
    );
    _currentUser = user;
    await _cacheCurrentUser(user);
    return user;
  }

  Future<void> logout() async {
    _currentUser = null;
    await _cacheCurrentUser(null);
  }

  Future<User> updateProfile(User updatedUser) async {
    if (CloudBaseConfig.isConfigured) {
      try {
        await _db.update('users',
          where: 'user_id=eq.${updatedUser.id}',
          data: {
            'nickname': updatedUser.nickname,
            'email': updatedUser.email,
            'phone': updatedUser.phone,
            'gender': updatedUser.gender,
            'birth_date': updatedUser.birthDate?.toIso8601String(),
            'height': updatedUser.height,
            'weight': updatedUser.weight,
          },
        );
      } catch (e) {
        debugPrint('[AuthService] 云端更新 profile 失败: $e');
      }
    }
    _currentUser = updatedUser;
    await _cacheCurrentUser(updatedUser);
    return updatedUser;
  }

  User _userFromRow(Map<String, dynamic> row) {
    return User(
      id: row['user_id'] as String? ?? '',
      username: row['username'] as String? ?? '',
      email: row['email'] as String?,
      phone: row['phone'] as String?,
      nickname: row['nickname'] as String?,
      gender: row['gender'] as String?,
      birthDate: row['birth_date'] != null
          ? DateTime.tryParse(row['birth_date'] as String)
          : null,
      height: (row['height'] as num?)?.toDouble(),
      weight: (row['weight'] as num?)?.toDouble(),
      createdAt: row['created_at'] != null
          ? DateTime.tryParse(row['created_at'] as String)
          : null,
    );
  }
}
