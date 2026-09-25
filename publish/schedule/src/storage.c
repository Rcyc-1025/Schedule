/* storage.c — 设置/事件持久化、导入导出、开机自启 */
#include "app.h"
#include "storage.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================= 文件读写基础 ================= */

static wchar_t *path_of(const char *name, wchar_t *out, int cap)
{
    wchar_t wname[64];
    u8_to_w16(name, wname, 64);
    _snwprintf(out, cap, L"%s\\%s", g.dir, wname);
    out[cap - 1] = 0;
    return out;
}

static BOOL write_file_utf8(const wchar_t *path, const char *data, size_t len)
{
    wchar_t tmp[MAX_PATH + 8];
    _snwprintf(tmp, MAX_PATH + 7, L"%s.tmp", path);
    tmp[MAX_PATH + 7] = 0;
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD written = 0;
    BOOL ok = TRUE;
    while (len > 0) {
        DWORD chunk = len > 0x40000000 ? 0x40000000u : (DWORD)len;
        if (!WriteFile(h, data, chunk, &written, NULL) || written != chunk) { ok = FALSE; break; }
        data += chunk;
        len -= chunk;
    }
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp); return FALSE; }
    return MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
}

static char *read_file_utf8(const wchar_t *path, size_t *outLen)
{
    *outLen = 0;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 64 * 1024 * 1024) { CloseHandle(h); return NULL; }
    size_t len = (size_t)sz.QuadPart;
    char *buf = (char *)malloc(len + 1);
    if (!buf) { CloseHandle(h); return NULL; }
    DWORD rd = 0;
    if (!ReadFile(h, buf, (DWORD)len, &rd, NULL)) { CloseHandle(h); free(buf); return NULL; }
    CloseHandle(h);
    buf[rd] = 0;
    /* 跳过 UTF-8 BOM (EF BB BF) */
    if (rd >= 3 && (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        memmove(buf, buf + 3, rd - 3);
        rd -= 3;
        buf[rd] = 0;
    }
    *outLen = rd;
    return buf;
}

/* ================= 目录 ================= */

static void dir_ensure(void)
{
    PWSTR roaming = NULL;
    BOOL ok = FALSE;
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_RoamingAppData, 0, NULL, &roaming))) {
        _snwprintf(g.dir, MAX_PATH, L"%s\\ScheduleWidget", roaming);
        g.dir[MAX_PATH - 1] = 0;
        CoTaskMemFree(roaming);
        ok = CreateDirectoryW(g.dir, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
    }
    if (!ok) { /* 回退到 exe 目录（不修改全局 g.exePath） */
        wchar_t tmp[MAX_PATH];
        wcsncpy_s(tmp, MAX_PATH, g.exePath, _TRUNCATE);
        wchar_t *slash = wcsrchr(tmp, L'\\');
        if (slash) *slash = 0;
        _snwprintf(g.dir, MAX_PATH, L"%s", tmp);
        g.dir[MAX_PATH - 1] = 0;
    }
}

/* ================= 事件 JSON 映射 ================= */

static void event_to_json(const Event *ev, cJSON *arr)
{
    cJSON *o = cJSON_CreateObject();
    char *title = w16_to_u8_dup(ev->title);
    char *note = w16_to_u8_dup(ev->note);
    cJSON_AddStringToObject(o, "id", ev->id);
    cJSON_AddStringToObject(o, "title", title ? title : "");
    cJSON_AddStringToObject(o, "note", note ? note : "");
    cJSON_AddStringToObject(o, "start", ev->start);
    cJSON_AddStringToObject(o, "end", ev->end);
    cJSON_AddBoolToObject(o, "allDay", ev->allDay ? 1 : 0);
    cJSON_AddNumberToObject(o, "color", ev->colorIdx);
    cJSON_AddNumberToObject(o, "remind", ev->remindMinutes);
    cJSON_AddStringToObject(o, "rec", rec_to_str(ev->rec));
    cJSON_AddNumberToObject(o, "interval", ev->interval);
    cJSON_AddStringToObject(o, "unit", unit_to_str(ev->unit));
    cJSON_AddStringToObject(o, "until", ev->until);
    cJSON_AddNumberToObject(o, "lastNotified", (double)ev->lastNotified);
    cJSON_AddItemToArray(arr, o);
    free(title);
    free(note);
}

