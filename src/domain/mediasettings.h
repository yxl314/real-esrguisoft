#ifndef MEDIASETTINGS_H
#define MEDIASETTINGS_H

// ============================================================================
// 领域层 - 媒体处理设置模型
//
// 本文件定义了应用的核心领域类型（纯数据 + 枚举），不依赖任何 Qt 界面类，
// 也不依赖任何业务服务。上层（模型 / 视图 / 控制器）均可引用本层，但本层
// 不得反向依赖任何上层。
// ============================================================================

#include <QList>
#include <QString>
#include <QStringList>

// 媒体种类：决定整个处理管线走图片分支还是视频分支
enum class MediaKind
{
	IMAGE, // 图片
	VIDEO  // 视频
};

// 图片类型枚举
enum class ImageType
{
	REAL_IMAGE, // 真实图片
	ANIME_IMAGE // 动漫图片
};

// 模型类型枚举
enum class ModelType
{
	REAL_ESRGAN_X4PLUS,		  // realesrgan-x4plus
	REAL_ESRGAN_X4PLUS_ANIME, // realesrgan-x4plus-anime
	REAL_ESR_ANIMEVIDEOV3	  // realesr-animevideov3
};

// 放大倍数枚举
enum class ScaleFactor
{
	SCALE2X, // 2倍
	SCALE3X, // 3倍
	SCALE4X	 // 4倍
};

// 处理模式枚举
enum class ProcessMode
{
	SINGLE_FILE,  // 单文件处理
	BATCH_PROCESS // 批量处理
};

// 视频编码格式枚举
enum class VideoEncoding
{
	H264, // H.264
	H265, // H.265
	VP9,  // VP9
	AV1	  // AV1
};

// 视频编码质量枚举
enum class VideoQuality
{
	FAST,	  // 快速
	BALANCED, // 均衡
	QUALITY	  // 质量
};

// 输出格式枚举
enum class OutputFormat
{
	ORIGINAL, // 原格式
	JPG,	  // jpg
	PNG,	  // png
	WEB_P,	  // webp
	MP4,	  // mp4
	MKV,	  // mkv
	AVI,	  // avi
	MOV,	  // mov
	WEB_M,	  // webm
	GIF		  // gif
};

// GPU设备枚举
enum class GPUDevice
{
	AUTO, // 自动
	GPU0, // GPU 0
	GPU1, // GPU 1
	GPU2  // GPU 2
};

// 基础设置（图片类型 / 模型 / 放大倍数）
struct BasicSettings
{
	ImageType m_image_type = ImageType::REAL_IMAGE;
	ModelType m_model_type = ModelType::REAL_ESRGAN_X4PLUS;
	ScaleFactor m_scale_factor = ScaleFactor::SCALE4X;
};

// 高级参数（输出格式 / GPU / 图块 / TTA / 线程数）
struct AdvancedSettings
{
	OutputFormat m_output_format = OutputFormat::ORIGINAL;
	GPUDevice m_gpu_device = GPUDevice::AUTO;
	// 图块大小「自动」= 显式安全默认 256（见 safe_default_tile_size()）：
	// 不省略 -t 交给引擎自动选择，避免 12GB 级显存下自动图块过大导致单次
	// GPU dispatch 时间不可控（TDR 风险，task-34 加固）。
	QString m_tile_size = QStringLiteral("自动");
	bool m_tta_mode = false;
	// 默认线程 1:1:2（单推理线程，降低并发 Vulkan 压力；见
	// safe_default_thread_count()），不再使用引擎默认的 1:2:2。
	QString m_thread_count = QStringLiteral("1:1:2");
};

// 统一的媒体处理设置：
// 原先的 ImageSettings / VideoSettings 通过继承拆分，导致模型层依赖视图层
// 的 settings.h 且 ExeRunner 需要向下转型。这里收敛为单一结构体，
// 图片模式下视频相关字段保持默认值且不会被使用。
struct MediaSettings
{
	MediaKind m_kind = MediaKind::IMAGE;				   // 媒体种类
	BasicSettings m_basic;								   // 基础设置
	AdvancedSettings m_advanced;						   // 高级参数
	ProcessMode m_process_mode = ProcessMode::SINGLE_FILE; // 处理模式
	VideoEncoding m_video_encoding = VideoEncoding::H264;  // 视频编码格式
	VideoQuality m_video_quality = VideoQuality::BALANCED; // 视频编码质量
};

