"""
MoodSync — Backend Python
=========================
Teknologi  : Python + WebUI (IPC/Data Binding) + SQLite3
Demonstrasi: Komunikasi antar bahasa (HTML/JS <-> Python) via webui.bind()

Cara kerja:
  1. Python mendaftarkan fungsi-fungsi ke WebUI via win.bind("nama", fungsi)
  2. HTML/JS memanggil webui.call("nama", arg1, arg2, ...) saat user klik tombol
  3. WebUI meneruskan panggilan tsb ke fungsi Python via WebSocket (IPC)
  4. Python memproses logika bisnis (CRUD SQLite), lalu mengembalikan hasil JSON
  5. JavaScript menerima hasilnya sebagai string JSON → diparse dan ditampilkan
"""

from webui import webui
import sqlite3
import json
import hashlib
import os
import sys
from datetime import datetime, date, timedelta

# ─────────────────────────────────────────────────────────────────────────────
# PATH — semua file relatif terhadap lokasi script ini
# ─────────────────────────────────────────────────────────────────────────────
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DB_PATH  = os.path.join(BASE_DIR, "moodsync.db")
APP_DIR  = os.path.join(BASE_DIR, "application")

# ─────────────────────────────────────────────────────────────────────────────
# SESSION — state user yang sedang login (in-memory, per-session)
# ─────────────────────────────────────────────────────────────────────────────
session = {
    "user_id":  None,
    "username": "",
    "email":    ""
}

# ─────────────────────────────────────────────────────────────────────────────
# DATABASE SETUP
# ─────────────────────────────────────────────────────────────────────────────
def get_db():
    """Buat koneksi SQLite dan kembalikan (conn, cursor)."""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    conn.execute("PRAGMA journal_mode = WAL")
    return conn


def db_init():
    """Inisialisasi semua tabel dan seed aktivitas default."""
    conn = get_db()
    c = conn.cursor()

    # ── TABEL USERS ──────────────────────────────────────────────────────────
    c.execute("""
        CREATE TABLE IF NOT EXISTS users (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            username   TEXT NOT NULL,
            email      TEXT NOT NULL UNIQUE,
            password   TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT (datetime('now','localtime'))
        )
    """)

    # ── TABEL ISI_CATATAN (catatan mood harian) ───────────────────────────────
    c.execute("""
        CREATE TABLE IF NOT EXISTS isi_catatan (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            date       TEXT NOT NULL,
            mood_level INTEGER NOT NULL,
            story      TEXT NOT NULL DEFAULT '',
            created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')),
            users_id   INTEGER NOT NULL,
            UNIQUE(date, users_id),
            FOREIGN KEY (users_id) REFERENCES users(id)
        )
    """)

    # ── TABEL ACTIVITY ───────────────────────────────────────────────────────
    c.execute("""
        CREATE TABLE IF NOT EXISTS activity (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL
        )
    """)

    # ── TABEL USER_SETTINGS ──────────────────────────────────────────────────
    c.execute("""
        CREATE TABLE IF NOT EXISTS user_settings (
            users_id       INTEGER NOT NULL UNIQUE,
            daily_reminder INTEGER NOT NULL DEFAULT 0,
            weekly_report  INTEGER NOT NULL DEFAULT 0,
            FOREIGN KEY (users_id) REFERENCES users(id)
        )
    """)

    # ── TABEL MOOD_ACTIVITY (relasi catatan <-> aktivitas) ───────────────────
    c.execute("""
        CREATE TABLE IF NOT EXISTS mood_activity (
            id             INTEGER PRIMARY KEY AUTOINCREMENT,
            activity_id    INTEGER NOT NULL,
            isicatatan_id  INTEGER NOT NULL,
            FOREIGN KEY (activity_id)   REFERENCES activity(id),
            FOREIGN KEY (isicatatan_id) REFERENCES isi_catatan(id)
        )
    """)

    # ── SEED AKTIVITAS ────────────────────────────────────────────────────────
    count = c.execute("SELECT COUNT(*) FROM activity").fetchone()[0]
    if count == 0:
        activities = [
            "Bekerja", "Sosial", "Tidur", "Olahraga",
            "Makanan", "Zen", "Kreativitas", "Hal lain"
        ]
        c.executemany("INSERT INTO activity(name) VALUES(?)", [(a,) for a in activities])
        print(f"[DB] {len(activities)} aktivitas default berhasil di-seed.")

    conn.commit()
    conn.close()
    print(f"[DB] Database siap: {DB_PATH}")