static void json_to_event(const cJSON *o, Event *ev, BOOL genNewId)
{
    memset(ev, 0, sizeof(*ev));
    ev->colorIdx = 0;
    ev->remindMinutes = -1;
    ev->rec = REC_NONE;
    ev->interval = 1;
    ev->unit = U_DAY;
    ev->lastNotified = 0;

    cJSON *j;
    if (genNewId) {
        gen_id(ev->id, sizeof(ev->id));
    } else {
        j = cJSON_GetObjectItemCaseSensitive(o, "id");
        if (cJSON_IsString(j) && j->valuestring && j->valuestring[0])
            strncpy(ev->id, j->valuestring, sizeof(ev->id) - 1);
        else
            gen_id(ev->id, sizeof(ev->id));
    }
    j = cJSON_GetObjectItemCaseSensitive(o, "title");
    if (cJSON_IsString(j) && j->valuestring) u8_to_w16(j->valuestring, ev->title, 128);
    j = cJSON_GetObjectItemCaseSensitive(o, "note");
    if (cJSON_IsString(j) && j->valuestring) u8_to_w16(j->valuestring, ev->note, 512);
    j = cJSON_GetObjectItemCaseSensitive(o, "start");
    if (cJSON_IsString(j) && j->valuestring) {
        strncpy(ev->start, j->valuestring, sizeof(ev->start) - 1);
        /* 统一为 T 分隔 */
        for (char *p = ev->start; *p; p++) if (*p == ' ') *p = 'T';
    }
    j = cJSON_GetObjectItemCaseSensitive(o, "end");
    if (cJSON_IsString(j) && j->valuestring) {
        strncpy(ev->end, j->valuestring, sizeof(ev->end) - 1);
        for (char *p = ev->end; *p; p++) if (*p == ' ') *p = 'T';
    }
    j = cJSON_GetObjectItemCaseSensitive(o, "allDay");
    if (cJSON_IsBool(j)) ev->allDay = cJSON_IsTrue(j) ? 1 : 0;
    /* 兼容无 allDay 字段但 start/end 为纯日期的旧数据 */
    if (!ev->allDay && ev->start[0] && strlen(ev->start) == 10)
        ev->allDay = 1;
    j = cJSON_GetObjectItemCaseSensitive(o, "color");
    if (cJSON_IsNumber(j)) { int v = (int)j->valuedouble; if (v >= 0 && v < PAL_COUNT) ev->colorIdx = v; }
    j = cJSON_GetObjectItemCaseSensitive(o, "remind");
    if (cJSON_IsNumber(j)) ev->remindMinutes = (int)j->valuedouble;
    j = cJSON_GetObjectItemCaseSensitive(o, "rec");
    if (cJSON_IsString(j) && j->valuestring) ev->rec = rec_from_str(j->valuestring);
    j = cJSON_GetObjectItemCaseSensitive(o, "interval");
    if (cJSON_IsNumber(j) && j->valuedouble >= 1) ev->interval = (int)j->valuedouble;
    j = cJSON_GetObjectItemCaseSensitive(o, "unit");
    if (cJSON_IsString(j) && j->valuestring) ev->unit = unit_from_str(j->valuestring, U_DAY);
    j = cJSON_GetObjectItemCaseSensitive(o, "until");
    if (cJSON_IsString(j) && j->valuestring) {
        strncpy(ev->until, j->valuestring, sizeof(ev->until) - 1);
        for (char *p = ev->until; *p; p++) if (*p == ' ') *p = 'T';
    }
    j = cJSON_GetObjectItemCaseSensitive(o, "lastNotified");
    if (cJSON_IsNumber(j)) ev->lastNotified = (long long)j->valuedouble;
}

void events_save(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "events");
    for (int i = 0; i < schedule_count(); i++)
        event_to_json(schedule_at(i), arr);
    char *txt = cJSON_PrintUnformatted(root);
    if (txt) {
        wchar_t path[MAX_PATH];
        write_file_utf8(path_of("schedule.json", path, MAX_PATH), txt, strlen(txt));
        free(txt);
    }
    cJSON_Delete(root);
}

static void events_load(void)
{
    wchar_t path[MAX_PATH];
    size_t len = 0;
    char *txt = read_file_utf8(path_of("schedule.json", path, MAX_PATH), &len);
    if (!txt) return;
    cJSON *root = cJSON_Parse(txt);
    if (root) {
        cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "events");
        cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            Event ev;
            json_to_event(it, &ev, FALSE);
            if (ev.start[0]) schedule_add_nosave(&ev);
        }
        cJSON_Delete(root);
    }
    free(txt);
}

/* ================= 设置 ================= */

