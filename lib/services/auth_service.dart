import '../models/user.dart';

class AuthService {
  // In-memory user store: username -> {password, User}
  final Map<String, _UserEntry> _users = {};
  User? _currentUser;

  AuthService() {
    // Pre-load test account
    final testUser = User(
      id: 'user_001',
      username: 'test',
      nickname: '测试用户',
      email: 'test@example.com',
      phone: '13800138000',
      gender: '男',
      birthDate: DateTime(1995, 6, 15),
      height: 175,
      weight: 70,
    );
    _users['test'] = _UserEntry(password: '123456', user: testUser);
  }

  User? get currentUser => _currentUser;
  bool get isLoggedIn => _currentUser != null;

  Future<User> login(String username, String password) async {
    await Future.delayed(const Duration(milliseconds: 800)); // Simulate network
    final entry = _users[username];
    if (entry == null || entry.password != password) {
      throw Exception('用户名或密码错误');
    }
    _currentUser = entry.user;
    return entry.user;
  }

  Future<User> register({
    required String username,
    required String password,
    String? email,
    String? phone,
  }) async {
    await Future.delayed(const Duration(milliseconds: 800));
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
    return user;
  }

  Future<void> logout() async {
    await Future.delayed(const Duration(milliseconds: 300));
    _currentUser = null;
  }

  Future<User> updateProfile(User updatedUser) async {
    await Future.delayed(const Duration(milliseconds: 500));
    final entry = _users[updatedUser.username];
    if (entry != null) {
      _users[updatedUser.username] = _UserEntry(password: entry.password, user: updatedUser);
    }
    _currentUser = updatedUser;
    return updatedUser;
  }
}

class _UserEntry {
  final String password;
  final User user;
  _UserEntry({required this.password, required this.user});
}
