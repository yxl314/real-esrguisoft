#ifndef EXEOUTPUTPARSER_H
#define EXEOUTPUTPARSER_H

// ============================================================================
// 基础设施层 - 外部引擎输出解析
//
// realesrgan-ncnn-vulkan 的全部输出都走 stderr：
//   - 进度行：形如 "16.67%"（整行匹配，每处理完一个 tile 批次输出一行）
//   - GPU 信息行：以 '[' 开头（调用方直接忽略）
//   - 失败标记："decode image ... failed" / "encode image ... failed" /
//     "_wfopen ... failed"（模型文件缺失）
// 另外保留旧 Python 编排层的 "PROGRESS:xx" 协议解析能力，便于回归与调试。
// 纯静态函数、无状态、可独立测试。
// ============================================================================

#include <QString>
#include <QStringList>

#include <optional>

class ExeOutputParser
{
  public:
	// 一次解析的结果：progress / file 均为「可能缺失」
	struct Result
	{
		std::optional<int> m_progress; // 进度百分比 0-100
		std::optional<QString> m_file; // 当前处理文件（引擎通常不输出）
		QStringList m_failures;		   // 识别到的失败标记原文（空 = 未发现）

		bool
		has_failure() const
		{
			return !m_failures.isEmpty();
		}
	};

	// 解析单行输出（ExeRunner 的行缓冲逐行调用）
	static Result
	parse_line(const QString &v_line);

	// 解析整段输出（多行聚合：保留最后一个进度 / 文件名，汇总失败标记）
	static Result
	parse(const QString &v_output);
};

#endif // EXEOUTPUTPARSER_H
