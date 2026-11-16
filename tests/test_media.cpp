// ============================================================================
// 媒体基础设施冒烟测试（Qt Test，Windows / Linux 通用）
//
// 用工程自带真实素材 input/video/1.mp4 走完整链路：
//   1. MediaProbe 探测（时长 / 分辨率 / 帧率 / 帧数 / 音频参数）；
//   2. FrameDecoder 解码前 30 帧为 PNG 序列；
//   3. VideoEncoder 用这 30 帧 + 原视频音频编码成约 1 秒的 MP4（音频 stream copy）；
//   4. 再探测输出，校验分辨率 / 时长 / 帧数 / 音频；
//   5. 音频重编码分支（MKV，强制重编码为 AAC）与取消语义。
//
// 运行前需把 FFmpeg / Qt 运行时 DLL 所在目录加入 PATH（如 C:\msys64\ucrt64\bin）。
// ============================================================================

#include "framedecoder.h"
#include "mediaframe.h"
#include "mediaprobe.h"
#include "videoencoder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

class MediaLayerTest : public QObject
{
	Q_OBJECT

  private slots:
	void
	initTestCase();
	void
	cleanupTestCase();
	void
	probe_real_video();
	void
	decode_first_frames();
	void
	encode_mp4_with_audio_copy();
	void
	encode_mkv_with_audio_transcode();
	void
	encode_webm_with_audio_transcode();
	void
	encode_mp4_duration_includes_last_frame();
	void
	decode_all_frames();
	void
	encode_publish_semantics();
	void
	encode_other_codecs();
	void
	non_ascii_paths();
	void
	cancel_during_decode();

  private:
	QString m_video_path;	  // 测试素材
	QString m_work_dir_path;  // 本次测试的临时工作目录（cleanupTestCase 删除）
	QString m_frames_dir;	  // 拆帧输出目录
	MediaInfo m_source_info;  // 素材探测结果
	int m_decoded_frames = 0; // 已解码帧数（前 N 帧）
};

void
MediaLayerTest::initTestCase()
{
	// 相对路径按测试源码目录 / 构建目录搜索
	m_video_path = QFINDTESTDATA("../input/video/1.mp4");
	QVERIFY2(!m_video_path.isEmpty(),
			 "找不到测试素材 input/video/1.mp4（请从工程根目录运行测试）");
	// 临时工作目录放在测试可执行文件所在目录（构建目录）下，避免受系统
	// 临时目录权限 / 沙箱策略影响；cleanupTestCase 统一删除。
	m_work_dir_path
		= QDir(QCoreApplication::applicationDirPath())
			  .filePath(QStringLiteral("media-test-work-%1")
							.arg(QCoreApplication::applicationPid()));
	QDir(m_work_dir_path).removeRecursively();
	QVERIFY2(
		QDir().mkpath(m_work_dir_path),
		qPrintable(
			QStringLiteral("无法创建测试临时目录：%1").arg(m_work_dir_path)));
	m_frames_dir = m_work_dir_path + QStringLiteral("/frames");
}

void
MediaLayerTest::cleanupTestCase()
{
	// 调试时可设置 RESR_KEEP_TEST_OUTPUT=1 保留临时产物（PNG 序列 / 编码结果）
	if (qEnvironmentVariableIsSet("RESR_KEEP_TEST_OUTPUT"))
	{
		qInfo("测试产物保留在：%s", qPrintable(m_work_dir_path));
		return;
	}
	QDir(m_work_dir_path).removeRecursively();
}