// ---------------------------------------------------------------------------
// 枚举 <-> 展示字符串 互相转换（供视图层使用）
// ---------------------------------------------------------------------------
QString
media_kind_to_string(MediaKind v_kind);
QString
image_type_to_string(ImageType v_type);
QString
model_type_to_string(ModelType v_type);
QString
scale_factor_to_string(ScaleFactor v_factor);
QString
process_mode_to_string(ProcessMode v_mode);
QString
video_encoding_to_string(VideoEncoding v_encoding);
QString
video_quality_to_string(VideoQuality v_quality);
QString
output_format_to_string(OutputFormat v_format);
QString
gpu_device_to_string(GPUDevice v_device);

ImageType
string_to_image_type(const QString &v_str);
ModelType
string_to_model_type(const QString &v_str);
ScaleFactor
string_to_scale_factor(const QString &v_str);
ProcessMode
string_to_process_mode(const QString &v_str);
VideoEncoding
string_to_video_encoding(const QString &v_str);
VideoQuality
string_to_video_quality(const QString &v_str);
OutputFormat
string_to_output_format(const QString &v_str);
GPUDevice
string_to_gpu_device(const QString &v_str);

// ---------------------------------------------------------------------------
// 领域辅助函数
// ---------------------------------------------------------------------------
// 模型对应的命令行短名（real / anime / anime_video）
QString
model_short_name(ModelType v_type);

// 模型对应的 realesrgan 引擎模型文件名（realesrgan-x4plus 等）
QString
model_file_name(ModelType v_type);

// 模型默认放大倍数：anime / real = 4，anime_video = 2
//（对应 Python core.MODEL_DEFAULT_SCALE）
int
model_default_scale(ModelType v_type);

// 输出格式对应的文件扩展名（"原格式" 返回空串表示沿用输入格式）
QString
output_format_extension(OutputFormat v_format);

// 视频编码质量档 + 编码格式 → (CRF, preset)
//（对应 Python video.GetEncodingParams：h264/h265 与 vp9/av1 两套矩阵）
struct VideoEncodeParams
{
	int m_crf = 23;
	QString m_preset = QStringLiteral("medium");
};

VideoEncodeParams
video_encode_params(VideoEncoding v_encoding, VideoQuality v_quality);

// 媒体种类的非翻译 token（"image" / "video"），供日志等不翻译场景使用；
// 展示文案请用 media_kind_to_string()。
QString
media_kind_token(MediaKind v_kind);

// 扩展名（可带点，大小写不敏感）是否属于 realesrgan 引擎可原生解码 / 编码的
// 图片格式（jpg/jpeg/png/webp）；bmp/gif 等需要先经 QImage 转成 PNG 再送引擎。
bool
is_engine_image_format(const QString &v_extension);

// 图片 / 视频模式的输出格式枚举顺序（即 UI 下拉顺序；视图用 itemData 承载）。
QList<OutputFormat>
image_output_format_options();
QList<OutputFormat>
video_output_format_options();

// 图块大小「自动」的领域规范值（非翻译 token，供 UI collect 写入）。
QString
tile_size_auto_token();

// 引擎安全调用参数（task-34 TDR 加固的单一来源；domain 与 ExeCommand 共用）：
//   - 图块「自动 / 0 / 非法」时显式传 -t 256，而不是省略让引擎自动决策；
//   - 显式图块值限制在 [32, 1024]，越界按边界值处理并告警；
//   - 默认线程为 1:1:2（单推理线程），且 -j 总是显式传入；
//   - 崩溃降级重试使用更保守的 -t 128 -j 1:1:1（仅重试时使用，不写回用户设置）。
int
safe_default_tile_size();
int
safe_min_tile_size();
int
safe_max_tile_size();
QString
safe_default_thread_count();
int
safe_retry_tile_size();
QString
safe_retry_thread_count();

// 图片模式允许的输出格式选项（展示用；即 *_options() 的翻译显示文本）
QStringList
image_output_format_names();
// 视频模式允许的输出格式选项（展示用）。
// 注意：不含 gif —— 原生 libav 链路没有 GIF 编码器，GIF 仅作为输入格式支持；
// OutputFormat::GIF 枚举本身保留用于旧配置/字符串兼容与输入识别。
QStringList
video_output_format_names();

#endif // MEDIASETTINGS_H