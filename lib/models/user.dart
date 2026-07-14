class User {
  final String id;
  final String username;
  final String? email;
  final String? phone;
  final String? nickname;
  final String? gender; // '男' or '女'
  final DateTime? birthDate;
  final double? height; // cm
  final double? weight; // kg
  final DateTime createdAt;

  User({
    required this.id,
    required this.username,
    this.email,
    this.phone,
    this.nickname,
    this.gender,
    this.birthDate,
    this.height,
    this.weight,
    DateTime? createdAt,
  }) : createdAt = createdAt ?? DateTime.now();

  User copyWith({
    String? nickname,
    String? email,
    String? phone,
    String? gender,
    DateTime? birthDate,
    double? height,
    double? weight,
  }) {
    return User(
      id: id,
      username: username,
      email: email ?? this.email,
      phone: phone ?? this.phone,
      nickname: nickname ?? this.nickname,
      gender: gender ?? this.gender,
      birthDate: birthDate ?? this.birthDate,
      height: height ?? this.height,
      weight: weight ?? this.weight,
      createdAt: createdAt,
    );
  }
}
