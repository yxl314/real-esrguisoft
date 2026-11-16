#ifndef SESSIONSTATE_H
#define SESSIONSTATE_H

// ============================================================================
// 控制器层 - 会话状态
//
// 当前「选择」的单一事实来源：输入路径、输出路径、处理模式。
// 视图只负责展示这些状态，控制器负责维护与校验。
// ============================================================================

#include <QString>

#include "mediasettings.h"

struct SessionState
{
	MediaKind m_kind = MediaKind::IMAGE; // 图片 / 视频模式
	bool m_batch_mode = false;			 // 是否批量处理
	QString m_input_path;				 // 输入文件或目录
	int m_input_file_count = 0;			 // 目录模式下可处理的文件数
	QString m_output_path;				 // 用户设置的输出路径（可为空）
};

#endif // SESSIONSTATE_H