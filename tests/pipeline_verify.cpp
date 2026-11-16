// ============================================================================
// 原生处理管线验收工具（手动执行，不注册进 ctest）
//
// 用工程 input/ 下的真实素材，通过 ProcessService 完整跑通四条链路：
//   1. 单文件图片：4 倍超分 + 原格式输出
//   2. 批量图片：整目录超分，输出目录命名 / 文件数校验
//   3. 单文件视频：探测 → 拆帧 → 帧超分 → libav* 编码（含原音频）
//   4. 批量视频：逐视频串行处理 + 总进度 + 失败聚合
//
// 输出：每个链路的实际输出路径、文件大小、耗时、探测/尺寸校验结果。
// 用法：<构建目录>/tests/resr_pipeline_verify[.exe] [--out <输出根目录>]
//       （构建目录可任意嵌套，工具会自动向上查找工程根）
// 退出码：0 = 四条链路全部成功；1 = 有链路失败。
// ============================================================================

#include "fileservice.h"
#include "logger.h"
#include "mediaprobe.h"
#include "mediasettings.h"
#include "processrequest.h"
#include "processservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QList>
#include <QTextStream>
#include <QThread>

#include <cstdio>

namespace
{

struct ChainResult
{
	QString m_name;
	bool m_ok = false;
	QString m_message;
	QString m_output;
	qint64 m_elapsed_ms = 0;
	qint64 m_bytes = 0;
	QString m_detail;
};

QString
project_root()
{
	// 从可执行文件所在目录向上查找工程根：必须同时含 CMakeLists.txt 与
	// input/。不能假设 applicationDirPath()/../.. 就是工程根——本工具从
	// build/ci/tests、build-r6/tests 等嵌套构建目录运行时，那里并不是仓库根，
	// 会找不到 input/ 素材（与 test_pipeline.cpp 的 project_root() 同一实现；
	// 本工具不注册 ctest、单独链接 test 目标，故不引入共享 helper）。
	QDir directory(QCoreApplication::applicationDirPath());
	while (true)
	{
		if (directory.exists(QStringLiteral("CMakeLists.txt"))
			&& directory.exists(QStringLiteral("input")))
		{
			return directory.absolutePath();
		}
		if (!directory.cdUp())
		{
			break;
		}
	}

	// 保底行为与旧实现一致，便于在非常规目录结构下给出可诊断的路径。
	return QDir::cleanPath(
		QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../.."));
}

void
reset_dir(const QString &v_path)
{
	QDir(v_path).removeRecursively();
	QDir().mkpath(v_path);
}

QString
format_size(qint64 v_bytes)
{
	if (v_bytes >= 1024 * 1024)
	{
		return QStringLiteral("%1 MB").arg(v_bytes / (1024.0 * 1024.0), 0, 'f',
										   2);
	}
	if (v_bytes >= 1024)
	{
		return QStringLiteral("%1 KB").arg(v_bytes / 1024.0, 0, 'f', 2);
	}
	return QStringLiteral("%1 B").arg(v_bytes);
}

ChainResult
run_chain(const QString &v_name, const ProcessRequest &v_request,
		  int v_timeout_ms)
{
	ChainResult result;
	result.m_name = v_name;

	ProcessService service;
	int last_reported_block = -1;

	const QMetaObject::Connection status_connection = QObject::connect(
		&service, &ProcessService::status_changed,
		[&last_reported_block, &v_name](const ProcessStatus &v_status)
		{
			const int block = v_status.m_progress_percent / 20;
			if (block != last_reported_block
				|| v_status.m_state == ProcessState::PAUSED)
			{
				last_reported_block = block;
				std::printf("    [%s] %3d%%  %s\n", qPrintable(v_name),
							v_status.m_progress_percent,
							qPrintable(v_status.m_message));
				std::fflush(stdout);
			}
		});
	const QMetaObject::Connection output_connection = QObject::connect(
		&service, &ProcessService::output_ready,
		[&result](const QString &v_path) { result.m_output = v_path; });
	const QMetaObject::Connection finished_connection
		= QObject::connect(&service, &ProcessService::finished,
						   [&result](bool v_ok, const QString &v_message)
						   {
							   result.m_ok = v_ok;
							   result.m_message = v_message;
						   });

	QElapsedTimer timer;
	timer.start();
	bool finished = false;
	const QMetaObject::Connection finished_flag_connection = QObject::connect(
		&service, &ProcessService::finished,
		[&finished](bool, const QString &) { finished = true; });

	service.start(v_request);
	while (!finished && timer.elapsed() < v_timeout_ms)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
		QThread::msleep(10);
	}
	result.m_elapsed_ms = timer.elapsed();

	QObject::disconnect(status_connection);
	QObject::disconnect(output_connection);
	QObject::disconnect(finished_connection);
	QObject::disconnect(finished_flag_connection);

	if (!finished)
	{
		result.m_ok = false;
		result.m_message = QStringLiteral("超时（%1 ms）").arg(v_timeout_ms);
		service.stop();
	}

	if (!result.m_output.isEmpty())
	{
		const QFileInfo output_info(result.m_output);
		result.m_bytes
			= output_info.isDir()
				  ? FileService::directory_total_size(result.m_output)
				  : output_info.size();
	}
	return result;
}

} // namespace

