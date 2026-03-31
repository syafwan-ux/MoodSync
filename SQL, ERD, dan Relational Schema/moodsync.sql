CREATE TABLE users
(
  username VARCHAR(100) NOT NULL,
  created_at TIMESTAMP NOT NULL,
  password VARCHAR(255) NOT NULL,
  email VARCHAR(255) NOT NULL,
  id INT NOT NULL AUTO_INCREMENT,
  profile_photo VARBINARY(255),
  PRIMARY KEY (id),
  UNIQUE (email)
);

CREATE TABLE isi_catatan
(
  id INT NOT NULL AUTO_INCREMENT,
  date DATE NOT NULL,
  mood_level INT NOT NULL,
  story TEXT NOT NULL,
  created_at TIMESTAMP NOT NULL,
  users_id INT NOT NULL,
  PRIMARY KEY (id),
  FOREIGN KEY (users_id) REFERENCES users(id),
  UNIQUE (date)
);

CREATE TABLE activity
(
  id INT NOT NULL AUTO_INCREMENT,
  name VARCHAR(100) NOT NULL,
  PRIMARY KEY (id)
);

CREATE TABLE user_settings
(
  weekly_report BOOLEAN DEFAULT 0,
  daily_reminder BOOLEAN DEFAULT 0,
  users_id INT NOT NULL,
  FOREIGN KEY (users_id) REFERENCES users(id)
);

CREATE TABLE mood_activity
(
  id INT NOT NULL AUTO_INCREMENT,
  activity_id INT NOT NULL,
  isicatatan_id INT NOT NULL,
  PRIMARY KEY (id),
  FOREIGN KEY (activity_id) REFERENCES activity(id),
  FOREIGN KEY (isicatatan_id) REFERENCES isi_catatan(id)
);