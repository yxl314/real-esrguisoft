#ifndef APPSETTINGS_H
#define APPSETTINGS_H

// ============================================================================
// 公共层 - 应用级用户设置（QSettings 持久化）
//
// 存放「用户可改、且跨层都要读」的设置。目前只有超分引擎路径：
//   engine/path = 用户指定的引擎位置，可为 bin 目录、bin/realesrgan 目录或
//                 realesrgan-ncnn-vulkan.exe 文件；空 = 未指定（自动发现）。
//
// 只负责读写，不做路径合法性判断；解析 / 校验属于基础设施层的引擎定位逻辑
// （ExeCommand::engine_location），自动发现仍是缺省回退。
// 组织 / 应用名与 ThemeManager 一致，因此主题与引擎路径落在同一配置文件。
// ============================================================================

#include <QString>

namespace AppSettings
{

/// 超分引擎路径（QSettings engine/path）；未设置或空白时返回空串。
QString
engine_path();

/// 写入超分引擎路径；空白串表示清除（恢复自动发现）。
void
set_engine_path(const QString &v_path);

} // namespace AppSettings

#endif // APPSETTINGS_H
