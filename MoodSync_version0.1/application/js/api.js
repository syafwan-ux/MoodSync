/**
 * MoodSync — JavaScript API Bridge
 * 
 * File ini menjadi jembatan antara HTML/JS (frontend) dan C++ (backend).
 * Setiap pemanggilan fungsi di sini akan dikirim ke backend C++ via
 * WebUI IPC (Inter-Process Communication) menggunakan WebSocket.
 * 
 * Cara kerja:
 *   JS: webui.call("nama_fungsi", arg1, arg2, ...)
 *   C++ menerima event, proses, dan return_string(json_result)
 *   JS mendapat respons berupa string JSON
 */

// ─── Konstanta Mood ──────────────────────────────────────────────────────────
const MOOD_MAP = {
    1: { label: "Biasa Saja", emoji: "😐", color: "#8B9DC3" },
    2: { label: "Sedih",      emoji: "😢", color: "#6B8EBD" },
    3: { label: "Bahagia",    emoji: "😊", color: "#7BC67E" },
    4: { label: "Senang",     emoji: "✨", color: "#F7C56C" },
    5: { label: "Marah",      emoji: "😡", color: "#E57373" },
    6: { label: "Mengantuk",  emoji: "😴", color: "#B39DDB" }
};

// ─── API Wrapper ─────────────────────────────────────────────────────────────
/**
 * Memanggil fungsi C++ backend dan mengembalikan objek JSON.
 * @param {string} funcName - nama fungsi yang di-bind di C++ (win.bind("funcName", ...))
 * @param {...any} args     - argumen yang dikirim ke C++
 * @returns {Promise<Object>} - hasil JSON dari C++
 */
async function api(funcName, ...args) {
    let retries = 20; // Coba hingga 2 detik (20 * 100ms)
    while (retries > 0) {
        try {
            if (typeof webui === "undefined" || !webui.call) {
                await new Promise(r => setTimeout(r, 100));
                retries--;
                continue;
            }
            const rawResult = await webui.call(funcName, ...args);
            return JSON.parse(rawResult);
        } catch (err) {
            // WebUI mungkin belum siap koneksi WebSocket-nya
            await new Promise(r => setTimeout(r, 100));
            retries--;
            if (retries === 0) {
                console.error(`[API Error] ${funcName}:`, err);
                return { ok: false, message: "Terjadi kesalahan koneksi dengan backend." };
            }
        }
    }
}

// ─── Auth Helpers ─────────────────────────────────────────────────────────────
const Auth = {
    async login(email, password) {
        return await api("login", email, password);
    },
    async register(username, email, password) {
        return await api("register", username, email, password);
    },
    async logout() {
        return await api("logout");
    },
    async getSession() {
        return await api("get_session");
    },
    /** Cek session dan redirect ke login jika tidak login */
    async requireAuth() {
        const res = await this.getSession();
        if (!res.ok) {
            window.location.href = "login.html";
            return null;
        }
        return res.user;
    }
};

// ─── Mood Helpers ─────────────────────────────────────────────────────────────
const Mood = {
    async saveMood(moodLevel, story, activityIds) {
        return await api("save_mood", moodLevel, story, JSON.stringify(activityIds));
    },
    async getMoodToday() {
        return await api("get_mood_today");
    },
    async getHistory() {
        return await api("get_history");
    },
    async getInsights() {
        return await api("get_insights");
    },
    async getActivities() {
        return await api("get_activities");
    }
};

// ─── Settings Helpers ────────────────────────────────────────────────────────
const Settings = {
    async get() {
        return await api("get_settings");
    },
    async save(dailyReminder, weeklyReport) {
        return await api("save_settings", dailyReminder, weeklyReport);
    },
    async deleteAccount() {
        return await api("delete_account");
    }
};

// ─── UI Utilities ─────────────────────────────────────────────────────────────
const UI = {
    showToast(message, type = "success") {
        const existing = document.getElementById("ms-toast");
        if (existing) existing.remove();

        const toast = document.createElement("div");
        toast.id = "ms-toast";
        toast.textContent = message;
        toast.style.cssText = `
            position: fixed; bottom: 90px; left: 50%; transform: translateX(-50%);
            background: ${type === "success" ? "#4CAF50" : "#f44336"};
            color: white; padding: 12px 24px; border-radius: 24px;
            font-family: 'Manrope', sans-serif; font-weight: 600; font-size: 14px;
            box-shadow: 0 4px 20px rgba(0,0,0,0.2); z-index: 9999;
            animation: slideUp 0.3s ease;
        `;
        document.body.appendChild(toast);
        setTimeout(() => {
            toast.style.opacity = "0";
            toast.style.transition = "opacity 0.3s";
            setTimeout(() => toast.remove(), 300);
        }, 3000);
    },

    showError(elementId, message) {
        let el = document.getElementById(elementId);
        if (!el) {
            el = document.createElement("p");
            el.id = elementId;
            el.style.cssText = "color:#e74c3c; font-size:13px; margin:4px 0; text-align:center;";
        }
        el.textContent = message;
        return el;
    },

    formatDate(dateStr) {
        if (!dateStr) return "";
        const d = new Date(dateStr);
        const months = ["JAN","FEB","MAR","APR","MEI","JUN",
                        "JUL","AGU","SEP","OKT","NOV","DES"];
        return `${d.getDate()} ${months[d.getMonth()]} ${d.getFullYear()}`;
    },

    formatTime(datetimeStr) {
        if (!datetimeStr) return "";
        const d = new Date(datetimeStr);
        let h = d.getHours(), m = d.getMinutes();
        const ampm = h >= 12 ? "PM" : "AM";
        h = h % 12 || 12;
        return `${h}:${String(m).padStart(2, "0")} ${ampm}`;
    },

    isToday(dateStr) {
        const today = new Date().toISOString().split("T")[0];
        return dateStr === today;
    },

    isYesterday(dateStr) {
        const d = new Date();
        d.setDate(d.getDate() - 1);
        return dateStr === d.toISOString().split("T")[0];
    }
};
