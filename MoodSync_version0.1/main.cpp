/*
  MoodSync — Backend Utama
  Teknologi: C++ + WebUI (IPC/Data Binding) + SQLite3
  Demonstrasi: Komunikasi antar bahasa (HTML/JS <-> C++) via WebUI bind()
*/

#include "webui.hpp"
#include "include/sqlite3.h"
#include "include/nlohmann/json.hpp"

#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <functional>
#include <openssl/sha.h>  // optional fallback below

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
//  GLOBAL STATE
// ─────────────────────────────────────────────────────────────────────────────

static sqlite3* g_db = nullptr;
static webui::window* g_win = nullptr;

// Session sederhana — simpan user_id yang sedang login di memory
static int  g_session_user_id = 0;
static std::string g_session_username;
static std::string g_session_email;

// ─────────────────────────────────────────────────────────────────────────────
//  HASH PASSWORD (simple FNV-1a 64-bit, tidak perlu OpenSSL)
// ─────────────────────────────────────────────────────────────────────────────
static std::string hash_password(const std::string& password) {
    uint64_t hash = 14695981039346656037ULL;
    for (char c : password) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
//  DATABASE HELPER
// ─────────────────────────────────────────────────────────────────────────────
static bool db_exec(const std::string& sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(g_db, sql.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "[DB ERROR] " << sql.substr(0, 60) << " => " << errmsg << "\n";
        sqlite3_free(errmsg);
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  DB INIT — buat tabel dan seed aktivitas default
// ─────────────────────────────────────────────────────────────────────────────
static void db_init() {
    // Buka / buat file database
    int rc = sqlite3_open("moodsync.db", &g_db);
    if (rc != SQLITE_OK) {
        std::cerr << "[FATAL] Tidak bisa membuka database: " << sqlite3_errmsg(g_db) << "\n";
        return;
    }
    sqlite3_exec(g_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    // ── TABEL USERS ──────────────────────────────────────────────────────────
    db_exec(R"(
        CREATE TABLE IF NOT EXISTS users (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            username   TEXT NOT NULL,
            email      TEXT NOT NULL UNIQUE,
            password   TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT (datetime('now'))
        );
    )");

    // ── TABEL ISI_CATATAN (mood harian) ──────────────────────────────────────
    db_exec(R"(
        CREATE TABLE IF NOT EXISTS isi_catatan (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            date       TEXT NOT NULL,
            mood_level INTEGER NOT NULL,
            story      TEXT NOT NULL DEFAULT '',
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            users_id   INTEGER NOT NULL,
            FOREIGN KEY (users_id) REFERENCES users(id),
            UNIQUE(date, users_id)
        );
    )");

    // ── TABEL ACTIVITY ───────────────────────────────────────────────────────
    db_exec(R"(
        CREATE TABLE IF NOT EXISTS activity (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL
        );
    )");

    // ── TABEL USER_SETTINGS ──────────────────────────────────────────────────
    db_exec(R"(
        CREATE TABLE IF NOT EXISTS user_settings (
            users_id       INTEGER NOT NULL UNIQUE,
            daily_reminder INTEGER NOT NULL DEFAULT 0,
            weekly_report  INTEGER NOT NULL DEFAULT 0,
            FOREIGN KEY (users_id) REFERENCES users(id)
        );
    )");

    // ── TABEL MOOD_ACTIVITY (relasi catatan <-> aktivitas) ───────────────────
    db_exec(R"(
        CREATE TABLE IF NOT EXISTS mood_activity (
            id             INTEGER PRIMARY KEY AUTOINCREMENT,
            activity_id    INTEGER NOT NULL,
            isicatatan_id  INTEGER NOT NULL,
            FOREIGN KEY (activity_id)   REFERENCES activity(id),
            FOREIGN KEY (isicatatan_id) REFERENCES isi_catatan(id)
        );
    )");

    // ── SEED AKTIVITAS (hanya jika tabel kosong) ─────────────────────────────
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db, "SELECT COUNT(*) FROM activity;", -1, &stmt, nullptr);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (count == 0) {
        const char* activities[] = {
            "Bekerja", "Sosial", "Tidur", "Olahraga",
            "Makanan", "Zen", "Kreativitas", "Hal lain"
        };
        for (const char* a : activities) {
            std::string sql = "INSERT INTO activity(name) VALUES('" + std::string(a) + "');";
            db_exec(sql);
        }
        std::cout << "[DB] Aktivitas default berhasil di-seed.\n";
    }

    std::cout << "[DB] Database siap: moodsync.db\n";
}

// ─────────────────────────────────────────────────────────────────────────────
//  HELPER — tanggal hari ini (YYYY-MM-DD)
// ─────────────────────────────────────────────────────────────────────────────
static std::string today_date() {
    time_t t = time(nullptr);
    struct tm* tm_info = localtime(&t);
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", tm_info);
    return std::string(buf);
}

static std::string current_datetime() {
    time_t t = time(nullptr);
    struct tm* tm_info = localtime(&t);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    return std::string(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: REGISTER
//  JS call: webui.call("register", username, email, password)
//  Return : JSON { "ok": true/false, "message": "..." }
// ─────────────────────────────────────────────────────────────────────────────
void handle_register(webui::window::event* e) {
    std::string username = e->get_string(0);
    std::string email    = e->get_string(1);
    std::string password = e->get_string(2);

    json res;

    if (username.empty() || email.empty() || password.length() < 8) {
        res = { {"ok", false}, {"message", "Data tidak valid. Password minimal 8 karakter."} };
        e->return_string(res.dump());
        return;
    }

    // Cek apakah email sudah terdaftar
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db, "SELECT id FROM users WHERE email = ?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_STATIC);
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);

    if (exists) {
        res = { {"ok", false}, {"message", "Email sudah terdaftar."} };
        e->return_string(res.dump());
        return;
    }

    // Simpan user baru
    std::string hashed = hash_password(password);
    std::string now    = current_datetime();

    sqlite3_prepare_v2(g_db,
        "INSERT INTO users(username, email, password, created_at) VALUES(?, ?, ?, ?);",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, email.c_str(),    -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, hashed.c_str(),   -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, now.c_str(),      -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        res = { {"ok", false}, {"message", "Gagal membuat akun."} };
    } else {
        int new_id = (int)sqlite3_last_insert_rowid(g_db);
        // Buat default settings
        sqlite3_prepare_v2(g_db,
            "INSERT INTO user_settings(users_id) VALUES(?);", -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, new_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        res = { {"ok", true}, {"message", "Akun berhasil dibuat! Silakan login."} };
    }

    e->return_string(res.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: LOGIN
//  JS call: webui.call("login", email, password)
//  Return : JSON { "ok": true/false, "message": "...", "user": {...} }
// ─────────────────────────────────────────────────────────────────────────────
void handle_login(webui::window::event* e) {
    std::string email    = e->get_string(0);
    std::string password = e->get_string(1);

    json res;

    if (email.empty() || password.empty()) {
        res = { {"ok", false}, {"message", "Email dan password harus diisi."} };
        e->return_string(res.dump());
        return;
    }

    std::string hashed = hash_password(password);

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db,
        "SELECT id, username, email FROM users WHERE email = ? AND password = ?;",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, email.c_str(),  -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hashed.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        g_session_user_id  = sqlite3_column_int(stmt, 0);
        g_session_username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        g_session_email    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        sqlite3_finalize(stmt);

        res = {
            {"ok", true},
            {"message", "Login berhasil!"},
            {"user", {
                {"id",       g_session_user_id},
                {"username", g_session_username},
                {"email",    g_session_email}
            }}
        };
    } else {
        sqlite3_finalize(stmt);
        res = { {"ok", false}, {"message", "Email atau password salah."} };
    }

    e->return_string(res.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: LOGOUT
//  JS call: webui.call("logout")
// ─────────────────────────────────────────────────────────────────────────────
void handle_logout(webui::window::event* e) {
    g_session_user_id  = 0;
    g_session_username = "";
    g_session_email    = "";
    json res = { {"ok", true} };
    e->return_string(res.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: GET SESSION (cek siapa yang login)
//  JS call: webui.call("get_session")
// ─────────────────────────────────────────────────────────────────────────────
void handle_get_session(webui::window::event* e) {
    json res;
    if (g_session_user_id > 0) {
        res = {
            {"ok", true},
            {"user", {
                {"id",       g_session_user_id},
                {"username", g_session_username},
                {"email",    g_session_email}
            }}
        };
    } else {
        res = { {"ok", false} };
    }
    e->return_string(res.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: SAVE MOOD
//  JS call: webui.call("save_mood", mood_level, story, activities_json)
//  activities_json contoh: "[1,3,5]" (array ID aktivitas)
// ─────────────────────────────────────────────────────────────────────────────
void handle_save_mood(webui::window::event* e) {
    json res;

    if (g_session_user_id == 0) {
        res = { {"ok", false}, {"message", "Silakan login terlebih dahulu."} };
        e->return_string(res.dump());
        return;
    }

    int         mood_level       = (int)e->get_int(0);
    std::string story            = e->get_string(1);
    std::string activities_json  = e->get_string(2);

    // Parse activities array
    json activity_ids = json::array();
    try {
        activity_ids = json::parse(activities_json);
    } catch (...) {}

    std::string date = today_date();
    std::string now  = current_datetime();

    // Insert atau replace catatan hari ini
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db,
        "INSERT INTO isi_catatan(date, mood_level, story, created_at, users_id) "
        "VALUES(?, ?, ?, ?, ?) "
        "ON CONFLICT(date, users_id) DO UPDATE SET mood_level=excluded.mood_level, story=excluded.story, created_at=excluded.created_at;",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 2, mood_level);
    sqlite3_bind_text(stmt, 3, story.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, now.c_str(),  -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 5, g_session_user_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        res = { {"ok", false}, {"message", "Gagal menyimpan catatan."} };
        e->return_string(res.dump());
        return;
    }

    // Ambil ID catatan yang baru disimpan
    sqlite3_prepare_v2(g_db,
        "SELECT id FROM isi_catatan WHERE date = ? AND users_id = ?;",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 2, g_session_user_id);

    int catatan_id = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) catatan_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    // Hapus mood_activity lama untuk catatan ini, lalu insert ulang
    sqlite3_prepare_v2(g_db, "DELETE FROM mood_activity WHERE isicatatan_id = ?;", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, catatan_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    for (auto& aid : activity_ids) {
        int activity_id = aid.get<int>();
        sqlite3_prepare_v2(g_db,
            "INSERT INTO mood_activity(activity_id, isicatatan_id) VALUES(?, ?);",
            -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, activity_id);
        sqlite3_bind_int(stmt, 2, catatan_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    res = { {"ok", true}, {"message", "Catatan berhasil disimpan!"} };
    e->return_string(res.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: GET MOOD TODAY
//  JS call: webui.call("get_mood_today")
//  Return : JSON { "ok": true/false, "entry": {...} }
// ─────────────────────────────────────────────────────────────────────────────
void handle_get_mood_today(webui::window::event* e) {
    json res;

    if (g_session_user_id == 0) {
        res = { {"ok", false}, {"message", "Belum login."} };
        e->return_string(res.dump());
        return;
    }

    std::string date = today_date();
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db,
        "SELECT ic.id, ic.mood_level, ic.story, ic.created_at, "
        "GROUP_CONCAT(a.name, ',') as activities "
        "FROM isi_catatan ic "
        "LEFT JOIN mood_activity ma ON ma.isicatatan_id = ic.id "
        "LEFT JOIN activity a ON a.id = ma.activity_id "
        "WHERE ic.date = ? AND ic.users_id = ? "
        "GROUP BY ic.id;",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 2, g_session_user_id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int id         = sqlite3_column_int(stmt, 0);
        int mood_level = sqlite3_column_int(stmt, 1);
        const char* story_raw     = (const char*)sqlite3_column_text(stmt, 2);
        const char* created_raw   = (const char*)sqlite3_column_text(stmt, 3);
        const char* activities_raw= (const char*)sqlite3_column_text(stmt, 4);

        res = {
            {"ok", true},
            {"entry", {
                {"id",         id},
                {"mood_level", mood_level},
                {"story",      story_raw     ? story_raw     : ""},
                {"created_at", created_raw   ? created_raw   : ""},
                {"activities", activities_raw ? activities_raw : ""}
            }}
        };
    } else {
        res = { {"ok", false}, {"message", "Belum ada catatan hari ini."} };
    }
    sqlite3_finalize(stmt);
    e->return_string(res.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: GET HISTORY
//  JS call: webui.call("get_history")
//  Return : JSON { "ok": true, "entries": [...] }
// ─────────────────────────────────────────────────────────────────────────────
void handle_get_history(webui::window::event* e) {
    json res;

    if (g_session_user_id == 0) {
        res = { {"ok", false}, {"message", "Belum login."} };
        e->return_string(res.dump());
        return;
    }

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db,
        "SELECT ic.id, ic.date, ic.mood_level, ic.story, ic.created_at, "
        "GROUP_CONCAT(a.name, ',') as activities "
        "FROM isi_catatan ic "
        "LEFT JOIN mood_activity ma ON ma.isicatatan_id = ic.id "
        "LEFT JOIN activity a ON a.id = ma.activity_id "
        "WHERE ic.users_id = ? "
        "GROUP BY ic.id "
        "ORDER BY ic.date DESC;",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, g_session_user_id);

    json entries = json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id         = sqlite3_column_int(stmt, 0);
        const char* date_raw      = (const char*)sqlite3_column_text(stmt, 1);
        int mood_level = sqlite3_column_int(stmt, 2);
        const char* story_raw     = (const char*)sqlite3_column_text(stmt, 3);
        const char* created_raw   = (const char*)sqlite3_column_text(stmt, 4);
        const char* activities_raw= (const char*)sqlite3_column_text(stmt, 5);

        entries.push_back({
            {"id",         id},
            {"date",       date_raw        ? date_raw        : ""},
            {"mood_level", mood_level},
            {"story",      story_raw       ? story_raw       : ""},
            {"created_at", created_raw     ? created_raw     : ""},
            {"activities", activities_raw  ? activities_raw  : ""}
        });
    }
    sqlite3_finalize(stmt);

    res = { {"ok", true}, {"entries", entries} };
    e->return_string(res.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: GET INSIGHTS
//  JS call: webui.call("get_insights")
//  Return : JSON berisi statistik 7 hari terakhir
// ─────────────────────────────────────────────────────────────────────────────
void handle_get_insights(webui::window::event* e) {
    json res;

    if (g_session_user_id == 0) {
        res = { {"ok", false}, {"message", "Belum login."} };
        e->return_string(res.dump());
        return;
    }

    // 7 hari terakhir — mood per hari
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db,
        "SELECT date, mood_level FROM isi_catatan "
        "WHERE users_id = ? AND date >= date('now', '-6 days') "
        "ORDER BY date ASC;",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, g_session_user_id);

    json mood_data = json::array();
    double mood_sum = 0;
    int mood_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* d = (const char*)sqlite3_column_text(stmt, 0);
        int ml = sqlite3_column_int(stmt, 1);
        mood_data.push_back({ {"date", d ? d : ""}, {"mood_level", ml} });
        mood_sum += ml;
        mood_count++;
    }
    sqlite3_finalize(stmt);

    // Aktivitas yang paling sering
    sqlite3_prepare_v2(g_db,
        "SELECT a.name, COUNT(*) as freq FROM mood_activity ma "
        "JOIN activity a ON a.id = ma.activity_id "
        "JOIN isi_catatan ic ON ic.id = ma.isicatatan_id "
        "WHERE ic.users_id = ? AND ic.date >= date('now', '-6 days') "
        "GROUP BY a.id ORDER BY freq DESC LIMIT 5;",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, g_session_user_id);

    json top_activities = json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = (const char*)sqlite3_column_text(stmt, 0);
        int freq = sqlite3_column_int(stmt, 1);
        top_activities.push_back({ {"name", name ? name : ""}, {"count", freq} });
    }
    sqlite3_finalize(stmt);

    // Hitung streak (berturut-turut hari mencatat)
    sqlite3_prepare_v2(g_db,
        "SELECT date FROM isi_catatan WHERE users_id = ? ORDER BY date DESC;",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, g_session_user_id);

    int streak = 0;
    std::string expected = today_date();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* d_raw = (const char*)sqlite3_column_text(stmt, 0);
        std::string d = d_raw ? d_raw : "";
        if (d == expected) {
            streak++;
            // Kurangi expected satu hari
            time_t t = time(nullptr);
            // Hitung mundur dari today
            struct tm* tm_info = localtime(&t);
            tm_info->tm_mday -= streak;
            mktime(tm_info);
            char buf[11];
            strftime(buf, sizeof(buf), "%Y-%m-%d", tm_info);
            expected = std::string(buf);
        } else {
            break;
        }
    }
    sqlite3_finalize(stmt);

    double avg_mood = (mood_count > 0) ? (mood_sum / mood_count) : 0.0;

    res = {
        {"ok", true},
        {"mood_data",      mood_data},
        {"top_activities", top_activities},
        {"streak",         streak},
        {"avg_mood",       avg_mood},
        {"total_entries",  mood_count}
    };
    e->return_string(res.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: GET SETTINGS
//  JS call: webui.call("get_settings")
// ─────────────────────────────────────────────────────────────────────────────
void handle_get_settings(webui::window::event* e) {
    json res;

    if (g_session_user_id == 0) {
        res = { {"ok", false} };
        e->return_string(res.dump());
        return;
    }

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db,
        "SELECT daily_reminder, weekly_report FROM user_settings WHERE users_id = ?;",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, g_session_user_id);

    bool daily = false, weekly = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        daily  = sqlite3_column_int(stmt, 0) != 0;
        weekly = sqlite3_column_int(stmt, 1) != 0;
    }
    sqlite3_finalize(stmt);

    res = {
        {"ok", true},
        {"user", {
            {"id",       g_session_user_id},
            {"username", g_session_username},
            {"email",    g_session_email}
        }},
        {"settings", {
            {"daily_reminder", daily},
            {"weekly_report",  weekly}
        }}
    };
    e->return_string(res.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: SAVE SETTINGS
//  JS call: webui.call("save_settings", daily_reminder, weekly_report)
// ─────────────────────────────────────────────────────────────────────────────
void handle_save_settings(webui::window::event* e) {
    json res;

    if (g_session_user_id == 0) {
        res = { {"ok", false} };
        e->return_string(res.dump());
        return;
    }

    bool daily  = e->get_bool(0);
    bool weekly = e->get_bool(1);

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db,
        "INSERT INTO user_settings(users_id, daily_reminder, weekly_report) VALUES(?, ?, ?) "
        "ON CONFLICT(users_id) DO UPDATE SET daily_reminder=excluded.daily_reminder, weekly_report=excluded.weekly_report;",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, g_session_user_id);
    sqlite3_bind_int(stmt, 2, daily  ? 1 : 0);
    sqlite3_bind_int(stmt, 3, weekly ? 1 : 0);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    res = { {"ok", rc == SQLITE_DONE}, {"message", rc == SQLITE_DONE ? "Pengaturan disimpan!" : "Gagal menyimpan."} };
    e->return_string(res.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: DELETE ACCOUNT
//  JS call: webui.call("delete_account")
// ─────────────────────────────────────────────────────────────────────────────
void handle_delete_account(webui::window::event* e) {
    json res;

    if (g_session_user_id == 0) {
        res = { {"ok", false} };
        e->return_string(res.dump());
        return;
    }

    // Hapus semua data terkait user ini berurutan (karena foreign key)
    auto run_delete = [&](const std::string& sql) {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(g_db, sql.c_str(), -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, g_session_user_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    };

    // Hapus mood_activity dulu
    db_exec("PRAGMA foreign_keys=OFF;");
    run_delete("DELETE FROM mood_activity WHERE isicatatan_id IN (SELECT id FROM isi_catatan WHERE users_id = ?);");
    run_delete("DELETE FROM isi_catatan WHERE users_id = ?;");
    run_delete("DELETE FROM user_settings WHERE users_id = ?;");
    run_delete("DELETE FROM users WHERE id = ?;");
    db_exec("PRAGMA foreign_keys=ON;");

    g_session_user_id  = 0;
    g_session_username = "";
    g_session_email    = "";

    res = { {"ok", true}, {"message", "Akun berhasil dihapus."} };
    e->return_string(res.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: GET ACTIVITIES LIST
//  JS call: webui.call("get_activities")
// ─────────────────────────────────────────────────────────────────────────────
void handle_get_activities(webui::window::event* e) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db, "SELECT id, name FROM activity ORDER BY id;", -1, &stmt, nullptr);

    json activities = json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* name = (const char*)sqlite3_column_text(stmt, 1);
        activities.push_back({ {"id", id}, {"name", name ? name : ""} });
    }
    sqlite3_finalize(stmt);

    json res = { {"ok", true}, {"activities", activities} };
    e->return_string(res.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
//  MAIN
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "=== MoodSync v0.1 Starting ===\n";

    // Inisialisasi database
    db_init();

    // Buat window WebUI
    webui::window win;
    g_win = &win;

    // Set root folder ke direktori aplikasi
    win.set_root_folder("application");

    // ── IPC DATA BINDING ─────────────────────────────────────────────────────
    // Setiap bind() menghubungkan nama fungsi JS ke handler C++
    win.bind("register",        handle_register);
    win.bind("login",           handle_login);
    win.bind("logout",          handle_logout);
    win.bind("get_session",     handle_get_session);
    win.bind("save_mood",       handle_save_mood);
    win.bind("get_mood_today",  handle_get_mood_today);
    win.bind("get_history",     handle_get_history);
    win.bind("get_insights",    handle_get_insights);
    win.bind("get_settings",    handle_get_settings);
    win.bind("save_settings",   handle_save_settings);
    win.bind("delete_account",  handle_delete_account);
    win.bind("get_activities",  handle_get_activities);
    // ─────────────────────────────────────────────────────────────────────────

    // Tampilkan halaman login saat startup
    win.show("login.html");

    // Tunggu sampai semua window ditutup
    webui::wait();

    // Cleanup
    if (g_db) sqlite3_close(g_db);
    webui::clean();

    std::cout << "=== MoodSync Closed ===\n";
    return 0;
}