int
main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	Logger::instance()->set_log_level(LogLevel::WARNING);

	const QString root = project_root();
	QString output_root = root + QStringLiteral("/build/verify");

	const QStringList arguments = app.arguments();
	for (int i = 1; i < arguments.size(); ++i)
	{
		if (arguments.at(i) == QStringLiteral("--out")
			&& i + 1 < arguments.size())
		{
			output_root = arguments.at(i + 1);
		}
	}

	QTextStream out(stdout);
	out << "工程根目录: " << root << "\n";
	out << "输出根目录: " << output_root << "\n";
	out.flush();

	reset_dir(output_root);
	const QString temp_dir = output_root + QStringLiteral("/temp");
	QDir().mkpath(temp_dir);

	QList<ChainResult> results;

	// ----------------------------------------------------------------------
	// 链路 1：单文件图片（4 倍真实照片模型）
	// ----------------------------------------------------------------------
	{
		MediaSettings settings;
		settings.m_kind = MediaKind::IMAGE;
		settings.m_basic.m_model_type = ModelType::REAL_ESRGAN_X4PLUS;
		settings.m_basic.m_scale_factor = ScaleFactor::SCALE4X;
		settings.m_advanced.m_output_format = OutputFormat::ORIGINAL;

		ProcessRequest request;
		request.m_kind = MediaKind::IMAGE;
		request.m_mode = ProcessMode::SINGLE_FILE;
		request.m_input_path = root + QStringLiteral("/input/1.png");
		request.m_output_path = output_root + QStringLiteral("/image_single");
		request.m_settings = settings;
		request.m_temp_directory = temp_dir;
		QDir().mkpath(request.m_output_path);

		ChainResult result
			= run_chain(QStringLiteral("单文件图片"), request, 180000);
		const QString expected
			= request.m_output_path + QStringLiteral("/1_png_real.png");
		if (result.m_ok)
		{
			const QImage source(request.m_input_path);
			const QImage target(expected);
			result.m_ok = QFileInfo(expected).isFile() && !target.isNull()
						  && target.width() == source.width() * 4
						  && target.height() == source.height() * 4;
			result.m_detail = QStringLiteral("%1x%2 -> %3x%4，%5")
								  .arg(source.width())
								  .arg(source.height())
								  .arg(target.width())
								  .arg(target.height())
								  .arg(format_size(QFileInfo(expected).size()));
		}
		results << result;
	}

	// ----------------------------------------------------------------------
	// 链路 2：批量图片（整目录 2 倍动漫模型，输出目录命名校验）
	// ----------------------------------------------------------------------
	{
		MediaSettings settings;
		settings.m_kind = MediaKind::IMAGE;
		settings.m_basic.m_model_type = ModelType::REAL_ESRGAN_X4PLUS_ANIME;
		settings.m_basic.m_scale_factor = ScaleFactor::SCALE2X;
		settings.m_advanced.m_output_format = OutputFormat::ORIGINAL;

		ProcessRequest request;
		request.m_kind = MediaKind::IMAGE;
		request.m_mode = ProcessMode::BATCH_PROCESS;
		request.m_input_path = root + QStringLiteral("/input");
		request.m_output_path = output_root + QStringLiteral("/image_batch");
		request.m_settings = settings;
		request.m_temp_directory = temp_dir;
		QDir().mkpath(request.m_output_path);

		ChainResult result
			= run_chain(QStringLiteral("批量图片"), request, 300000);
		const QString expected_dir
			= request.m_output_path + QStringLiteral("/input_anime");
		const int file_count
			= QDir(expected_dir)
				  .entryList(QStringList{ QStringLiteral("*.png") },
							 QDir::Files)
				  .size();
		if (result.m_ok)
		{
			result.m_ok = (file_count == 9);
			result.m_detail
				= QStringLiteral("输出 %1 个 PNG（期望 9），合计 %2")
					  .arg(file_count)
					  .arg(format_size(result.m_bytes));
		}
		results << result;
	}

	// ----------------------------------------------------------------------
	// 链路 3：单文件视频（动画视频模型 2 倍 + H.264 均衡）
	// ----------------------------------------------------------------------
	{
		MediaSettings settings;
		settings.m_kind = MediaKind::VIDEO;
		settings.m_basic.m_model_type = ModelType::REAL_ESR_ANIMEVIDEOV3;
		settings.m_basic.m_scale_factor = ScaleFactor::SCALE2X;
		settings.m_advanced.m_output_format = OutputFormat::ORIGINAL;
		settings.m_video_encoding = VideoEncoding::H264;
		settings.m_video_quality = VideoQuality::BALANCED;

		ProcessRequest request;
		request.m_kind = MediaKind::VIDEO;
		request.m_mode = ProcessMode::SINGLE_FILE;
		request.m_input_path = root + QStringLiteral("/input/video/1.mp4");
		request.m_output_path = output_root + QStringLiteral("/video_single");
		request.m_settings = settings;
		request.m_temp_directory = temp_dir;
		QDir().mkpath(request.m_output_path);

		ChainResult result
			= run_chain(QStringLiteral("单文件视频"), request, 900000);
		const QString expected
			= request.m_output_path + QStringLiteral("/1_mp4_anime_video.mp4");
		if (result.m_ok)
		{
			const MediaProbeResult source_probe
				= MediaProbe::probe(request.m_input_path);
			const MediaProbeResult target_probe = MediaProbe::probe(expected);
			result.m_ok = target_probe.m_ok && target_probe.m_info.m_has_video
						  && target_probe.m_info.m_video.m_width
								 == source_probe.m_info.m_video.m_width * 2
						  && target_probe.m_info.m_video.m_height
								 == source_probe.m_info.m_video.m_height * 2;
			result.m_detail
				= QStringLiteral("%1x%2 -> %3x%4，时长 %5s，音频 %6")
					  .arg(source_probe.m_info.m_video.m_width)
					  .arg(source_probe.m_info.m_video.m_height)
					  .arg(target_probe.m_info.m_video.m_width)
					  .arg(target_probe.m_info.m_video.m_height)
					  .arg(target_probe.m_info.m_duration_seconds, 0, 'f', 2)
					  .arg(target_probe.m_info.m_has_audio ? "有" : "无");
		}
		results << result;
	}

	// ----------------------------------------------------------------------
	// 链路 4：批量视频（3 个视频串行 + 总进度 + 失败聚合）
	// ----------------------------------------------------------------------
	{
		MediaSettings settings;
		settings.m_kind = MediaKind::VIDEO;
		settings.m_basic.m_model_type = ModelType::REAL_ESR_ANIMEVIDEOV3;
		settings.m_basic.m_scale_factor = ScaleFactor::SCALE2X;
		settings.m_advanced.m_output_format = OutputFormat::ORIGINAL;
		settings.m_video_encoding = VideoEncoding::H264;
		settings.m_video_quality = VideoQuality::BALANCED;

		ProcessRequest request;
		request.m_kind = MediaKind::VIDEO;
		request.m_mode = ProcessMode::BATCH_PROCESS;
		request.m_input_path = root + QStringLiteral("/input/video");
		request.m_output_path = output_root + QStringLiteral("/video_batch");
		request.m_settings = settings;
		request.m_temp_directory = temp_dir;
		QDir().mkpath(request.m_output_path);

		ChainResult result
			= run_chain(QStringLiteral("批量视频"), request, 1800000);
		const QString expected_dir
			= request.m_output_path + QStringLiteral("/video_anime_video");
		const QStringList files
			= QDir(expected_dir)
				  .entryList(QStringList{ QStringLiteral("*.mp4") },
							 QDir::Files);
		if (result.m_ok)
		{
			bool all_valid = (files.size() == 3);
			for (const QString &name : files)
			{
				const MediaProbeResult probe
					= MediaProbe::probe(expected_dir + "/" + name);
				all_valid = all_valid && probe.m_ok && probe.m_info.m_has_video;
			}
			result.m_ok = all_valid;
			result.m_detail
				= QStringLiteral("输出 %1 个视频（期望 3）").arg(files.size());
		}
		results << result;
	}

	// ----------------------------------------------------------------------
	// 汇总
	// ----------------------------------------------------------------------
	out << "\n================ 四条链路验收汇总 ================\n";
	bool all_ok = true;
	for (const ChainResult &result : results)
	{
		out << (result.m_ok ? "[通过] " : "[失败] ") << result.m_name
			<< "  耗时 " << result.m_elapsed_ms << " ms"
			<< "  产物 " << result.m_output << "\n";
		if (!result.m_detail.isEmpty())
		{
			out << "        校验: " << result.m_detail << "\n";
		}
		if (!result.m_message.isEmpty())
		{
			out << "        消息: " << result.m_message << "\n";
		}
		all_ok = all_ok && result.m_ok;
	}
	out.flush();

	return all_ok ? 0 : 1;
}