void
MediaLayerTest::probe_real_video()
{
	const MediaProbeResult result = MediaProbe::probe(m_video_path);
	QVERIFY2(result.m_ok, qPrintable(result.m_error.m_message));

	const MediaInfo &info = result.m_info;
	QVERIFY(info.m_has_video);
	QVERIFY(info.m_has_audio);
	QVERIFY(!info.m_format_name.isEmpty());
	QVERIFY(info.m_duration_seconds > 7.0 && info.m_duration_seconds < 8.0);
	QVERIFY(info.m_video.m_codec_name == QStringLiteral("h264"));
	QCOMPARE(info.m_video.m_width, 640);
	QCOMPARE(info.m_video.m_height, 480);
	QVERIFY(qAbs(info.m_video.m_frame_rate - 23.98) < 0.1);
	QCOMPARE(info.m_video.m_frame_rate_num, 1199);
	QCOMPARE(info.m_video.m_frame_rate_den, 50);
	QCOMPARE(info.m_video.m_frame_count, 181);
	QVERIFY(!info.m_video.m_frame_count_estimated);
	QCOMPARE(info.m_audio.m_sample_rate, 48000);
	QCOMPARE(info.m_audio.m_channels, 2);
	QVERIFY(info.m_audio.m_codec_name == QStringLiteral("aac"));

	m_source_info = info;
}

void
MediaLayerTest::decode_first_frames()
{
	FrameDecoder decoder;
	int progress_calls = 0;
	int last_done = -1;
	int last_total = -1;
	decoder.set_progress_callback(
		[&](int v_frames_done, int v_frames_total)
		{
			++progress_calls;
			QVERIFY(v_frames_done >= last_done); // 进度单调不减
			last_done = v_frames_done;
			last_total = v_frames_total;
		});

	FrameDecoder::Options options;
	options.m_max_frames = 30;
	const FrameDecoder::Result result
		= decoder.decode_to_png_sequence(m_video_path, m_frames_dir, options);
	QVERIFY2(result.m_ok, qPrintable(result.m_error.m_message));
	QCOMPARE(result.m_frames_written, 30);
	QVERIFY(result.m_frames_total >= 150);
	QVERIFY(progress_calls >= 30);
	QCOMPARE(last_done, 30);
	QCOMPARE(last_total, result.m_frames_total);

	const QStringList frame_files = MediaFrameSequence::list_frame_files(
		m_frames_dir, QStringLiteral("frame"), QStringLiteral("png"));
	QCOMPARE(frame_files.size(), 30);
	QVERIFY(frame_files.first() == QStringLiteral("frame00000001.png"));
	QVERIFY(frame_files.last() == QStringLiteral("frame00000030.png"));

	const QImage first_frame(
		QDir(m_frames_dir).filePath(QStringLiteral("frame00000001.png")));
	QVERIFY2(!first_frame.isNull(), "解码出的首帧 PNG 无法读取");
	QCOMPARE(first_frame.width(), 640);
	QCOMPARE(first_frame.height(), 480);

	// 色彩合理性：YUV→RGB 若走错通道，绿色会异常偏高
	const QImage rgb_frame = first_frame.convertToFormat(QImage::Format_RGB888);
	double sum_r = 0.0;
	double sum_g = 0.0;
	double sum_b = 0.0;
	int samples = 0;
	for (int y = 0; y < rgb_frame.height(); y += 16)
	{
		for (int x = 0; x < rgb_frame.width(); x += 16)
		{
			const QColor pixel = rgb_frame.pixelColor(x, y);
			sum_r += pixel.red();
			sum_g += pixel.green();
			sum_b += pixel.blue();
			++samples;
		}
	}
	QVERIFY(samples > 0);
	const double average_r = sum_r / samples;
	const double average_g = sum_g / samples;
	const double average_b = sum_b / samples;
	QVERIFY2(average_g < 2.0 * qMax(average_r, average_b) + 24.0,
			 "解码帧绿色通道异常偏高，YUV→RGB 色彩矩阵可能错误");

	m_decoded_frames = result.m_frames_written;
	m_source_info = result.m_source;
}

