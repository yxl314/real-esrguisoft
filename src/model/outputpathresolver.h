#ifndef OUTPUTPATHRESOLVER_H
#define OUTPUTPATHRESOLVER_H

// ============================================================================
// 模型层 - 输出路径解析
//
// 集中管理「输出文件 / 输出目录」的命名规则，与处理编排（ProcessService）
// 分离，便于独立测试与后续调整命名策略。
//
// 命名规则逐条继承旧 Python 编排层（bin/src/core.py:110-216）：
//   单文件：{输入 stem}[_{输入扩展名}]_{model}[_x].{输出扩展名}
//   批量：  {输入目录名}_{model}[_x]/
// 相对 Python 版本修复的隐式 bug：BMP / GIF 图片无法被引擎编码，统一转为 PNG。
// ============================================================================

#include <QDateTime>
#include <QHash>
#include <QString>

#include "mediasettings.h"

class OutputPathResolver
{
  public:
	// 单文件处理：计算最终输出文件路径。
	//   - userOutputPath 为空 → 输出到输入文件所在目录；
	//   - userOutputPath 已存在且是目录 / 以分隔符结尾 → 视为目录；
	//   - userOutputPath 带扩展名（不存在）→ 视为完整文件路径（另存为场景）；
	//   - 其余（不存在且无扩展名）→ 视为目录。
	static QString
	resolve_single_output(const QString &v_input_file,
						  const MediaSettings &v_settings,
						  const QString &v_user_output_path);

	// 批量处理：计算输出目录。
	//   - userOutputDir 为空 → 输入目录的父目录下；
	//   - userOutputDir 非空 → 视为父目录（与 Python 一致）。
	static QString
	resolve_batch_output_dir(const QString &v_input_dir,
							 const MediaSettings &v_settings,
							 const QString &v_user_output_dir);

	// ------------------------------------------------------------------
	// 批量图片产物校验（阶段二修复）：
	//   例：同一输出目录先跑原格式（png）再改 jpg 重跑，旧 png 残留在目录里。
	//   服务层不能再按「目录内图片总数 == 输入数」判定成功，而是逐输入校验
	//   「本次任务确实生成了对应产物」。
	// ------------------------------------------------------------------

	// 期望产物快照：路径 → (大小, 修改时间)
	struct OutputFileSnapshot
	{
		qint64 m_size = 0;
		QDateTime m_last_modified;
	};
	using OutputSnapshotMap = QHash<QString, OutputFileSnapshot>;

	// 批量图片：单个输入在输出目录下的期望产物文件名（按优先级）。
	//   - 显式输出格式 → 该格式扩展名（jpg 同时接受 jpeg 别名）；
	//   - 原格式 → 沿用输入扩展名（jpeg/jpg 互相兼容）；
	//   - bmp / gif 引擎无法编码 → png。
	// 第一个元素是逐文件模式实际写入时使用的规范文件名。
	static QStringList
	batch_output_file_names(const QString &v_input_file,
							const MediaSettings &v_settings);

	// 对期望产物路径拍快照（只记录已存在的文件），用于识别同名旧产物未更新。
	static OutputSnapshotMap
	snapshot_batch_outputs(const QStringList &v_input_files,
						   const MediaSettings &v_settings,
						   const QString &v_output_dir);

	// 返回仍未就绪的输入文件（空 = 本批产物全部就绪）：
	//   任一候选产物不存在 / 大小为 0 / 与快照相比大小与修改时间都未变化。
	static QStringList
	unresolved_batch_outputs(const QStringList &v_input_files,
							 const MediaSettings &v_settings,
							 const QString &v_output_dir,
							 const OutputSnapshotMap &v_before);
};

#endif // OUTPUTPATHRESOLVER_H
