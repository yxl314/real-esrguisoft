#ifndef EXECOMMAND_H
#define EXECOMMAND_H

// ============================================================================
// 基础设施层 - 超分引擎命令构造与定位
//
// 把「引擎布局 / 用户设置」翻译成 realesrgan-ncnn-vulkan 的启动命令：
//   - 定位 bin 目录（内含 realesrgan/realesrgan-ncnn-vulkan.exe 与 models/），
//     用户可在设置页手动指定（QSettings engine/path），未指定或无效时回退
//     自动发现（从程序目录向上找 bin）；
//   - 构造超分参数（与旧 Python 编排层完全一致：-i/-o/-n/-s/-f/-g/-x/-t，并显式
//     传 -m 锁定模型目录，避免依赖引擎的工作目录）
// 无内部状态与缓存：每次定位都按当前用户设置重新计算，可独立测试。
// ============================================================================

#include <QString>
#include <QStringList>

#include "mediasettings.h"

namespace ExeCommand
{

// 引擎位置的来源（设置页据此提示「自动发现 / 手动指定 / 指定无效」）
enum class EnginePathSource
{
	AUTO_DISCOVERED, // 未手动指定，使用自动发现
	MANUAL,			 // 用户手动指定且有效
	MANUAL_INVALID	 // 用户手动指定但无效，已回退自动发现
};

// 当前生效的引擎位置
struct EngineLocation
{
	QString m_bin_directory;   // 生效的 bin 目录（内含 realesrgan/）
	QString m_executable_path; // <bin>/realesrgan/realesrgan-ncnn-vulkan.exe
	EnginePathSource m_source = EnginePathSource::AUTO_DISCOVERED;
	QString m_configured_path; // QSettings engine/path 原始值（空 = 未指定）
};

// 把用户选择的路径解析为 bin 目录（可为 bin 目录、bin/realesrgan 目录或
// realesrgan-ncnn-vulkan.exe 文件）；无法识别 / 找不到引擎时返回空串。
QString
bin_directory_for_path(const QString &v_path);

// 当前生效的引擎位置：QSettings engine/path 优先，自动发现为回退。
EngineLocation
engine_location();

// 生效的 bin 目录（快捷方式，等价于 engine_location().m_bin_directory）。
// 服务层在任务启动时调用，保证设置页的修改在下一次任务生效。
QString
discover_bin_directory();

// 引擎可执行文件 / 模型目录的绝对路径（v_bin_dir 为 bin 目录）
QString
realesrgan_executable(const QString &v_bin_dir);
QString
models_directory(const QString &v_bin_dir);

// 构造 realesrgan 参数（不含可执行文件本身）。
//   v_model_file_name：模型文件名（realesrgan-x4plus 等）
//   v_scale：放大倍数；仅当 ≠4 时传 -s（4 倍为引擎默认，与 Python 一致）
//   v_output_format：输出格式（jpg/png/webp）；为空表示 auto（沿用输出路径扩展名）
//   v_tile_size：图块大小；「自动」/0/非法值 → 显式 safe_default_tile_size()
//                （256），显式正整数值 clamp 到 [safe_min_tile_size(),
//                safe_max_tile_size()]；**总是显式传 -t**，不交给引擎自动选择。
//   v_thread_count：线程配置 load:proc:save；空/非法回退
//                safe_default_thread_count()（1:1:2）；**总是显式传 -j**。
//   v_models_dir：模型目录；非空时追加 -m（显式锁定，避免引擎按 exe 目录解析）
//   v_safe_retry：引擎崩溃后的降级重试参数（-t 128 / -j 1:1:1），仅重试时
//                由服务层使用，不写回用户设置。
QStringList
upscale_arguments(const QString &v_input_path, const QString &v_output_path,
				  const QString &v_model_file_name, int v_scale,
				  const QString &v_output_format, GPUDevice v_gpu_device,
				  bool v_tta, const QString &v_tile_size,
				  const QString &v_thread_count = QString(),
				  const QString &v_models_dir = QString(),
				  bool v_safe_retry = false);

// 文件选择对话框的过滤串（供视图层 / 控制器使用）
QString
file_dialog_filter(MediaKind v_kind);

} // namespace ExeCommand

#endif // EXECOMMAND_H