void
MediaLayerTest::encode_mp4_with_audio_copy()
{
	if (!VideoEncoder::is_encoder_available(VideoEncoding::H264))
	{
		QSKIP("当前 FFmpeg 构建缺少 libx264，跳过 H.264 编码用例");
	}
	const double frame_rate = (m_source_info.m_video.m_frame_rate > 0.0)
								  ? m_source_info.m_video.m_frame_rate
								  : 30.0;

	VideoEncoder encoder;
	int last_done = -1;
	int last_total = -1;
	encoder.set_progress_callback(
		[&](int v_frames_done, int v_frames_total)
		{
			last_done = v_frames_done;
			last_total = v_frames_total;
		});

	VideoEncoder::Options options;
	options.m_encoding = VideoEncoding::H264;
	options.m_crf = 23;
	options.m_preset = QStringLiteral("ultrafast");
	options.m_frame_rate = frame_rate;
	options.m_frame_rate_num = m_source_info.m_video.m_frame_rate_num;
	options.m_frame_rate_den = m_source_info.m_video.m_frame_rate_den;
	options.m_audio_source_path = m_video_path;
	options.m_copy_audio = true;
	// 只编码 30 帧（约 1.25 秒），音频同步截断到视频时长附近
	options.m_audio_max_seconds = m_decoded_frames / frame_rate + 0.05;

	const QString output_path
		= m_work_dir_path + QStringLiteral("/out_h264_copy.mp4");
	const VideoEncoder::Result result
		= encoder.encode_png_sequence(m_frames_dir, output_path, options);
	QVERIFY2(result.m_ok, qPrintable(result.m_error.m_message));
	QCOMPARE(result.m_frames_encoded, m_decoded_frames);
	QVERIFY(result.m_audio_packets > 0);
	QVERIFY(result.m_audio_copied);
	QCOMPARE(last_done, m_decoded_frames);
	QCOMPARE(last_total, m_decoded_frames);

	const QFileInfo output_info(output_path);
	QVERIFY(output_info.exists());
	QVERIFY(output_info.size() > 1024);

	// 再探测输出文件，校验分辨率 / 时长 / 帧数 / 音频
	const MediaProbeResult probe = MediaProbe::probe(output_path);
	QVERIFY2(probe.m_ok, qPrintable(probe.m_error.m_message));
	QVERIFY(probe.m_info.m_has_video);
	QVERIFY(probe.m_info.m_has_audio);
	QCOMPARE(probe.m_info.m_video.m_width, 640);
	QCOMPARE(probe.m_info.m_video.m_height, 480);
	const double expected_seconds = m_decoded_frames / frame_rate;
	QVERIFY2(qAbs(probe.m_info.m_duration_seconds - expected_seconds) < 0.5,
			 qPrintable(QStringLiteral("输出时长异常：%1 秒，期望约 %2 秒")
							.arg(probe.m_info.m_duration_seconds)
							.arg(expected_seconds)));
	QVERIFY(probe.m_info.m_video.m_frame_count >= 25
			&& probe.m_info.m_video.m_frame_count <= 35);
	QVERIFY(probe.m_info.m_audio.m_sample_rate == 48000);
}

void
MediaLayerTest::encode_mkv_with_audio_transcode()
{
	if (!VideoEncoder::is_encoder_available(VideoEncoding::H264))
	{
		QSKIP("当前 FFmpeg 构建缺少 libx264，跳过 H.264 编码用例");
	}
	const double frame_rate = (m_source_info.m_video.m_frame_rate > 0.0)
								  ? m_source_info.m_video.m_frame_rate
								  : 30.0;

	VideoEncoder encoder;
	VideoEncoder::Options options;
	options.m_encoding = VideoEncoding::H264;
	options.m_crf = 26;
	options.m_preset = QStringLiteral("ultrafast");
	options.m_frame_rate = frame_rate;
	options.m_frame_rate_num = m_source_info.m_video.m_frame_rate_num;
	options.m_frame_rate_den = m_source_info.m_video.m_frame_rate_den;
	options.m_audio_source_path = m_video_path;
	options.m_copy_audio
		= false; // 强制重编码，覆盖「不支持 copy 时重编码」分支
	options.m_audio_max_seconds = m_decoded_frames / frame_rate + 0.05;

	const QString output_path
		= m_work_dir_path + QStringLiteral("/out_h264_transcode.mkv");
	const VideoEncoder::Result result
		= encoder.encode_png_sequence(m_frames_dir, output_path, options);
	QVERIFY2(result.m_ok, qPrintable(result.m_error.m_message));
	QCOMPARE(result.m_frames_encoded, m_decoded_frames);
	QVERIFY(result.m_audio_packets > 0);
	QVERIFY(!result.m_audio_copied);

	const MediaProbeResult probe = MediaProbe::probe(output_path);
	QVERIFY2(probe.m_ok, qPrintable(probe.m_error.m_message));
	QCOMPARE(probe.m_info.m_video.m_width, 640);
	QCOMPARE(probe.m_info.m_video.m_height, 480);
	QVERIFY(probe.m_info.m_has_audio);
	const double expected_seconds = m_decoded_frames / frame_rate;
	QVERIFY(qAbs(probe.m_info.m_duration_seconds - expected_seconds) < 0.5);
}

