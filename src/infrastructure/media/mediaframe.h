#ifndef MEDIAFRAME_H
#define MEDIAFRAME_H

// ============================================================================
// 基础设施层 - 帧序列文件命名约定
//
// 拆帧（FrameDecoder）与合成（VideoEncoder）共用同一套帧文件命名规则，
// 等价旧 Python 编排里 ffmpeg 的 frame%08d.jpg 模板，只是把中间帧改为无损 PNG：
//   frame00000001.png, frame00000002.png, ...
// 序号从 1 开始、固定 8 位补零，因此按文件名字典序排列即帧顺序。
// ============================================================================

#include <QString>
#include <QStringList>

namespace MediaFrameSequence
{

// 生成单帧文件名（不含目录），如 ("frame", "png", 1) -> "frame00000001.png"
QString
frame_file_name(const QString &v_prefix, const QString &v_extension,
				int v_frame_number);

// 列出目录中的帧文件名（仅文件名，按帧序升序）；目录不存在返回空表
QStringList
list_frame_files(const QString &v_dir, const QString &v_prefix,
				 const QString &v_extension);

} // namespace MediaFrameSequence

#endif // MEDIAFRAME_H