# ─────────────────────────────────────────────────────────────────────────────
# UTILITY
# ─────────────────────────────────────────────────────────────────────────────
def hash_password(password: str) -> str:
    """Hash password dengan SHA-256."""
    return hashlib.sha256(password.encode("utf-8")).hexdigest()


def ok(data: dict = None) -> str:
    """Return JSON sukses."""
    result = {"ok": True}
    if data:
        result.update(data)
    return json.dumps(result, ensure_ascii=False)


def err(message: str) -> str:
    """Return JSON error."""
    return json.dumps({"ok": False, "message": message}, ensure_ascii=False)


# ─────────────────────────────────────────────────────────────────────────────
# HANDLER: REGISTER
# JS call : webui.call("register", username, email, password)
# C++/Py  : validasi input, cek duplikat email, simpan ke SQLite
# Return  : JSON { "ok": bool, "message": str }
# ─────────────────────────────────────────────────────────────────────────────
def handle_register(e: webui.Event):
    username = e.get_string_at(0)
    email    = e.get_string_at(1)
    password = e.get_string_at(2)

    if not username or not email or len(password) < 8:
        e.return_string(err("Data tidak valid. Password minimal 8 karakter."))
        return

    conn = get_db()
    try:
        # Cek apakah email sudah terdaftar
        existing = conn.execute("SELECT id FROM users WHERE email = ?", (email,)).fetchone()
        if existing:
            e.return_string(err("Email sudah terdaftar."))
            return

        # Simpan user baru
        hashed = hash_password(password)
        now    = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        conn.execute(
            "INSERT INTO users(username, email, password, created_at) VALUES(?, ?, ?, ?)",
            (username, email, hashed, now)
        )
        new_id = conn.execute("SELECT last_insert_rowid()").fetchone()[0]

        # Buat default settings untuk user baru
        conn.execute("INSERT INTO user_settings(users_id) VALUES(?)", (new_id,))
        conn.commit()

        print(f"[AUTH] User baru terdaftar: {username} ({email})")
        e.return_string(ok({"message": "Akun berhasil dibuat! Silakan login."}))

    except Exception as ex:
        print(f"[ERROR] register: {ex}")
        e.return_string(err("Gagal membuat akun."))
    finally:
        conn.close()


# ─────────────────────────────────────────────────────────────────────────────
# HANDLER: LOGIN
# JS call : webui.call("login", email, password)
# Python  : verifikasi email+SHA256(password) di SQLite, set session
# Return  : JSON { "ok": bool, "user": {...} }
# ─────────────────────────────────────────────────────────────────────────────
def handle_login(e: webui.Event):
    email    = e.get_string_at(0)
    password = e.get_string_at(1)

    if not email or not password:
        e.return_string(err("Email dan password harus diisi."))
        return

    hashed = hash_password(password)
    conn   = get_db()
    try:
        row = conn.execute(
            "SELECT id, username, email FROM users WHERE email = ? AND password = ?",
            (email, hashed)
        ).fetchone()

        if row:
            session["user_id"]  = row["id"]
            session["username"] = row["username"]
            session["email"]    = row["email"]
            print(f"[AUTH] Login berhasil: {row['username']} (id={row['id']})")
            e.return_string(ok({
                "message": "Login berhasil!",
                "user": {
                    "id":       row["id"],
                    "username": row["username"],
                    "email":    row["email"]
                }
            }))
        else:
            e.return_string(err("Email atau password salah."))

    except Exception as ex:
        print(f"[ERROR] login: {ex}")
        e.return_string(err("Terjadi kesalahan saat login."))
    finally:
        conn.close()


# ─────────────────────────────────────────────────────────────────────────────
# HANDLER: LOGOUT
# JS call : webui.call("logout")
# ─────────────────────────────────────────────────────────────────────────────
def handle_logout(e: webui.Event):
    username = session["username"]
    session["user_id"]  = None
    session["username"] = ""
    session["email"]    = ""
    print(f"[AUTH] Logout: {username}")
    e.return_string(ok())


# ─────────────────────────────────────────────────────────────────────────────
# HANDLER: GET SESSION
# JS call : webui.call("get_session")
# ─────────────────────────────────────────────────────────────────────────────
def handle_get_session(e: webui.Event):
    if session["user_id"]:
        e.return_string(ok({
            "user": {
                "id":       session["user_id"],
                "username": session["username"],
                "email":    session["email"]
            }
        }))
    else:
        e.return_string(err("Belum login."))


