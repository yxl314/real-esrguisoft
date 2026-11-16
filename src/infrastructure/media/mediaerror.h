#ifndef MEDIAERROR_H
#define MEDIAERROR_H

// ============================================================================
// 基础设施层 - 媒体层统一错误类型
//
// 媒体基础设施（探测 / 拆帧 / 编码）由阻塞式同步函数组成，不允许 C++ 异常
// 穿过 Qt 事件循环边界：所有操作返回携带 MediaError 的结果结构，调用方按
// 错误码分支处理。错误信息统一使用中文文案，便于直接展示到界面。
// ============================================================================

#include <QString>

// 媒体层错误码
enum class MediaErrorCode
{
	NONE,			  // 无错误
	INVALID_ARGUMENT, // 参数非法（空路径、非法帧率、目录不存在等）
	FILE_NOT_FOUND,	  // 输入文件不存在
	OPEN_FAILED,	  // 无法打开输入文件 / 流
	STREAM_NOT_FOUND, // 文件里没有需要的流（如视频流）
	UNSUPPORTED,	  // 当前 FFmpeg 构建不支持（如缺少某个编码器）
	PROBE_FAILED,	  // 探测（解析流信息）失败
	DECODE_FAILED,	  // 解码失败
	ENCODE_FAILED,	  // 编码失败
	MUX_FAILED,		  // 封装写头部 / 写包 / 写尾部失败
	IO_FAILED,		  // 帧文件 / 输出目录读写失败
	CANCELLED,		  // 调用方主动取消
	INTERNAL		  // 内部错误（防御性分支）
};

struct MediaError
{
	MediaErrorCode m_code = MediaErrorCode::NONE;
	QString m_message;

	bool
	is_ok() const;

	void
	clear();

	// 构造一个失败错误；成功时使用默认构造（NONE）
	static MediaError
	make(MediaErrorCode v_code, const QString &v_message);
};

// 错误码 → 英文短名（日志 / 测试断言用，界面展示请用 m_message）
QString
media_error_code_to_string(MediaErrorCode v_code);

#endif // MEDIAERROR_H
