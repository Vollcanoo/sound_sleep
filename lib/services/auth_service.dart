import 'dart:convert';
import 'package:shared_preferences/shared_preferences.dart';
import '../models/user.dart';

class AuthService {
  static const _usersKey = 'auth_users';
  static const _currentUserKey = 'auth_current_user';

  final Map<String, _UserEntry> _users = {};
  User? _currentUser;
  late final SharedPreferences _prefs;

  User? get currentUser => _currentUser;
  bool get isLoggedIn => _currentUser != null;

  Future<void> init() async {
    _prefs = await SharedPreferences.getInstance();
    _loadUsers();
    _loadCurrentUser();
  }

  void _loadUsers() {
    final raw = _prefs.getString(_usersKey);
    if (raw != null) {
      final Map<String, dynamic> map = jsonDecode(raw);
      for (final entry in map.entries) {
        final data = entry.value as Map<String, dynamic>;
        _users[entry.key] = _UserEntry(
          password: data['password'] as String,
          user: User.fromJson(data['user'] as Map<String, dynamic>),
        );
      }
    }
  }

  void _loadCurrentUser() {
    final raw = _prefs.getString(_currentUserKey);
    if (raw != null) {
      _currentUser = User.fromJson(jsonDecode(raw));
    }
  }

  Future<void> _persist() async {
    final map = <String, dynamic>{};
    for (final entry in _users.entries) {
      map[entry.key] = {
        'password': entry.value.password,
        'user': entry.value.user.toJson(),
      };
    }
    await _prefs.setString(_usersKey, jsonEncode(map));
    if (_currentUser != null) {
      await _prefs.setString(_currentUserKey, jsonEncode(_currentUser!.toJson()));
    } else {
      await _prefs.remove(_currentUserKey);
    }
  }

  Future<User> login(String username, String password) async {
    final entry = _users[username];
    if (entry == null || entry.password != password) {
      throw Exception('用户名或密码错误');
    }
    _currentUser = entry.user;
    await _persist();
    return entry.user;
  }

  Future<User> register({
    required String username,
    required String password,
    String? email,
    String? phone,
  }) async {
    if (_users.containsKey(username)) {
      throw Exception('用户名已存在');
    }
    final user = User(
      id: 'user_${DateTime.now().millisecondsSinceEpoch}',
      username: username,
      email: email,
      phone: phone,
      nickname: username,
    );
    _users[username] = _UserEntry(password: password, user: user);
    _currentUser = user;
    await _persist();
    return user;
  }

  Future<void> logout() async {
    _currentUser = null;
    await _persist();
  }

  Future<User> updateProfile(User updatedUser) async {
    final entry = _users[updatedUser.username];
    if (entry != null) {
      _users[updatedUser.username] =
          _UserEntry(password: entry.password, user: updatedUser);
    }
    _currentUser = updatedUser;
    await _persist();
    return updatedUser;
  }
}

class _UserEntry {
  final String password;
  final User user;
  _UserEntry({required this.password, required this.user});
}