static void settings_defaults(void)
{
    memset(&g.st, 0, sizeof(g.st));
    g.st.autostart = 0;
    g.st.defRemind = 10;
    g.st.posX = g.st.posY = -1;
    g.st.collapsed = 0;
}

void settings_save(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "autostart", g.st.autostart != 0);
    cJSON_AddNumberToObject(root, "defRemind", g.st.defRemind);
    cJSON_AddNumberToObject(root, "posX", g.st.posX);
    cJSON_AddNumberToObject(root, "posY", g.st.posY);
    cJSON_AddBoolToObject(root, "collapsed", g.st.collapsed != 0);
    char *txt = cJSON_PrintUnformatted(root);
    if (txt) {
        wchar_t path[MAX_PATH];
        write_file_utf8(path_of("settings.json", path, MAX_PATH), txt, strlen(txt));
        free(txt);
    }
    cJSON_Delete(root);
}

static void settings_load(void)
{
    settings_defaults();
    wchar_t path[MAX_PATH];
    size_t len = 0;
    char *txt = read_file_utf8(path_of("settings.json", path, MAX_PATH), &len);
    if (!txt) return;
    cJSON *root = cJSON_Parse(txt);
    if (root) {
        cJSON *j;
        /* 旧版本字段 provider/base/key/model/hideFullscreen/shareUrl 直接忽略 */
        j = cJSON_GetObjectItemCaseSensitive(root, "autostart");
        if (cJSON_IsBool(j)) g.st.autostart = cJSON_IsTrue(j) ? 1 : 0;
        j = cJSON_GetObjectItemCaseSensitive(root, "defRemind");
        if (cJSON_IsNumber(j)) g.st.defRemind = (int)j->valuedouble;
        j = cJSON_GetObjectItemCaseSensitive(root, "posX");
        if (cJSON_IsNumber(j)) g.st.posX = (int)j->valuedouble;
        j = cJSON_GetObjectItemCaseSensitive(root, "posY");
        if (cJSON_IsNumber(j)) g.st.posY = (int)j->valuedouble;
        j = cJSON_GetObjectItemCaseSensitive(root, "collapsed");
        if (cJSON_IsBool(j)) g.st.collapsed = cJSON_IsTrue(j) ? 1 : 0;
        cJSON_Delete(root);
    }
    free(txt);
}

/* ================= 导入导出 ================= */

int events_export(const wchar_t *path)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "events");
    for (int i = 0; i < schedule_count(); i++)
        event_to_json(schedule_at(i), arr);
    char *txt = cJSON_Print(root);   /* 带缩进，便于人工查看 */
    int ok = 0;
    if (txt) {
        ok = write_file_utf8(path, txt, strlen(txt)) ? 0 : -1;
        free(txt);
    } else {
        ok = -1;
    }
    cJSON_Delete(root);
    return ok;
}

int events_import(const wchar_t *path)
{
    size_t len = 0;
    char *txt = read_file_utf8(path, &len);
    if (!txt) return -1;
    cJSON *root = cJSON_Parse(txt);
    free(txt);
    if (!root) return -1;
    /* 支持 {"events":[...]} 或直接数组 */
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "events");
    if (!cJSON_IsArray(arr) && cJSON_IsArray(root)) arr = root;
    int n = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (!cJSON_IsObject(it)) continue;
        Event ev;
        json_to_event(it, &ev, TRUE);
        if (!ev.start[0] || iso_to_t64(ev.start) == 0) continue;
        if (!ev.end[0]) strcpy(ev.end, ev.start);
        schedule_add(&ev);
        n++;
    }
    cJSON_Delete(root);
    if (n > 0) events_save();
    return n;
}

/* ================= 开机自启 ================= */

static const wchar_t *RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t *RUN_VAL = L"ScheduleWidget";

void autostart_set(BOOL on)
{
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &h) != ERROR_SUCCESS)
        return;
    if (on) {
        wchar_t cmd[MAX_PATH * 2];
        _snwprintf(cmd, MAX_PATH * 2, L"\"%s\"", g.exePath);
        cmd[MAX_PATH * 2 - 1] = 0;
        RegSetValueExW(h, RUN_VAL, 0, REG_SZ, (const BYTE *)cmd,
                       (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(h, RUN_VAL);
    }
    RegCloseKey(h);
}

/* ================= 初始化 ================= */

void storage_init(void)
{
    GetModuleFileNameW(NULL, g.exePath, MAX_PATH);
    g.exePath[MAX_PATH - 1] = 0;
    dir_ensure();
    settings_load();
    events_load();
}
