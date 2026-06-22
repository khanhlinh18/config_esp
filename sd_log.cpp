#include "sd_log.h"
#include <time.h>

// ================== TIMESTAMP ==================
String getTimestamp() {
    struct tm t;
    if (getLocalTime(&t)) {
        char buf[24];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
        return String(buf);
    }
    if (rtc_ok) {
        DateTime now = rtc.now();
        char buf[24];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                 now.year(), now.month(), now.day(),
                 now.hour(), now.minute(), now.second());
        return String(buf);
    }
    return "????-??-?? ??:??:??";
}

String getDateStr() {
    struct tm t;
    if (getLocalTime(&t)) {
        char buf[12];
        strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
        return String(buf);
    }
    if (rtc_ok) {
        DateTime now = rtc.now();
        char buf[12];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d", now.year(), now.month(), now.day());
        return String(buf);
    }
    return "0000-00-00";
}

// ================== SD LOG ==================
void sdLog(const char* level, const char* tag, const char* msg) {
    if (!sd_ok) return;
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    xSemaphoreGive(sdMutex);
    ensureSDSpace();
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    String filename = "/log_" + getDateStr() + ".txt";
    File f = SD.open(filename, FILE_APPEND);
    if (f) { f.printf("%s [%s] [%s] %s\n", getTimestamp().c_str(), level, tag, msg); f.close(); }
    xSemaphoreGive(sdMutex);
}

void sdLog(const char* level, const char* tag, const String& msg) {
    sdLog(level, tag, msg.c_str());
}

// ================== SERIAL + SD LOGGER ==================
void _logPrint(const char* tag, const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.printf("[%s] %s\n", tag, buf);
    const char* level = "INFO";
    String lower = String(buf); lower.toLowerCase();
    if (lower.indexOf("fail") >= 0 || lower.indexOf("lost") >= 0 ||
        lower.indexOf("error") >= 0 || lower.indexOf("thất bại") >= 0 ||
        lower.indexOf("disconnect") >= 0 || lower.indexOf("timeout") >= 0)
        level = "WARN";
    sdLog(level, tag, buf);
}

// ================== SD SPACE MANAGEMENT ==================
String deleteOldestLog() {
    String todayFile = "log_" + getDateStr() + ".txt";
    File root = SD.open("/");
    if (!root) return "";
    String oldestName = "";
    File entry = root.openNextFile();
    while (entry) {
        String fullName = String(entry.name());
        int slashIdx = fullName.lastIndexOf('/');
        String name = (slashIdx >= 0) ? fullName.substring(slashIdx + 1) : fullName;
        entry.close();
        if (name.startsWith("log_") && name.endsWith(".txt") && name.length() == 18 && name != todayFile) {
            if (oldestName == "" || name < oldestName) oldestName = name;
        }
        entry = root.openNextFile();
    }
    root.close();
    if (oldestName != "") { SD.remove("/" + oldestName); return oldestName; }
    return "";
}

void ensureSDSpace() {
    if (!sd_ok) return;
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) != pdTRUE) return;
    uint64_t freeBytes = SD.totalBytes() - SD.usedBytes();
    while (freeBytes < SD_MIN_FREE_BYTES) {
        String deleted = deleteOldestLog();
        if (deleted == "") { Serial.println("[SD] Không còn log cũ để xóa, SD gần đầy!"); break; }
        String auditFile = "/log_" + getDateStr() + ".txt";
        File f = SD.open(auditFile, FILE_APPEND);
        if (f) {
            f.printf("%s [WARN] [SD] Xóa log cũ do hết dung lượng: %s (free: %lluKB)\n",
                     getTimestamp().c_str(), deleted.c_str(), freeBytes / 1024);
            f.close();
        }
        Serial.printf("[SD] Xóa log cũ: %s (free: %lluKB)\n", deleted.c_str(), freeBytes / 1024);
        freeBytes = SD.totalBytes() - SD.usedBytes();
    }
    xSemaphoreGive(sdMutex);
}