void
MediaLayerTest::encode_webm_with_audio_transcode()
{
	if (!VideoEncoder::is_encoder_available(VideoEncoding::VP9))
	{
		QSKIP("当前 FFmpeg 构建缺少 libvpx-vp9，跳过 WebM 编码用例");
	}
	// 回归：webm 的音频必须重编码为 libopus；libopus 要求固定 960 样本/帧，
	// 旧实现把任意长度重采样帧直接送编码器 → avformat_write_header EINVAL。
	const double frame_rate = (m_source_info.m_video.m_frame_rate > 0.0)
								  ? m_source_info.m_video.m_frame_rate
								  : 30.0;

	VideoEncoder encoder;
	VideoEncoder::Options options;
	options.m_encoding = VideoEncoding::VP9;
	options.m_crf = 32;
	options.m_frame_rate = frame_rate;
	options.m_frame_rate_num = m_source_info.m_video.m_frame_rate_num;
	options.m_frame_rate_den = m_source_info.m_video.m_frame_rate_den;
	options.m_audio_source_path = m_video_path;
	// 与生产链路一致：m_copy_audio = true，由编码器判定 webm 不接受 AAC 后
	// 自动回退 libopus 重编码（回归：旧实现误判可 copy → 写头失败）。
	options.m_copy_audio = true;
	options.m_audio_max_seconds = m_decoded_frames / frame_rate + 0.05;

	const QString output_path
		= m_work_dir_path + QStringLiteral("/out_vp9_opus.webm");
	const VideoEncoder::Result result
		= encoder.encode_png_sequence(m_frames_dir, output_path, options);
	QVERIFY2(result.m_ok, qPrintable(result.m_error.m_message));
	QCOMPARE(result.m_frames_encoded, m_decoded_frames);
	QVERIFY(result.m_audio_packets > 0);
	QVERIFY(!result.m_audio_copied);

	const MediaProbeResult probe = MediaProbe::probe(output_path);
	QVERIFY2(probe.m_ok, qPrintable(probe.m_error.m_message));
	QVERIFY(probe.m_info.m_has_video);
	QCOMPARE(probe.m_info.m_video.m_codec_name, QStringLiteral("vp9"));
	QVERIFY(probe.m_info.m_has_audio);
	QCOMPARE(probe.m_info.m_audio.m_codec_name, QStringLiteral("opus"));
	const double expected_seconds = m_decoded_frames / frame_rate;
	QVERIFY2(qAbs(probe.m_info.m_duration_seconds - expected_seconds) < 0.5,
			 qPrintable(QStringLiteral("webm 输出时长异常：%1 秒，期望约 %2 秒")
							.arg(probe.m_info.m_duration_seconds)
							.arg(expected_seconds)));
}

