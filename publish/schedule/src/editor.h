/* editor.h — 事件编辑对话框 与 模态对话框公共基础（W_DLG 类） */
#ifndef EDITOR_H
#define EDITOR_H

#include <windows.h>
#include "schedule.h"

/* 模态对话框基础（editor / settings / 导入预览 共用） */
HWND dlg_base_create(HWND owner, const wchar_t *title, int baseW, int baseH);
void dlg_run(HWND dlg, HWND owner);                     /* 手动模态循环 */
HWND dlg_ctrl(HWND parent, const wchar_t *cls, const wchar_t *text,
              DWORD style, DWORD exStyle, int id, int x, int y, int w, int h);
                                                 /* 96 基准坐标，内部按 DPI 缩放 */
void dlg_check_set(HWND h, int checked);   /* OD_CHECK 复选框状态 */
int  dlg_check_get(HWND h);                /* 读取 OD_CHECK 复选框状态 */
HFONT dlg_font(HWND w);                                 /* 标准 UI 字体（9pt YaHei） */
int  dlg_dpi(HWND w);                                   /* 当前对话框 DPI */

/* 事件编辑器：ev=NULL 新建。返回 1 表示发生了保存/删除 */
int editor_open(HWND owner, const Event *ev);

#endif
