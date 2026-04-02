import sqlite3, os, hashlib
from datetime import datetime, date

DB_PATH = 'test_moodsync.db'
conn = sqlite3.connect(DB_PATH)
conn.row_factory = sqlite3.Row

conn.execute("""CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL,
    email TEXT NOT NULL UNIQUE,
    password TEXT NOT NULL,
    created_at TEXT NOT NULL)""")

conn.execute("""CREATE TABLE IF NOT EXISTS isi_catatan (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    date TEXT NOT NULL,
    mood_level INTEGER NOT NULL,
    story TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL,
    users_id INTEGER NOT NULL,
    UNIQUE(date, users_id))""")

conn.execute("""CREATE TABLE IF NOT EXISTS activity (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL)""")

conn.execute("""CREATE TABLE IF NOT EXISTS user_settings (
    users_id INTEGER NOT NULL UNIQUE,
    daily_reminder INTEGER NOT NULL DEFAULT 0,
    weekly_report INTEGER NOT NULL DEFAULT 0)""")

conn.execute("""CREATE TABLE IF NOT EXISTS mood_activity (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    activity_id INTEGER NOT NULL,
    isicatatan_id INTEGER NOT NULL)""")

acts = ['Bekerja','Sosial','Tidur','Olahraga','Makanan','Zen','Kreativitas','Hal lain']
for a in acts:
    conn.execute('INSERT OR IGNORE INTO activity(name) VALUES(?)', (a,))

pw_hash = hashlib.sha256('password123'.encode()).hexdigest()
conn.execute('INSERT OR IGNORE INTO users(username,email,password,created_at) VALUES(?,?,?,?)',
             ('TestUser','test@moodsync.com',pw_hash,datetime.now().strftime('%Y-%m-%d %H:%M:%S')))
conn.execute('INSERT OR IGNORE INTO user_settings(users_id) VALUES((SELECT id FROM users WHERE email="test@moodsync.com"))')
conn.commit()

row = conn.execute('SELECT id,username FROM users WHERE email=? AND password=?',
                   ('test@moodsync.com',pw_hash)).fetchone()
print(f'LOGIN OK: id={row["id"]}, username={row["username"]}')

today = date.today().strftime('%Y-%m-%d')
now   = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
conn.execute('INSERT OR REPLACE INTO isi_catatan(date,mood_level,story,created_at,users_id) VALUES(?,?,?,?,?)',
             (today, 3, 'Test story hari ini', now, row['id']))
cid = conn.execute('SELECT id FROM isi_catatan WHERE date=? AND users_id=?',(today,row['id'])).fetchone()['id']
conn.execute('INSERT OR IGNORE INTO mood_activity(activity_id,isicatatan_id) VALUES(?,?)',(1,cid))
conn.commit()
print(f'SAVE MOOD OK: catatan_id={cid}')

hist = conn.execute("""SELECT ic.date, ic.mood_level, ic.story,
    GROUP_CONCAT(a.name, ',') as activities
    FROM isi_catatan ic
    LEFT JOIN mood_activity ma ON ma.isicatatan_id=ic.id
    LEFT JOIN activity a ON a.id=ma.activity_id
    WHERE ic.users_id=?
    GROUP BY ic.id ORDER BY ic.date DESC""", (row['id'],)).fetchall()
print(f'HISTORY OK: {len(hist)} entries')
for h in hist:
    print(f'  date={h["date"]}, mood={h["mood_level"]}, story={h["story"]}, activities={h["activities"]}')

settings_row = conn.execute('SELECT daily_reminder, weekly_report FROM user_settings WHERE users_id=?',(row['id'],)).fetchone()
print(f'SETTINGS OK: daily={settings_row["daily_reminder"]}, weekly={settings_row["weekly_report"]}')

conn.close()
os.remove(DB_PATH)
print('\nALL TESTS PASSED OK!')