void
MediaLayerTest::encode_mp4_duration_includes_last_frame()
{
	if (!VideoEncoder::is_encoder_available(VideoEncoding::H264))
	{
		QSKIP("当前 FFmpeg 构建缺少 libx264，跳过 H.264 编码用例");
	}
	const double frame_rate = (m_source_info.m_video.m_frame_rate > 0.0)
								  ? m_source_info.m_video.m_frame_rate
								  : 30.0;
	const double frame_duration = 1.0 / frame_rate;

	VideoEncoder encoder;
	VideoEncoder::Options options;
	options.m_encoding = VideoEncoding::H264;
	options.m_crf = 23;
	options.m_preset = QStringLiteral("ultrafast");
	options.m_frame_rate = frame_rate;
	options.m_frame_rate_num = m_source_info.m_video.m_frame_rate_num;
	options.m_frame_rate_den = m_source_info.m_video.m_frame_rate_den;

	const QString output_path
		= m_work_dir_path + QStringLiteral("/out_duration.mp4");
	const VideoEncoder::Result result
		= encoder.encode_png_sequence(m_frames_dir, output_path, options);
	QVERIFY2(result.m_ok, qPrintable(result.m_error.m_message));

	// 回归：旧实现不设置 packet duration，muxer 以最后一个 PTS 当流时长，
	// 产物比源少一帧；修复后应为 N/fps（允许 1/4 帧误差）。
	const MediaProbeResult probe = MediaProbe::probe(output_path);
	QVERIFY2(probe.m_ok, qPrintable(probe.m_error.m_message));
	const double expected_seconds = m_decoded_frames * frame_duration;
	QVERIFY2(qAbs(probe.m_info.m_duration_seconds - expected_seconds)
				 < frame_duration * 0.25,
			 qPrintable(QStringLiteral("MP4 时长少了末帧：%1 秒，期望约 %2 秒")
							.arg(probe.m_info.m_duration_seconds)
							.arg(expected_seconds)));
}

void
MediaLayerTest::decode_all_frames()
{
	// 回归：FrameDecoder 的 send/receive 循环重构（EAGAIN 重发 + flush 共用
	// 取帧逻辑）后必须解出视频全部帧、不能因 flush 路径丢帧。
	const QString frames_dir = m_work_dir_path + QStringLiteral("/frames_all");
	FrameDecoder decoder;
	FrameDecoder::Options options; // 不限制帧数
	const FrameDecoder::Result result
		= decoder.decode_to_png_sequence(m_video_path, frames_dir, options);
	QVERIFY2(result.m_ok, qPrintable(result.m_error.m_message));
	QCOMPARE(result.m_frames_written, m_source_info.m_video.m_frame_count);

	const QStringList files = MediaFrameSequence::list_frame_files(
		frames_dir, QStringLiteral("frame"), QStringLiteral("png"));
	QCOMPARE(files.size(), result.m_frames_written);
}