# ─────────────────────────────────────────────────────────────────────────────
# HANDLER: SAVE MOOD
# JS call : webui.call("save_mood", mood_level, story, activities_json)
#           activities_json contoh: "[1,3,5]" (array ID aktivitas dari tabel activity)
# Python  : INSERT/UPDATE isi_catatan + hapus & insert ulang mood_activity
# Return  : JSON { "ok": bool, "message": str }
# ─────────────────────────────────────────────────────────────────────────────
def handle_save_mood(e: webui.Event):
    if not session["user_id"]:
        e.return_string(err("Silakan login terlebih dahulu."))
        return

    mood_level      = int(e.get_int_at(0))
    story           = e.get_string_at(1)
    activities_json = e.get_string_at(2)

    try:
        activity_ids = json.loads(activities_json) if activities_json else []
    except Exception:
        activity_ids = []

    today = date.today().strftime("%Y-%m-%d")
    now   = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    conn = get_db()
    try:
        # INSERT OR REPLACE catatan mood hari ini
        conn.execute("""
            INSERT INTO isi_catatan(date, mood_level, story, created_at, users_id)
            VALUES(?, ?, ?, ?, ?)
            ON CONFLICT(date, users_id) DO UPDATE SET
                mood_level = excluded.mood_level,
                story      = excluded.story,
                created_at = excluded.created_at
        """, (today, mood_level, story, now, session["user_id"]))

        # Ambil ID catatan hari ini
        row = conn.execute(
            "SELECT id FROM isi_catatan WHERE date = ? AND users_id = ?",
            (today, session["user_id"])
        ).fetchone()
        catatan_id = row["id"]

        # Hapus mood_activity lama, lalu insert ulang
        conn.execute("DELETE FROM mood_activity WHERE isicatatan_id = ?", (catatan_id,))
        for aid in activity_ids:
            conn.execute(
                "INSERT INTO mood_activity(activity_id, isicatatan_id) VALUES(?, ?)",
                (int(aid), catatan_id)
            )

        conn.commit()
        print(f"[MOOD] Catatan disimpan: user={session['user_id']}, mood={mood_level}, date={today}")
        e.return_string(ok({"message": "Catatan berhasil disimpan! 🎉"}))

    except Exception as ex:
        print(f"[ERROR] save_mood: {ex}")
        e.return_string(err("Gagal menyimpan catatan."))
    finally:
        conn.close()


# ─────────────────────────────────────────────────────────────────────────────
# HANDLER: GET MOOD TODAY
# JS call : webui.call("get_mood_today")
# ─────────────────────────────────────────────────────────────────────────────
def handle_get_mood_today(e: webui.Event):
    if not session["user_id"]:
        e.return_string(err("Belum login."))
        return

    today = date.today().strftime("%Y-%m-%d")
    conn  = get_db()
    try:
        row = conn.execute("""
            SELECT ic.id, ic.mood_level, ic.story, ic.created_at,
                   GROUP_CONCAT(a.name, ',') as activities
            FROM isi_catatan ic
            LEFT JOIN mood_activity ma ON ma.isicatatan_id = ic.id
            LEFT JOIN activity a       ON a.id = ma.activity_id
            WHERE ic.date = ? AND ic.users_id = ?
            GROUP BY ic.id
        """, (today, session["user_id"])).fetchone()

        if row:
            e.return_string(ok({
                "entry": {
                    "id":         row["id"],
                    "mood_level": row["mood_level"],
                    "story":      row["story"] or "",
                    "created_at": row["created_at"] or "",
                    "activities": row["activities"] or ""
                }
            }))
        else:
            e.return_string(err("Belum ada catatan hari ini."))

    except Exception as ex:
        print(f"[ERROR] get_mood_today: {ex}")
        e.return_string(err("Gagal mengambil data."))
    finally:
        conn.close()


# ─────────────────────────────────────────────────────────────────────────────
# HANDLER: GET HISTORY
# JS call : webui.call("get_history")
# Python  : JOIN isi_catatan + mood_activity + activity, GROUP BY tanggal
# ─────────────────────────────────────────────────────────────────────────────
def handle_get_history(e: webui.Event):
    if not session["user_id"]:
        e.return_string(err("Belum login."))
        return

    conn = get_db()
    try:
        rows = conn.execute("""
            SELECT ic.id, ic.date, ic.mood_level, ic.story, ic.created_at,
                   GROUP_CONCAT(a.name, ',') as activities
            FROM isi_catatan ic
            LEFT JOIN mood_activity ma ON ma.isicatatan_id = ic.id
            LEFT JOIN activity a       ON a.id = ma.activity_id
            WHERE ic.users_id = ?
            GROUP BY ic.id
            ORDER BY ic.date DESC
        """, (session["user_id"],)).fetchall()

        entries = []
        for row in rows:
            entries.append({
                "id":         row["id"],
                "date":       row["date"],
                "mood_level": row["mood_level"],
                "story":      row["story"] or "",
                "created_at": row["created_at"] or "",
                "activities": row["activities"] or ""
            })

        e.return_string(ok({"entries": entries}))

    except Exception as ex:
        print(f"[ERROR] get_history: {ex}")
        e.return_string(err("Gagal mengambil history."))
    finally:
        conn.close()


