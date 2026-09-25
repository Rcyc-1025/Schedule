/* text_extract.h — 从文件提取纯 UTF-8 文本（供日程文件导入窗口使用） */
#ifndef TEXT_EXTRACT_H
#define TEXT_EXTRACT_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 读取文件并提取为纯 UTF-8 文本。
 * 成功返回 1，*out 为 malloc 缓冲（含末尾 \0，调用方 free），*outLen 不含 \0。
 * 支持：.txt/.md/.csv 纯文本（自动识别 UTF-8 BOM / UTF-16 / GBK）
 *       .xlsx/.docx（自实现 ZIP 解包：stored + deflate，无 zlib 依赖）
 * 旧 OLE 二进制（.doc/.xls，魔数 D0CF11E0）返回 0，err 提示先另存为新格式。
 * 失败返回 0；err 非 NULL 且 errCap>0 时写入错误描述（UTF-16，截断安全）。 */
int extract_text_from_file(const wchar_t *path, char **out, long *outLen,
                           wchar_t *err, int errCap);

#ifdef __cplusplus
}
#endif

#endif