void
MediaLayerTest::encode_publish_semantics()
{
	// 回归：输出先写同目录临时文件、成功后替换（旧实现直接打开最终路径，
	// 取消/失败会留下半成品并截断已存在的旧产物）。
	if (!VideoEncoder::is_encoder_available(VideoEncoding::H264))
	{
		QSKIP("当前 FFmpeg 构建缺少 libx264，跳过 H.264 编码用例");
	}
	const double frame_rate = (m_source_info.m_video.m_frame_rate > 0.0)
								  ? m_source_info.m_video.m_frame_rate
								  : 30.0;

	VideoEncoder::Options options;
	options.m_encoding = VideoEncoding::H264;
	options.m_crf = 28;
	options.m_preset = QStringLiteral("ultrafast");
	options.m_frame_rate = frame_rate;
	options.m_frame_rate_num = m_source_info.m_video.m_frame_rate_num;
	options.m_frame_rate_den = m_source_info.m_video.m_frame_rate_den;

	const QString output_path
		= m_work_dir_path + QStringLiteral("/out_publish.mp4");
	const QByteArray old_content("OLD_GOOD_OUTPUT");
	{
		QFile existing(output_path);
		QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::Truncate));
		QCOMPARE(existing.write(old_content),
				 static_cast<qint64>(old_content.size()));
	}

	// 1) 取消编码：旧产物保持原样，无半成品/临时文件。
	{
		VideoEncoder encoder;
		int progress_calls = 0;
		encoder.set_progress_callback([&](int, int) { ++progress_calls; });
		encoder.set_cancel_checker([&]() { return progress_calls >= 1; });
		const VideoEncoder::Result result
			= encoder.encode_png_sequence(m_frames_dir, output_path, options);
		QVERIFY(!result.m_ok);
		QCOMPARE(result.m_error.m_code, MediaErrorCode::CANCELLED);

		QFile preserved(output_path);
		QVERIFY(preserved.open(QIODevice::ReadOnly));
		QCOMPARE(preserved.readAll(), old_content);
	}

	// 2) 成功编码：旧产物被有效视频替换。
	{
		VideoEncoder encoder;
		const VideoEncoder::Result result
			= encoder.encode_png_sequence(m_frames_dir, output_path, options);
		QVERIFY2(result.m_ok, qPrintable(result.m_error.m_message));

		QFile output(output_path);
		QVERIFY(output.open(QIODevice::ReadOnly));
		QVERIFY(output.readAll() != old_content);

		const MediaProbeResult probe = MediaProbe::probe(output_path);
		QVERIFY2(probe.m_ok, qPrintable(probe.m_error.m_message));
		QVERIFY(probe.m_info.m_has_video);
	}

	// 临时文件命名含 resr-part；任何路径都不应残留。
	const QStringList leftovers
		= QDir(m_work_dir_path)
			  .entryList(QStringList{ QStringLiteral("*resr-part*") },
						 QDir::Files | QDir::Hidden);
	QVERIFY2(leftovers.isEmpty(),
			 qPrintable(leftovers.join(QStringLiteral(", "))));
}

void
MediaLayerTest::encode_other_codecs()
{
	// 覆盖 README 中承诺的 H.265 / AV1 输出（此前仅 H.264/VP9 有回归用例）。
	// 编码器缺失时按平台 QSKIP（Windows MSYS2 的 FFmpeg 构建可能不含）。
	//
	// 注意（CI）：Linux valgrind 3.26 在 libx265 / libSvtAv1Enc 的 SIMD 汇编
	// 路径下会报大量 uninitialised 误报并 abort（栈全在外部库，无项目帧）。
	// 因此 valgrind 任务应通过测试函数过滤参数排除本用例（其余媒体用例仍全跑）。
	const double frame_rate = (m_source_info.m_video.m_frame_rate > 0.0)
								  ? m_source_info.m_video.m_frame_rate
								  : 30.0;
	VideoEncoder::Options options;
	options.m_crf = 30;
	options.m_frame_rate = frame_rate;
	options.m_frame_rate_num = m_source_info.m_video.m_frame_rate_num;
	options.m_frame_rate_den = m_source_info.m_video.m_frame_rate_den;

	if (VideoEncoder::is_encoder_available(VideoEncoding::H265))
	{
		options.m_encoding = VideoEncoding::H265;
		const QString output_path
			= m_work_dir_path + QStringLiteral("/out_h265.mp4");
		VideoEncoder encoder;
		const VideoEncoder::Result result
			= encoder.encode_png_sequence(m_frames_dir, output_path, options);
		QVERIFY2(result.m_ok, qPrintable(result.m_error.m_message));
		const MediaProbeResult probe = MediaProbe::probe(output_path);
		QVERIFY2(probe.m_ok, qPrintable(probe.m_error.m_message));
		QCOMPARE(probe.m_info.m_video.m_codec_name, QStringLiteral("hevc"));
	}

	if (VideoEncoder::is_encoder_available(VideoEncoding::AV1))
	{
		options.m_encoding = VideoEncoding::AV1;
		const QString output_path
			= m_work_dir_path + QStringLiteral("/out_av1.mkv");
		VideoEncoder encoder;
		const VideoEncoder::Result result
			= encoder.encode_png_sequence(m_frames_dir, output_path, options);
		QVERIFY2(result.m_ok, qPrintable(result.m_error.m_message));
		const MediaProbeResult probe = MediaProbe::probe(output_path);
		QVERIFY2(probe.m_ok, qPrintable(probe.m_error.m_message));
		QCOMPARE(probe.m_info.m_video.m_codec_name, QStringLiteral("av1"));
	}
}