# ─────────────────────────────────────────────────────────────────────────────
# HANDLER: GET INSIGHTS
# JS call : webui.call("get_insights")
# Python  : Aggregasi data 7 hari, hitung streak, top aktivitas
# ─────────────────────────────────────────────────────────────────────────────
def handle_get_insights(e: webui.Event):
    if not session["user_id"]:
        e.return_string(err("Belum login."))
        return

    conn = get_db()
    try:
        # Mood 7 hari terakhir
        mood_rows = conn.execute("""
            SELECT date, mood_level FROM isi_catatan
            WHERE users_id = ? AND date >= date('now', '-6 days', 'localtime')
            ORDER BY date ASC
        """, (session["user_id"],)).fetchall()

        mood_data  = [{"date": r["date"], "mood_level": r["mood_level"]} for r in mood_rows]
        mood_sum   = sum(r["mood_level"] for r in mood_rows)
        mood_count = len(mood_rows)

        # Aktivitas terfavorit 7 hari terakhir
        act_rows = conn.execute("""
            SELECT a.name, COUNT(*) as freq
            FROM mood_activity ma
            JOIN activity a       ON a.id = ma.activity_id
            JOIN isi_catatan ic   ON ic.id = ma.isicatatan_id
            WHERE ic.users_id = ? AND ic.date >= date('now', '-6 days', 'localtime')
            GROUP BY a.id
            ORDER BY freq DESC
            LIMIT 5
        """, (session["user_id"],)).fetchall()
        top_activities = [{"name": r["name"], "count": r["freq"]} for r in act_rows]

        # Hitung streak (hari berturut-turut mencatat)
        all_dates = conn.execute(
            "SELECT date FROM isi_catatan WHERE users_id = ? ORDER BY date DESC",
            (session["user_id"],)
        ).fetchall()

        streak   = 0
        expected = date.today()
        for row in all_dates:
            d = datetime.strptime(row["date"], "%Y-%m-%d").date()
            if d == expected:
                streak  += 1
                expected = expected - timedelta(days=1)
            elif d < expected:
                break  # ada gap, streak berhenti

        avg_mood = round(mood_sum / mood_count, 2) if mood_count > 0 else 0

        e.return_string(ok({
            "mood_data":      mood_data,
            "top_activities": top_activities,
            "streak":         streak,
            "avg_mood":       avg_mood,
            "total_entries":  mood_count
        }))

    except Exception as ex:
        print(f"[ERROR] get_insights: {ex}")
        e.return_string(err("Gagal mengambil insights."))
    finally:
        conn.close()


# ─────────────────────────────────────────────────────────────────────────────
# HANDLER: GET SETTINGS
# JS call : webui.call("get_settings")
# ─────────────────────────────────────────────────────────────────────────────
def handle_get_settings(e: webui.Event):
    if not session["user_id"]:
        e.return_string(err("Belum login."))
        return

    conn = get_db()
    try:
        row = conn.execute(
            "SELECT daily_reminder, weekly_report FROM user_settings WHERE users_id = ?",
            (session["user_id"],)
        ).fetchone()

        settings = {
            "daily_reminder": bool(row["daily_reminder"]) if row else False,
            "weekly_report":  bool(row["weekly_report"])  if row else False
        }
        e.return_string(ok({
            "user": {
                "id":       session["user_id"],
                "username": session["username"],
                "email":    session["email"]
            },
            "settings": settings
        }))

    except Exception as ex:
        print(f"[ERROR] get_settings: {ex}")
        e.return_string(err("Gagal mengambil settings."))
    finally:
        conn.close()


# ─────────────────────────────────────────────────────────────────────────────
# HANDLER: SAVE SETTINGS
# JS call : webui.call("save_settings", daily_reminder, weekly_report)
# Python  : UPDATE user_settings di SQLite
# ─────────────────────────────────────────────────────────────────────────────
def handle_save_settings(e: webui.Event):
    if not session["user_id"]:
        e.return_string(err("Belum login."))
        return

    daily  = e.get_bool_at(0)
    weekly = e.get_bool_at(1)

    conn = get_db()
    try:
        conn.execute("""
            INSERT INTO user_settings(users_id, daily_reminder, weekly_report) VALUES(?, ?, ?)
            ON CONFLICT(users_id) DO UPDATE SET
                daily_reminder = excluded.daily_reminder,
                weekly_report  = excluded.weekly_report
        """, (session["user_id"], 1 if daily else 0, 1 if weekly else 0))
        conn.commit()
        e.return_string(ok({"message": "Pengaturan berhasil disimpan!"}))

    except Exception as ex:
        print(f"[ERROR] save_settings: {ex}")
        e.return_string(err("Gagal menyimpan pengaturan."))
    finally:
        conn.close()


