/* samples.h — 生成四种格式的真实示范文件（ics/csv/xlsx/json） */
#ifndef SAMPLES_H
#define SAMPLES_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 在 dir 下生成 4 个示范文件（目录必须已存在）。全部成功返回 1。 */
int samples_write_all(const wchar_t *dir);

/* 在 g.dir\samples 生成示范文件并用资源管理器打开该文件夹。
 * 成功返回 1；失败返回 0。 */
int samples_open_dir(void);

#ifdef __cplusplus
}
#endif

#endif
