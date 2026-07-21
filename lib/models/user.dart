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

  Map<String, dynamic> toJson() => {
        'id': id,
        'username': username,
        'email': email,
        'phone': phone,
        'nickname': nickname,
        'gender': gender,
        'birthDate': birthDate?.toIso8601String(),
        'height': height,
        'weight': weight,
        'createdAt': createdAt.toIso8601String(),
      };

  factory User.fromJson(Map<String, dynamic> json) => User(
        id: json['id'] as String,
        username: json['username'] as String,
        email: json['email'] as String?,
        phone: json['phone'] as String?,
        nickname: json['nickname'] as String?,
        gender: json['gender'] as String?,
        birthDate: json['birthDate'] != null
            ? DateTime.parse(json['birthDate'] as String)
            : null,
        height: (json['height'] as num?)?.toDouble(),
        weight: (json['weight'] as num?)?.toDouble(),
        createdAt: json['createdAt'] != null
            ? DateTime.parse(json['createdAt'] as String)
            : null,
      );

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