# ─────────────────────────────────────────────────────────────────────────────
# HANDLER: DELETE ACCOUNT
# JS call : webui.call("delete_account")
# Python  : DELETE semua data user dari semua tabel, clear session
# ─────────────────────────────────────────────────────────────────────────────
def handle_delete_account(e: webui.Event):
    if not session["user_id"]:
        e.return_string(err("Belum login."))
        return

    uid  = session["user_id"]
    conn = get_db()
    try:
        conn.execute("PRAGMA foreign_keys = OFF")

        # Hapus mood_activity untuk semua catatan user ini
        conn.execute("""
            DELETE FROM mood_activity
            WHERE isicatatan_id IN (SELECT id FROM isi_catatan WHERE users_id = ?)
        """, (uid,))

        conn.execute("DELETE FROM isi_catatan WHERE users_id = ?", (uid,))
        conn.execute("DELETE FROM user_settings WHERE users_id = ?", (uid,))
        conn.execute("DELETE FROM users WHERE id = ?", (uid,))
        conn.execute("PRAGMA foreign_keys = ON")
        conn.commit()

        session["user_id"]  = None
        session["username"] = ""
        session["email"]    = ""

        print(f"[AUTH] Akun dihapus: user_id={uid}")
        e.return_string(ok({"message": "Akun berhasil dihapus."}))

    except Exception as ex:
        print(f"[ERROR] delete_account: {ex}")
        e.return_string(err("Gagal menghapus akun."))
    finally:
        conn.close()


# ─────────────────────────────────────────────────────────────────────────────
# HANDLER: GET ACTIVITIES
# JS call : webui.call("get_activities")
# Python  : SELECT semua aktivitas dari tabel activity
# ─────────────────────────────────────────────────────────────────────────────
def handle_get_activities(e: webui.Event):
    conn = get_db()
    try:
        rows = conn.execute("SELECT id, name FROM activity ORDER BY id").fetchall()
        activities = [{"id": r["id"], "name": r["name"]} for r in rows]
        e.return_string(ok({"activities": activities}))
    except Exception as ex:
        print(f"[ERROR] get_activities: {ex}")
        e.return_string(err("Gagal mengambil aktivitas."))
    finally:
        conn.close()


# ─────────────────────────────────────────────────────────────────────────────
# MAIN — Inisialisasi WebUI dan daftarkan semua binding
# ─────────────────────────────────────────────────────────────────────────────
def main():
    print("=" * 50)
    print("   MoodSync v0.1 — Starting")
    print("   Backend: Python + SQLite")
    print("   IPC    : WebUI Data Binding")
    print("=" * 50)

    # Inisialisasi database
    db_init()

    # Buat window WebUI
    win = webui.window()

    # Set root folder ke direktori aplikasi HTML/CSS/JS
    win.set_root_folder(APP_DIR)

    # ──────────────────────────────────────────────────────────────────────────
    # DATA BINDING — Hubungkan nama fungsi JS ke handler Python
    #
    # Setiap baris di sini adalah "bridge" antara frontend dan backend:
    #   win.bind("nama_js", fungsi_python)
    #
    # Ketika JS memanggil: await webui.call("login", email, pass)
    # WebUI meneruskan panggilan ke: handle_login(event)
    # ──────────────────────────────────────────────────────────────────────────
    win.bind("register",       handle_register)
    win.bind("login",          handle_login)
    win.bind("logout",         handle_logout)
    win.bind("get_session",    handle_get_session)
    win.bind("save_mood",      handle_save_mood)
    win.bind("get_mood_today", handle_get_mood_today)
    win.bind("get_history",    handle_get_history)
    win.bind("get_insights",   handle_get_insights)
    win.bind("get_settings",   handle_get_settings)
    win.bind("save_settings",  handle_save_settings)
    win.bind("delete_account", handle_delete_account)
    win.bind("get_activities", handle_get_activities)
    # ──────────────────────────────────────────────────────────────────────────

    # Tampilkan halaman login sebagai entry point
    win.show("login.html")

    # Tunggu sampai semua window ditutup (blocking)
    webui.wait()

    print("[MoodSync] Aplikasi ditutup. Sampai jumpa!")


if __name__ == "__main__":
    main()
