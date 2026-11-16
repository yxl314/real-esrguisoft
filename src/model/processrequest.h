#ifndef PROCESSREQUEST_H
#define PROCESSREQUEST_H

// ============================================================================
// 模型层 - 处理任务请求
//
// 一次处理任务的完整输入：媒体种类、处理模式、输入路径、用户输出路径与设置。
// 供 ProcessService 与各原生处理服务（ImageProcessService /
// VideoProcessService）共享，避免头文件循环依赖。
// ============================================================================

#include <QString>

#include "mediasettings.h"

struct ProcessRequest
{
	MediaKind m_kind = MediaKind::IMAGE;		   // 媒体种类
	ProcessMode m_mode = ProcessMode::SINGLE_FILE; // 处理模式
	QString m_input_path;						   // 输入文件或目录（必须非空）
	QString m_output_path;	  // 用户选择的输出目录 / 文件（空则按命名规则推导）
	MediaSettings m_settings; // 用户设置（模型 / 倍数 / 格式 / 编码 ...）

	// 仅测试 / 调试使用：视频最多处理的帧数（0 = 全部）
	int m_max_video_frames = 0;

	// 临时目录根（空 = 系统临时目录；测试 / 超长路径规避时可指定到其他盘）
	QString m_temp_directory;
};

#endif // PROCESSREQUEST_H
