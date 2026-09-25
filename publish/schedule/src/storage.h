/* storage.h — 设置/事件持久化 与 导入导出 */
#ifndef STORAGE_H
#define STORAGE_H

#include "app.h"

void storage_init(void);                 /* 建目录 + 载入 settings/schedule */
void settings_save(void);
void events_save(void);

/* 导入导出（JSON 文件, 事件数组）。export 0 成功；import 返回导入条数, <0 失败 */
int  events_export(const wchar_t *path);
int  events_import(const wchar_t *path);

/* 开机自启（HKCU\...\Run） */
void autostart_set(BOOL on);

#endif