void
MediaLayerTest::non_ascii_paths()
{
	// 阶段二修复的 Windows 中文路径回归：媒体层改用 QString::toUtf8() 传
	// FFmpeg。Linux（UTF-8 locale）下修复前后都可用，本用例保证不回归并把
	// 该路径写进自动化；Windows 真机仍需人工验证（见交接文档/修复报告）。
	const QString base = m_work_dir_path + QStringLiteral("/中文 目录");
	QVERIFY2(QDir().mkpath(base), "无法创建中文路径测试目录");
	const QString input_path = base + QStringLiteral("/测试 视频.mp4");
	QVERIFY2(QFile::copy(m_video_path, input_path),
			 "无法复制测试素材到中文路径");

	const MediaProbeResult probe = MediaProbe::probe(input_path);
	QVERIFY2(probe.m_ok, qPrintable(probe.m_error.m_message));
	QVERIFY(probe.m_info.m_has_video);
	QVERIFY(probe.m_info.m_has_audio);

	const QString frames_dir = base + QStringLiteral("/帧序列");
	FrameDecoder decoder;
	FrameDecoder::Options decode_options;
	decode_options.m_max_frames = 3;
	const FrameDecoder::Result decode_result = decoder.decode_to_png_sequence(
		input_path, frames_dir, decode_options);
	QVERIFY2(decode_result.m_ok, qPrintable(decode_result.m_error.m_message));
	QCOMPARE(decode_result.m_frames_written, 3);

	if (!VideoEncoder::is_encoder_available(VideoEncoding::H264))
	{
		QSKIP("当前 FFmpeg 构建缺少 libx264，跳过编码段");
	}
	VideoEncoder encoder;
	VideoEncoder::Options options;
	options.m_encoding = VideoEncoding::H264;
	options.m_crf = 26;
	options.m_preset = QStringLiteral("ultrafast");
	options.m_frame_rate_num = m_source_info.m_video.m_frame_rate_num;
	options.m_frame_rate_den = m_source_info.m_video.m_frame_rate_den;
	options.m_frame_rate = m_source_info.m_video.m_frame_rate;
	const QString output_path = base + QStringLiteral("/测试 输出.mp4");
	const VideoEncoder::Result encode_result
		= encoder.encode_png_sequence(frames_dir, output_path, options);
	QVERIFY2(encode_result.m_ok, qPrintable(encode_result.m_error.m_message));
	const MediaProbeResult output_probe = MediaProbe::probe(output_path);
	QVERIFY2(output_probe.m_ok, qPrintable(output_probe.m_error.m_message));
	QVERIFY(output_probe.m_info.m_has_video);
}

void
MediaLayerTest::cancel_during_decode()
{
	FrameDecoder decoder;
	int poll_count = 0;
	decoder.set_cancel_checker(
		[&]()
		{
			++poll_count;
			return poll_count > 5; // 第 6 次轮询起请求取消
		});

	const QString cancel_dir
		= m_work_dir_path + QStringLiteral("/cancel_frames");
	FrameDecoder::Options options; // 不限制帧数，靠取消提前退出
	const FrameDecoder::Result result
		= decoder.decode_to_png_sequence(m_video_path, cancel_dir, options);
	QVERIFY(!result.m_ok);
	QCOMPARE(result.m_error.m_code, MediaErrorCode::CANCELLED);
	QVERIFY(result.m_frames_written > 0);
	QVERIFY(result.m_frames_written < 20);
}

QTEST_GUILESS_MAIN(MediaLayerTest)

#include "test_media.moc"
