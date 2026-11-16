// ============================================================================
// 原生处理管线测试（Windows / Linux 均可运行，不依赖 /bin/sh 与 /tmp）
//
// 覆盖三层：
//   1. 纯逻辑：输出命名规则（与旧 Python 编排 1:1）、输入扫描、引擎输出解析、
//      领域编码参数矩阵、引擎命令行拼装；
//   2. 真实引擎冒烟：单图 / 批量图片走 bin/realesrgan 原生链路（产物 + 尺寸校验）；
//   3. 视频原生链路冒烟：探测 → 拆帧 → 帧超分 → libav* 编码（只处理前 12 帧）；
// 另含失败识别（不信任引擎退出码）与停止语义两条回归用例。
//
// 临时目录放在测试可执行文件所在目录（构建目录）下、按进程号隔离，由
// cleanupTestCase 统一删除：不往工程根写文件，多构建目录/多进程并行跑 ctest
// 也不会互删。
// bin/realesrgan 引擎不可用时（不存在 / 无执行权限 / 非 Windows 平台的 Windows
// PE），依赖引擎的用例会 QSKIP 并说明原因，而不是误报失败。
// ============================================================================

#include "execommand.h"
#include "exerunner.h"
#include "inputscanner.h"
#include "mediaprobe.h"
#include "mediasettings.h"
#include "outputpathresolver.h"
#include "exeoutputparser.h"
#include "language_manager.h"
#include "processrequest.h"
#include "processservice.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QList>
#include <QSettings>
#include <QTest>

#ifdef Q_OS_WIN
#include <QLibrary>
#include <windows.h>
#endif

namespace
{

struct JobOutcome
{
	bool m_start_accepted = false;
	bool m_finished = false;
	bool m_success = false;
	QString m_message;
	QString m_output;
	int m_last_progress = 0;
	ProcessState m_last_state = ProcessState::IDLE;
};

QString
project_root()
{
	// 从可执行文件所在目录向上查找工程根：必须同时含 CMakeLists.txt 与
	// input/。不能再假设 applicationDirPath()/../.. 就是工程根——CI 用
	// build/ci、本机验证用 build-r6 等构建目录时，那里并不是仓库根，
	// 会找不到 input/ 素材。
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

QString
test_work_dir()
{
	// 放在构建目录（可执行文件所在目录）下，而不是工程根的 build/：
	// 构建目录可能是 build/ci、build-r6 或任意自定义目录，工程根不应被测试
	// 写入；带 PID 保证并行/残留实例互不干扰。
	static const QString path
		= QDir(QCoreApplication::applicationDirPath())
			  .filePath(QStringLiteral("pipeline-test-work-%1")
							.arg(QCoreApplication::applicationPid()));
	QDir().mkpath(path);
	return path;
}

void
reset_dir(const QString &v_path)
{
	QDir(v_path).removeRecursively();
	QDir().mkpath(v_path);
}

QString
path_in_dir(const QString &v_dir, const QString &v_name)
{
	return QDir(v_dir).filePath(v_name);
}

QString
engine_executable()
{
	return ExeCommand::realesrgan_executable(
		ExeCommand::discover_bin_directory());
}

#ifdef Q_OS_WIN
// 真实 GPU 引擎用例的显式开关：默认不跑（避免 CI/开发机 ctest 误启动引擎
// 触发环境 TDR）。设置 RESR_TEST_ENGINE=1 才允许；Windows 上还要求最低
// Vulkan/GPU 能力探测通过（静态审查 A1/B4）。
bool
engine_tests_enabled()
{
	static const bool enabled = []()
	{
		const QByteArray value
			= qgetenv("RESR_TEST_ENGINE").trimmed().toLower();
		return value == "1" || value == "true" || value == "on"
			   || value == "yes";
	}();
	return enabled;
}

// 最低 Vulkan/GPU 能力探测：只加载 vulkan-1.dll 并枚举物理设备，不创建窗口、
// 不启动引擎、不做任何 GPU 计算。用于真实引擎用例的默认安全跳过。
bool
vulkan_gpu_available()
{
	static const bool available = []()
	{
		QLibrary loader(QStringLiteral("vulkan-1.dll"));
		if (!loader.load())
		{
			const QString system_root = qEnvironmentVariable("SystemRoot");
			if (system_root.isEmpty())
			{
				return false;
			}
			loader.setFileName(QDir::toNativeSeparators(
				QDir(system_root)
					.filePath(QStringLiteral("System32/vulkan-1.dll"))));
			if (!loader.load())
			{
				return false;
			}
		}

		// 直接从 Vulkan loader 解析函数，避免为测试引入 Vulkan 头文件依赖；
		// 结构体为 x64 ABI 的最小布局（仅用到的字段）。
		using PFN_vkCreateInstance
			= int (*)(const void *, const void *, void **);
		using PFN_vkEnumeratePhysicalDevices
			= int (*)(void *, quint32 *, void **);
		using PFN_vkDestroyInstance = void (*)(void *, const void *);

		const auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(
			loader.resolve("vkCreateInstance"));
		const auto enumerate_devices
			= reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
				loader.resolve("vkEnumeratePhysicalDevices"));
		const auto destroy_instance = reinterpret_cast<PFN_vkDestroyInstance>(
			loader.resolve("vkDestroyInstance"));
		if (create_instance == nullptr || enumerate_devices == nullptr
			|| destroy_instance == nullptr)
		{
			return false;
		}

		struct VkApplicationInfo
		{
			int m_s_type;
			const void *m_next;
			const char *m_application_name;
			quint32 m_application_version;
			const char *m_engine_name;
			quint32 m_engine_version;
			quint32 m_api_version;
		};
		struct VkInstanceCreateInfo
		{
			int m_s_type;
			const void *m_next;
			quint32 m_flags;
			const VkApplicationInfo *m_application_info;
			quint32 m_enabled_layer_count;
			const char *const *m_enabled_layer_names;
			quint32 m_enabled_extension_count;
			const char *const *m_enabled_extension_names;
		};

		VkApplicationInfo application_info = {};
		application_info.m_s_type = 0; // VK_STRUCTURE_TYPE_APPLICATION_INFO
		application_info.m_application_name = "resr-tests";
		application_info.m_api_version = (1u << 22); // VK_MAKE_VERSION(1,0,0)
		VkInstanceCreateInfo create_info = {};
		create_info.m_s_type = 1; // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
		create_info.m_application_info = &application_info;

		void *instance = nullptr;
		if (create_instance(&create_info, nullptr, &instance) != 0
			|| instance == nullptr)
		{
			return false;
		}
		quint32 device_count = 0;
		const int result = enumerate_devices(instance, &device_count, nullptr);
		destroy_instance(instance, nullptr);
		return result == 0 && device_count > 0;
	}();
	return available;
}
#endif // Q_OS_WIN

bool
engine_available()
{
	const QFileInfo engine(engine_executable());
	if (!engine.isFile() || !engine.isExecutable())
	{
		return false;
	}
#ifdef Q_OS_WIN
	if (!engine_tests_enabled())
	{
		return false;
	}
	return vulkan_gpu_available();
#else
	// bin/realesrgan 目前只随包提供 Windows PE（realesrgan-ncnn-vulkan.exe）；
	// Linux 上即使文件带执行位也无法启动，依赖引擎的链路用例属平台限制。
	return false;
#endif
}

QString
engine_unavailable_reason()
{
	const QFileInfo engine(engine_executable());
	if (!engine.isFile())
	{
		return QStringLiteral("bin/realesrgan 引擎不存在，跳过原生链路用例");
	}
#ifdef Q_OS_WIN
	if (!engine.isExecutable())
	{
		return QStringLiteral(
			"bin/realesrgan 引擎无执行权限，跳过原生链路用例");
	}
	if (!engine_tests_enabled())
	{
		return QStringLiteral(
			"未设置 RESR_TEST_ENGINE=1：真实 GPU 引擎用例默认跳过"
			"（ctest/CI 默认不跑引擎；显式运行时再设置并确保 GPU 环境稳定）");
	}
	if (!vulkan_gpu_available())
	{
		return QStringLiteral(
			"未检测到可用 Vulkan GPU 设备（vulkan-1.dll 或物理设备缺失），"
			"跳过真实引擎用例");
	}
	return QString();
#else
	// bin/realesrgan 目前只随包提供 Windows PE（realesrgan-ncnn-vulkan.exe）；
	// Linux 上无论是否带执行位都无法启动，依赖引擎的链路用例属平台限制。
	return QStringLiteral(
		"bin/realesrgan 引擎是 Windows PE，当前平台无法运行，跳过原生链路用例");
#endif
}

MediaSettings
image_settings(ModelType v_model, ScaleFactor v_scale)
{
	MediaSettings settings;
	settings.m_kind = MediaKind::IMAGE;
	settings.m_basic.m_model_type = v_model;
	settings.m_basic.m_scale_factor = v_scale;
	settings.m_advanced.m_output_format = OutputFormat::ORIGINAL;
	return settings;
}

MediaSettings
video_settings(ModelType v_model, ScaleFactor v_scale)
{
	MediaSettings settings;
	settings.m_kind = MediaKind::VIDEO;
	settings.m_basic.m_model_type = v_model;
	settings.m_basic.m_scale_factor = v_scale;
	settings.m_advanced.m_output_format = OutputFormat::ORIGINAL;
	settings.m_video_encoding = VideoEncoding::H264;
	settings.m_video_quality = VideoQuality::BALANCED;
	return settings;
}

// Linux 专用假引擎：随包 realesrgan 是 Windows PE，Linux 无法执行真实引擎；
// 用 POSIX shell 脚本验证「引擎输出暂存/提交」「停止后重启信号隔离」等
// 服务层行为。模式由 RESR_FAKE_ENGINE_MODE 控制（ok / truncate / slow）。
QString
make_fake_engine_bin(const QString &v_root)
{
	const QString bin = path_in_dir(v_root, QStringLiteral("fakebin"));
	QDir().mkpath(path_in_dir(bin, QStringLiteral("realesrgan/models")));
	const QString script = QStringLiteral(
		"#!/bin/sh\n"
		"in=\"\"; out=\"\"\n"
		"while [ $# -gt 0 ]; do\n"
		"  case \"$1\" in\n"
		"    -i) in=\"$2\"; shift 2;;\n"
		"    -o) out=\"$2\"; shift 2;;\n"
		"    *) shift;;\n"
		"  esac\n"
		"done\n"
		"mkdir -p \"$(dirname \"$out\")\"\n"
		"if [ -n \"${RESR_FAKE_ENGINE_LOG:-}\" ]; then "
		"echo \"call $in $(ls \"$in\"/*.png 2>/dev/null | wc -l)\" >> \"$RESR_FAKE_ENGINE_LOG\"; "
		"fi\n"
		"mode=\"${RESR_FAKE_ENGINE_MODE:-ok}\"\n"
		"if [ \"$mode\" = \"slow\" ]; then "
		"sleep \"${RESR_FAKE_ENGINE_SLEEP:-2}\"; fi\n"
		"if [ \"$mode\" = \"truncate\" ]; then : > \"$out\"; exit 0; fi\n"
		"if [ -d \"$in\" ]; then\n"
		"  mkdir -p \"$out\"\n"
		"  cp \"$in\"/*.png \"$out\"/ 2>/dev/null\n"
		"  exit 0\n"
		"fi\n"
		"cp \"$in\" \"$out\"\n"
		"exit 0\n");
	const QString exe = path_in_dir(
		bin, QStringLiteral("realesrgan/realesrgan-ncnn-vulkan.exe"));
	QFile file(exe);
	if (!file.open(QIODevice::WriteOnly))
	{
		return QString();
	}
	file.write(script.toUtf8());
	file.close();
	file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
						| QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther
						| QFile::ExeOther);
	return bin;
}

QByteArray
read_file_bytes(const QString &v_path)
{
	QFile file(v_path);
	if (!file.open(QIODevice::ReadOnly))
	{
		return QByteArray();
	}
	return file.readAll();
}

// 启动任务并泵事件循环直到 finished 或超时
JobOutcome
run_job(ProcessService &v_service, const ProcessRequest &v_request,
		int v_timeout_ms)
{
	JobOutcome outcome;

	const QMetaObject::Connection status_connection
		= QObject::connect(&v_service, &ProcessService::status_changed,
						   [&outcome](const ProcessStatus &v_status)
						   {
							   outcome.m_last_progress
								   = v_status.m_progress_percent;
							   outcome.m_last_state = v_status.m_state;
						   });
	const QMetaObject::Connection output_connection = QObject::connect(
		&v_service, &ProcessService::output_ready,
		[&outcome](const QString &v_path) { outcome.m_output = v_path; });
	const QMetaObject::Connection finished_connection
		= QObject::connect(&v_service, &ProcessService::finished,
						   [&outcome](bool v_success, const QString &v_message)
						   {
							   outcome.m_finished = true;
							   outcome.m_success = v_success;
							   outcome.m_message = v_message;
						   });

	outcome.m_start_accepted = v_service.start(v_request);

	QElapsedTimer timer;
	timer.start();
	while (!outcome.m_finished && timer.elapsed() < v_timeout_ms)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QTest::qSleep(10);
	}

	QObject::disconnect(status_connection);
	QObject::disconnect(output_connection);
	QObject::disconnect(finished_connection);

	if (!outcome.m_finished)
	{
		v_service.stop();
	}
	return outcome;
}

#ifdef Q_OS_WIN
// 进程是否仍然存活（用于验证销毁/停止后子进程不残留）。
bool
process_is_alive(DWORD v_pid)
{
	HANDLE handle = ::OpenProcess(
		SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, v_pid);
	if (handle == nullptr)
	{
		return false;
	}
	DWORD exit_code = 0;
	const bool alive = (::GetExitCodeProcess(handle, &exit_code) != 0)
					   && exit_code == STILL_ACTIVE;
	::CloseHandle(handle);
	return alive;
}
#endif

} // namespace

class PipelineTest : public QObject
{
	Q_OBJECT

  private slots:
	void
	initTestCase();
	void
	cleanupTestCase();

	void
	naming_rules();
	void
	batch_naming_rules();
	void
	input_scanner_rules();
	void
	output_parser_rules();
	void
	domain_parameter_rules();
	void
	output_path_directory_with_dot();
	void
	video_output_format_options();
	void
	batch_output_validation();
	void
	engine_command_arguments();
	void
	image_missing_input_fails();
	void
	image_output_staging();
	void
	batch_per_file_output_staging();
	void
	stop_then_restart_no_stale_signals();
	void
	video_worker_defers_language_switch();

	void
	image_single_native_smoke();
	void
	image_batch_native_smoke();
	void
	image_engine_crash_retry();
	void
	video_single_native_smoke();
	void
	video_engine_batches_frames();
	void
	stop_semantics();
	void
	engine_serialization_and_cleanup();
};

void
PipelineTest::initTestCase()
{
	// 隔离 QSettings（LanguageManager / AppSettings 等）：写到测试工作目录，
	// 避免污染真实用户配置。必须在任何 QSettings 写入之前设置。
	const QString config_dir
		= QDir(test_work_dir()).filePath(QStringLiteral("xdg-config"));
	QDir().mkpath(config_dir);
	qputenv("XDG_CONFIG_HOME", config_dir.toUtf8());
	QCoreApplication::setOrganizationName(QStringLiteral("real-esrguisoft"));
	QCoreApplication::setApplicationName(QStringLiteral("real-esrguisoft"));
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config_dir);
}

void
PipelineTest::cleanupTestCase()
{
	// 调试时可设置 RESR_KEEP_TEST_OUTPUT=1 保留临时产物（截图 / 超分结果）
	if (qEnvironmentVariableIsSet("RESR_KEEP_TEST_OUTPUT"))
	{
		qInfo("测试产物保留在：%s", qPrintable(test_work_dir()));
		return;
	}
	QDir(test_work_dir()).removeRecursively();
}

void
PipelineTest::naming_rules()
{
	// 用例不硬编码盘符路径：构造真实目录，让 QFileInfo 的父目录 / 后缀语义在
	// Windows 与 Linux 上一致（"C:/..." 在 Linux 上不是绝对路径，按 POSIX 解析
	// 会得到不同的父目录）。
	const QString input_dir = test_work_dir() + QStringLiteral("/naming_in");
	const QString output_dir = test_work_dir() + QStringLiteral("/naming_out");
	reset_dir(input_dir);
	reset_dir(output_dir);

	// 1) 单文件自动格式：{stem}_{输入扩展名}_{model}.{输入扩展名}
	const MediaSettings real_settings
		= image_settings(ModelType::REAL_ESRGAN_X4PLUS, ScaleFactor::SCALE4X);
	QCOMPARE(OutputPathResolver::resolve_single_output(
				 path_in_dir(input_dir, QStringLiteral("1.png")), real_settings,
				 QString()),
			 path_in_dir(input_dir, QStringLiteral("1_png_real.png")));

	// 2) TTA 后缀位于模型名之后、扩展名之前
	MediaSettings anime_tta = image_settings(
		ModelType::REAL_ESRGAN_X4PLUS_ANIME, ScaleFactor::SCALE4X);
	anime_tta.m_advanced.m_tta_mode = true;
	QCOMPARE(OutputPathResolver::resolve_single_output(
				 path_in_dir(input_dir, QStringLiteral("3.jpg")), anime_tta,
				 QString()),
			 path_in_dir(input_dir, QStringLiteral("3_jpg_anime_x.jpg")));

	// 3) 输入扩展名统一小写
	QCOMPARE(OutputPathResolver::resolve_single_output(
				 path_in_dir(input_dir, QStringLiteral("Photo.JPG")),
				 real_settings, QString()),
			 path_in_dir(input_dir, QStringLiteral("Photo_jpg_real.jpg")));

	// 4) BMP 输入自动转 PNG（引擎无法编码 bmp）
	QCOMPARE(OutputPathResolver::resolve_single_output(
				 path_in_dir(input_dir, QStringLiteral("a.bmp")), real_settings,
				 QString()),
			 path_in_dir(input_dir, QStringLiteral("a_bmp_real.png")));

	// 5) GIF 图片输入自动转 PNG
	QCOMPARE(OutputPathResolver::resolve_single_output(
				 path_in_dir(input_dir, QStringLiteral("a.gif")), real_settings,
				 QString()),
			 path_in_dir(input_dir, QStringLiteral("a_gif_real.png")));

	// 6) 无扩展名输入：不加 `_ext` 段，输出 png
	QCOMPARE(OutputPathResolver::resolve_single_output(
				 path_in_dir(input_dir, QStringLiteral("noext")), real_settings,
				 QString()),
			 path_in_dir(input_dir, QStringLiteral("noext_real.png")));

	// 7) 显式输出格式：扩展名取显式格式
	MediaSettings webp_settings = real_settings;
	webp_settings.m_advanced.m_output_format = OutputFormat::WEB_P;
	QCOMPARE(OutputPathResolver::resolve_single_output(
				 path_in_dir(input_dir, QStringLiteral("3.jpg")), webp_settings,
				 QString()),
			 path_in_dir(input_dir, QStringLiteral("3_jpg_real.webp")));

	// 8) 用户输出路径：目录 → 目录内默认名；带扩展名 → 视为完整文件路径
	QCOMPARE(OutputPathResolver::resolve_single_output(
				 path_in_dir(input_dir, QStringLiteral("1.png")), real_settings,
				 output_dir),
			 path_in_dir(output_dir, QStringLiteral("1_png_real.png")));
	QCOMPARE(OutputPathResolver::resolve_single_output(
				 path_in_dir(input_dir, QStringLiteral("1.png")), real_settings,
				 path_in_dir(output_dir, QStringLiteral("custom_name.png"))),
			 path_in_dir(output_dir, QStringLiteral("custom_name.png")));
}

void
PipelineTest::batch_naming_rules()
{
	// 同上：用真实测试目录代替 "C:/..." 盘符路径。
	const QString batch_root
		= test_work_dir() + QStringLiteral("/batch_naming");
	const QString images_dir
		= path_in_dir(batch_root, QStringLiteral("images"));
	const QString output_dir = path_in_dir(batch_root, QStringLiteral("out"));
	reset_dir(images_dir);
	reset_dir(output_dir);

	// 批量输出目录：{输入目录名}_{model}[_x]；未指定时位于输入目录的父目录
	const MediaSettings anime_settings = image_settings(
		ModelType::REAL_ESRGAN_X4PLUS_ANIME, ScaleFactor::SCALE2X);
	QCOMPARE(OutputPathResolver::resolve_batch_output_dir(
				 images_dir, anime_settings, QString()),
			 path_in_dir(batch_root, QStringLiteral("images_anime")));

	MediaSettings tta_settings = anime_settings;
	tta_settings.m_advanced.m_tta_mode = true;
	QCOMPARE(OutputPathResolver::resolve_batch_output_dir(
				 images_dir, tta_settings, QString()),
			 path_in_dir(batch_root, QStringLiteral("images_anime_x")));

	// 用户指定输出：一律作为父目录
	QCOMPARE(OutputPathResolver::resolve_batch_output_dir(
				 images_dir, tta_settings, output_dir),
			 path_in_dir(output_dir, QStringLiteral("images_anime_x")));
}

void
PipelineTest::input_scanner_rules()
{
	const QString dir = test_work_dir() + QStringLiteral("/scanner");
	reset_dir(dir);
	const QStringList names = { QStringLiteral("a.png"),
								QStringLiteral("b.JPG"),
								QStringLiteral("c.txt"),
								QStringLiteral("d.mp4"),
								QStringLiteral("e_mp4_anime_video.mp4"),
								QStringLiteral("f.MKV") };
	for (const QString &name : names)
	{
		QFile file(dir + QStringLiteral("/") + name);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("x");
		file.close();
	}

	const QStringList images = InputScanner::image_files(dir);
	QCOMPARE(images.size(), 2);
	QCOMPARE(QFileInfo(images.at(0)).fileName(), QStringLiteral("a.png"));
	QCOMPARE(QFileInfo(images.at(1)).fileName(), QStringLiteral("b.JPG"));

	// 视频批量排除「已处理」文件（_mp4_anime_video 后缀）
	const QStringList videos = InputScanner::video_files(
		dir, ModelType::REAL_ESR_ANIMEVIDEOV3, false);
	QCOMPARE(videos.size(), 2);
	QCOMPARE(QFileInfo(videos.at(0)).fileName(), QStringLiteral("d.mp4"));
	QCOMPARE(QFileInfo(videos.at(1)).fileName(), QStringLiteral("f.MKV"));

	// TTA 时已处理后缀带 _x
	QVERIFY(InputScanner::is_processed_video(
		QStringLiteral("C:/x/1_mp4_anime_video_x.mp4"),
		ModelType::REAL_ESR_ANIMEVIDEOV3, true));
	QVERIFY(!InputScanner::is_processed_video(
		QStringLiteral("C:/x/1_mp4_anime_video.mp4"),
		ModelType::REAL_ESR_ANIMEVIDEOV3, true));
}

void
PipelineTest::output_parser_rules()
{
	// 引擎进度在 stderr，形如 "16.67%"
	const ExeOutputParser::Result percent
		= ExeOutputParser::parse_line(QStringLiteral("16.67%"));
	QVERIFY(percent.m_progress.has_value());
	QCOMPARE(*percent.m_progress, 17);

	// GPU 枚举行忽略
	QVERIFY(!ExeOutputParser::parse_line(
				 QStringLiteral("[0 NVIDIA GeForce RTX]  queueC=2[8]"))
				 .m_progress.has_value());

	// 失败标记：退出码不可信，必须识别 decode / encode / _wfopen failed
	QCOMPARE(ExeOutputParser::parse_line(
				 QStringLiteral("encode image C:/a b/out.png failed"))
				 .m_failures.size(),
			 1);
	QCOMPARE(ExeOutputParser::parse_line(
				 QStringLiteral("decode image C:/中文 空格/x.jpg failed"))
				 .m_failures.size(),
			 1);
	QCOMPARE(ExeOutputParser::parse_line(
				 QStringLiteral("_wfopen C:/models/x.param failed"))
				 .m_failures.size(),
			 1);

	// 旧 Python 协议仍兼容
	const ExeOutputParser::Result legacy
		= ExeOutputParser::parse_line(QStringLiteral("PROGRESS:42.4"));
	QVERIFY(legacy.m_progress.has_value());
	QCOMPARE(*legacy.m_progress, 42);

	// 多行聚合：取最后一个进度 + 汇总失败
	const ExeOutputParser::Result aggregate = ExeOutputParser::parse(
		QStringLiteral("0.00%\n50.00%\n100.00%\nencode image x failed\n"));
	QVERIFY(aggregate.m_progress.has_value());
	QCOMPARE(*aggregate.m_progress, 100);
	QCOMPARE(aggregate.m_failures.size(), 1);
}

void
PipelineTest::domain_parameter_rules()
{
	// 质量档矩阵（与旧 Python video.GetEncodingParams 完全一致）
	const VideoEncodeParams h264_fast
		= video_encode_params(VideoEncoding::H264, VideoQuality::FAST);
	QCOMPARE(h264_fast.m_crf, 28);
	QCOMPARE(h264_fast.m_preset, QStringLiteral("ultrafast"));

	const VideoEncodeParams h265_quality
		= video_encode_params(VideoEncoding::H265, VideoQuality::QUALITY);
	QCOMPARE(h265_quality.m_crf, 18);
	QCOMPARE(h265_quality.m_preset, QStringLiteral("slow"));

	const VideoEncodeParams vp9_fast
		= video_encode_params(VideoEncoding::VP9, VideoQuality::FAST);
	QCOMPARE(vp9_fast.m_crf, 35);
	QCOMPARE(vp9_fast.m_preset, QStringLiteral("fast"));

	const VideoEncodeParams av1_balanced
		= video_encode_params(VideoEncoding::AV1, VideoQuality::BALANCED);
	QCOMPARE(av1_balanced.m_crf, 30);
	QCOMPARE(av1_balanced.m_preset, QStringLiteral("medium"));

	const VideoEncodeParams h264_balanced
		= video_encode_params(VideoEncoding::H264, VideoQuality::BALANCED);
	QCOMPARE(h264_balanced.m_crf, 23);
	QCOMPARE(h264_balanced.m_preset, QStringLiteral("medium"));

	// 模型默认倍数：动漫视频模型 2x，其余 4x
	QCOMPARE(model_default_scale(ModelType::REAL_ESR_ANIMEVIDEOV3), 2);
	QCOMPARE(model_default_scale(ModelType::REAL_ESRGAN_X4PLUS), 4);
	QCOMPARE(model_default_scale(ModelType::REAL_ESRGAN_X4PLUS_ANIME), 4);

	// 引擎可直接处理的图片格式
	QVERIFY(is_engine_image_format(QStringLiteral("png")));
	QVERIFY(is_engine_image_format(QStringLiteral(".PNG")));
	QVERIFY(!is_engine_image_format(QStringLiteral("bmp")));
	QVERIFY(!is_engine_image_format(QStringLiteral("gif")));
}

void
PipelineTest::output_path_directory_with_dot()
{
	// 阶段二修复：判定顺序改为「已存在目录 → 尾分隔符 → 有扩展名=文件 →
	// 其余=目录」。目录名带点（out.v1 / 2024.11）不能再被当成文件路径。
	const QString base = test_work_dir() + QStringLiteral("/dot-out");
	const QString dotted_dir = base + QStringLiteral("/out.v1");
	QDir().mkpath(dotted_dir);

	const MediaSettings image_settings_value
		= image_settings(ModelType::REAL_ESRGAN_X4PLUS, ScaleFactor::SCALE4X);
	QCOMPARE(
		OutputPathResolver::resolve_single_output(
			base + QStringLiteral("/a.png"), image_settings_value, dotted_dir),
		dotted_dir + QStringLiteral("/a_png_real.png"));

	const MediaSettings video_settings_value = video_settings(
		ModelType::REAL_ESR_ANIMEVIDEOV3, ScaleFactor::SCALE2X);
	QCOMPARE(
		OutputPathResolver::resolve_single_output(
			base + QStringLiteral("/1.mp4"), video_settings_value, dotted_dir),
		dotted_dir + QStringLiteral("/1_mp4_anime_video.mp4"));

	// 另存为语义不能破坏：不存在的带扩展名路径仍视为完整文件路径
	QCOMPARE(OutputPathResolver::resolve_single_output(
				 base + QStringLiteral("/a.png"), image_settings_value,
				 base + QStringLiteral("/name.webp")),
			 base + QStringLiteral("/name.webp"));
	// 不存在的无扩展名路径 → 目录
	QCOMPARE(OutputPathResolver::resolve_single_output(
				 base + QStringLiteral("/a.png"), image_settings_value,
				 base + QStringLiteral("/newdir")),
			 base + QStringLiteral("/newdir/a_png_real.png"));
	// 以分隔符结尾（目录尚不存在）→ 目录
	QCOMPARE(OutputPathResolver::resolve_single_output(
				 base + QStringLiteral("/a.png"), image_settings_value,
				 base + QStringLiteral("/newdir2/")),
			 base + QStringLiteral("/newdir2/a_png_real.png"));
	// 已存在的普通目录（无点）→ 目录
	QDir().mkpath(base + QStringLiteral("/plain"));
	QCOMPARE(OutputPathResolver::resolve_single_output(
				 base + QStringLiteral("/a.png"), image_settings_value,
				 base + QStringLiteral("/plain")),
			 base + QStringLiteral("/plain/a_png_real.png"));
}

void
PipelineTest::video_output_format_options()
{
	// 阶段二产品决策（方案 B）：视频输出下拉移除 gif（原生 libav 链路没有
	// GIF 编码器），但枚举/字符串映射保留用于旧配置兼容与输入识别。
	const QStringList video_names = video_output_format_names();
	QCOMPARE(video_names.size(), 6);
	QCOMPARE(video_names.first(), QStringLiteral("原格式"));
	QVERIFY(!video_names.contains(QStringLiteral("gif")));
	QVERIFY(video_names.contains(QStringLiteral("mp4")));
	QVERIFY(video_names.contains(QStringLiteral("webm")));

	QCOMPARE(string_to_output_format(QStringLiteral("gif")), OutputFormat::GIF);
	QCOMPARE(output_format_extension(OutputFormat::GIF), QStringLiteral("gif"));

	// 图片链路输出格式不受影响
	const QStringList image_names = image_output_format_names();
	QCOMPARE(image_names.size(), 4);
	QCOMPARE(image_names,
			 (QStringList{ QStringLiteral("原格式"), QStringLiteral("jpg"),
						   QStringLiteral("png"), QStringLiteral("webp") }));
}

void
PipelineTest::batch_output_validation()
{
	// 阶段二修复：批量图片成功判定改为「逐输入 + 目标扩展名」校验，并要求
	// 产物相对任务开始快照确实更新；输出目录里旧格式残留不能再被计入。
	const QString base
		= test_work_dir() + QStringLiteral("/batch-output-check");
	const QString output_dir = base + QStringLiteral("/out");
	QDir().mkpath(output_dir);

	const QString input_png = base + QStringLiteral("/a.png");
	const QString input_jpg = base + QStringLiteral("/b.jpg");
	const QString input_bmp = base + QStringLiteral("/c.bmp");
	const QString input_gif = base + QStringLiteral("/d.gif");

	const MediaSettings original
		= image_settings(ModelType::REAL_ESRGAN_X4PLUS, ScaleFactor::SCALE4X);
	MediaSettings explicit_jpg = original;
	explicit_jpg.m_advanced.m_output_format = OutputFormat::JPG;
	MediaSettings explicit_webp = original;
	explicit_webp.m_advanced.m_output_format = OutputFormat::WEB_P;

	// 原格式沿用输入扩展名（jpg/jpeg 互相兼容；bmp/gif 引擎不能编码 → png）
	QCOMPARE(OutputPathResolver::batch_output_file_names(input_png, original),
			 (QStringList{ QStringLiteral("a.png") }));
	QCOMPARE(
		OutputPathResolver::batch_output_file_names(input_jpg, original),
		(QStringList{ QStringLiteral("b.jpg"), QStringLiteral("b.jpeg") }));
	QCOMPARE(OutputPathResolver::batch_output_file_names(input_bmp, original),
			 (QStringList{ QStringLiteral("c.png") }));
	QCOMPARE(OutputPathResolver::batch_output_file_names(input_gif, original),
			 (QStringList{ QStringLiteral("d.png") }));
	// 显式格式统一用该格式扩展名
	QCOMPARE(
		OutputPathResolver::batch_output_file_names(input_png, explicit_jpg),
		(QStringList{ QStringLiteral("a.jpg"), QStringLiteral("a.jpeg") }));
	QCOMPARE(
		OutputPathResolver::batch_output_file_names(input_png, explicit_webp),
		(QStringList{ QStringLiteral("a.webp") }));

	const QStringList inputs = { input_png, input_jpg };
	const auto write_file = [](const QString &v_path, const QByteArray &v_data)
	{
		QFile file(v_path);
		if (!file.open(QIODevice::WriteOnly))
		{
			return false;
		}
		file.write(v_data);
		return true;
	};

	// 场景 1：输出目录里是上次「原格式」的 png 残留，本次改 jpg 重跑。
	// 修复前按「目录图片总数 == 输入数」会把 2 个残留 png 算成成功；
	// 修复后期望产物 a.jpg/b.jpg 缺失 → 2 个输入未就绪。
	QVERIFY(write_file(output_dir + QStringLiteral("/a.png"), "old"));
	QVERIFY(write_file(output_dir + QStringLiteral("/b.png"), "old"));
	const auto before = OutputPathResolver::snapshot_batch_outputs(
		inputs, explicit_jpg, output_dir);
	QCOMPARE(before.size(), 0); // jpg 期望产物都不存在
	QCOMPARE(OutputPathResolver::unresolved_batch_outputs(inputs, explicit_jpg,
														  output_dir, before)
				 .size(),
			 2);

	// 模拟引擎成功写出本次产物 → 全部就绪
	QVERIFY(write_file(output_dir + QStringLiteral("/a.jpg"), "new-a"));
	QVERIFY(write_file(output_dir + QStringLiteral("/b.jpg"), "new-b"));
	QVERIFY(OutputPathResolver::unresolved_batch_outputs(inputs, explicit_jpg,
														 output_dir, before)
				.isEmpty());

	// 场景 2：同名产物在任务开始前就存在且本次未被更新（引擎失败/未覆盖）。
	QVERIFY(write_file(output_dir + QStringLiteral("/a.jpg"), "stale"));
	QVERIFY(write_file(output_dir + QStringLiteral("/b.jpg"), "stale"));
	const auto stale_before = OutputPathResolver::snapshot_batch_outputs(
		inputs, explicit_jpg, output_dir);
	QCOMPARE(stale_before.size(), 2);
	QCOMPARE(OutputPathResolver::unresolved_batch_outputs(
				 inputs, explicit_jpg, output_dir, stale_before)
				 .size(),
			 2);

	// 模拟引擎真正覆盖（文件大小/时间变化）→ 恢复就绪
	QVERIFY(write_file(output_dir + QStringLiteral("/a.jpg"),
					   "stale-and-rewritten-a"));
	QVERIFY(write_file(output_dir + QStringLiteral("/b.jpg"),
					   "stale-and-rewritten-b"));
	QVERIFY(OutputPathResolver::unresolved_batch_outputs(
				inputs, explicit_jpg, output_dir, stale_before)
				.isEmpty());
}

void
PipelineTest::engine_command_arguments()
{
	MediaSettings settings = image_settings(ModelType::REAL_ESRGAN_X4PLUS_ANIME,
											ScaleFactor::SCALE4X);
	settings.m_advanced.m_tta_mode = true;
	settings.m_advanced.m_tile_size = QStringLiteral("512");
	settings.m_advanced.m_thread_count = QStringLiteral("1:2:2");

	const QStringList args = ExeCommand::upscale_arguments(
		QStringLiteral("C:/in/a.jpg"), QStringLiteral("C:/out/a_png.png"),
		QStringLiteral("realesrgan-x4plus-anime"), 4, QStringLiteral("png"),
		GPUDevice::AUTO, true, settings.m_advanced.m_tile_size,
		settings.m_advanced.m_thread_count,
		QStringLiteral("C:/bin/realesrgan/models"));

	QVERIFY(args.contains(QStringLiteral("-i")));
	QVERIFY(args.contains(QStringLiteral("-o")));
	QVERIFY(args.contains(QStringLiteral("-n")));
	QVERIFY(args.contains(QStringLiteral("realesrgan-x4plus-anime")));
	// 4 倍是引擎默认值，不传 -s（与 Python core.BuildRealesrganCommand 一致）
	QVERIFY(!args.contains(QStringLiteral("-s")));
	QVERIFY(args.contains(QStringLiteral("-f")));
	QVERIFY(args.contains(QStringLiteral("png")));
	QVERIFY(args.contains(QStringLiteral("-x")));
	QVERIFY(args.contains(QStringLiteral("-t")));
	QVERIFY(args.contains(QStringLiteral("512")));
	// 线程配置：合法 load:proc:save 必须落成 -j <value>（UI 控件不再是死设置）
	const int thread_index = args.indexOf(QStringLiteral("-j"));
	QVERIFY2(thread_index >= 0, "合法线程配置应输出 -j");
	QVERIFY(thread_index + 1 < args.size());
	QCOMPARE(args.at(thread_index + 1), QStringLiteral("1:2:2"));
	// 显式锁定模型目录
	QVERIFY(args.contains(QStringLiteral("-m")));
	// 自动 GPU 不传 -g
	QVERIFY(!args.contains(QStringLiteral("-g")));

	// task-34：图块「自动」必须显式落成 -t 256（不再省略交给引擎自动选择）
	const QStringList scale_args = ExeCommand::upscale_arguments(
		QStringLiteral("C:/in/a.jpg"), QStringLiteral("C:/out/a_png.png"),
		QStringLiteral("realesrgan-x4plus-anime"), 2, QString(),
		GPUDevice::GPU1, false, QStringLiteral("自动"));
	QVERIFY(scale_args.contains(QStringLiteral("-s")));
	QVERIFY(scale_args.contains(QStringLiteral("2")));
	QVERIFY(scale_args.contains(QStringLiteral("-g")));
	QVERIFY(scale_args.contains(QStringLiteral("1")));
	// auto 格式不传 -f
	QVERIFY(!scale_args.contains(QStringLiteral("-f")));
	const int auto_tile_index = scale_args.indexOf(QStringLiteral("-t"));
	QVERIFY2(auto_tile_index >= 0, "自动图块也必须显式传 -t");
	QVERIFY(auto_tile_index + 1 < scale_args.size());
	QCOMPARE(scale_args.at(auto_tile_index + 1), QString::number(256));
	// task-34：未提供线程配置时不再省略 -j，回退安全默认 1:1:2
	const int auto_thread_index = scale_args.indexOf(QStringLiteral("-j"));
	QVERIFY2(auto_thread_index >= 0, "空线程配置也必须显式传 -j");
	QVERIFY(auto_thread_index + 1 < scale_args.size());
	QCOMPARE(scale_args.at(auto_thread_index + 1), QStringLiteral("1:1:2"));

	// 非法线程配置回退安全默认 1:1:2（而不是省略后由引擎用 1:2:2），其它参数照旧
	const QStringList invalid_thread_args = ExeCommand::upscale_arguments(
		QStringLiteral("C:/in/a.jpg"), QStringLiteral("C:/out/a_png.png"),
		QStringLiteral("realesrgan-x4plus-anime"), 4, QString(),
		GPUDevice::AUTO, false, QStringLiteral("自动"), QStringLiteral("2:2"));
	const int invalid_thread_index
		= invalid_thread_args.indexOf(QStringLiteral("-j"));
	QVERIFY(invalid_thread_index >= 0);
	QVERIFY(invalid_thread_index + 1 < invalid_thread_args.size());
	QCOMPARE(invalid_thread_args.at(invalid_thread_index + 1),
			 QStringLiteral("1:1:2"));
	QVERIFY(invalid_thread_args.contains(QStringLiteral("-o")));

	// 空白线程配置同样回退 1:1:2
	const QStringList blank_thread_args = ExeCommand::upscale_arguments(
		QStringLiteral("C:/in/a.jpg"), QStringLiteral("C:/out/a_png.png"),
		QStringLiteral("realesrgan-x4plus-anime"), 4, QString(),
		GPUDevice::AUTO, false, QStringLiteral("自动"), QStringLiteral("   "));
	const int blank_thread_index
		= blank_thread_args.indexOf(QStringLiteral("-j"));
	QVERIFY(blank_thread_index >= 0);
	QVERIFY(blank_thread_index + 1 < blank_thread_args.size());
	QCOMPARE(blank_thread_args.at(blank_thread_index + 1),
			 QStringLiteral("1:1:2"));

	// task-34：显式图块值越界时 clamp 到 [32, 1024]；0/负数/非法文本回退 256
	const auto tile_value = [](const QString &v_tile) -> QString
	{
		const QStringList args = ExeCommand::upscale_arguments(
			QStringLiteral("C:/in/a.jpg"), QStringLiteral("C:/out/a.png"),
			QStringLiteral("m"), 4, QString(), GPUDevice::AUTO, false, v_tile);
		const int index = args.indexOf(QStringLiteral("-t"));
		return (index >= 0 && index + 1 < args.size()) ? args.at(index + 1)
													   : QString();
	};
	QCOMPARE(tile_value(QStringLiteral("10")), QStringLiteral("32"));
	QCOMPARE(tile_value(QStringLiteral("5000")), QStringLiteral("1024"));
	QCOMPARE(tile_value(QStringLiteral("0")), QStringLiteral("256"));
	QCOMPARE(tile_value(QStringLiteral("-8")), QStringLiteral("256"));
	QCOMPARE(tile_value(QStringLiteral("abc")), QStringLiteral("256"));
	QCOMPARE(tile_value(tile_size_auto_token()), QStringLiteral("256"));

	// task-34：崩溃降级重试参数覆盖为 -t 128 / -j 1:1:1（仅重试时使用）
	const QStringList retry_args = ExeCommand::upscale_arguments(
		QStringLiteral("C:/in/a.jpg"), QStringLiteral("C:/out/a.png"),
		QStringLiteral("m"), 4, QString(), GPUDevice::AUTO, false,
		QStringLiteral("自动"), QStringLiteral("1:2:2"), QString(), true);
	const int retry_tile_index = retry_args.indexOf(QStringLiteral("-t"));
	const int retry_thread_index = retry_args.indexOf(QStringLiteral("-j"));
	QVERIFY(retry_tile_index >= 0 && retry_tile_index + 1 < retry_args.size());
	QVERIFY(retry_thread_index >= 0
			&& retry_thread_index + 1 < retry_args.size());
	QCOMPARE(retry_args.at(retry_tile_index + 1), QString::number(128));
	QCOMPARE(retry_args.at(retry_thread_index + 1), QStringLiteral("1:1:1"));
}

void
PipelineTest::image_missing_input_fails()
{
	MediaSettings settings = image_settings(ModelType::REAL_ESRGAN_X4PLUS_ANIME,
											ScaleFactor::SCALE2X);
	ProcessRequest request;
	request.m_kind = MediaKind::IMAGE;
	request.m_mode = ProcessMode::SINGLE_FILE;
	request.m_input_path = test_work_dir() + QStringLiteral("/missing.png");
	request.m_settings = settings;
	request.m_temp_directory = test_work_dir() + QStringLiteral("/temp");

	ProcessService service;
	const JobOutcome outcome = run_job(service, request, 10000);
	QVERIFY(outcome.m_finished);
	QVERIFY(!outcome.m_success);
	QVERIFY(outcome.m_message.contains(QStringLiteral("处理失败")));

	// 回归（task-31）：子服务同步失败时 start() 必须返回 false，不能对外
	// 报告「已启动」（否则控制器会打印 "Process started" 且调用方误以为在跑）。
	QVERIFY(!outcome.m_start_accepted);
	QVERIFY(!service.is_busy());

	// 回归：失败原因要能被 current_status() 重建（语言切换重下发时不能退化
	// 成通用的「处理失败」）。
	QVERIFY2(service.current_status().m_message.contains(
				 QStringLiteral("输入文件不存在")),
			 qPrintable(service.current_status().m_message));
}

void
PipelineTest::image_output_staging()
{
#ifdef Q_OS_WIN
	QSKIP(
		"假引擎为 POSIX shell，仅 Linux 运行（平台限制；真实引擎用例默认不跑）");
#endif
	const QString root = test_work_dir() + QStringLiteral("/staging");
	reset_dir(root);
	const QString bin = make_fake_engine_bin(root);
	QVERIFY(!bin.isEmpty());

	const QString input = path_in_dir(root, QStringLiteral("1.png"));
	QImage image(16, 16, QImage::Format_RGB32);
	image.fill(Qt::red);
	QVERIFY(image.save(input, "PNG"));

	const QString output_root = path_in_dir(root, QStringLiteral("out"));
	QDir().mkpath(output_root);
	const QString final_path
		= path_in_dir(output_root, QStringLiteral("1_png_real.png"));
	{
		QFile file(final_path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("OLD-PRODUCT");
	}
	const QString staged_path
		= path_in_dir(output_root, QStringLiteral(".1_png_real.resr-part.png"));

	ProcessRequest request;
	request.m_kind = MediaKind::IMAGE;
	request.m_mode = ProcessMode::SINGLE_FILE;
	request.m_input_path = input;
	request.m_output_path = output_root;
	request.m_settings
		= image_settings(ModelType::REAL_ESRGAN_X4PLUS, ScaleFactor::SCALE4X);
	request.m_temp_directory = path_in_dir(root, QStringLiteral("temp"));

	// 1) 引擎“成功退出但产物被截断/为空” → 任务失败，且旧成品保持原样
	//    （修复前引擎直接写最终路径，会把旧成品截断成 0 字节）。
	qputenv("RESR_FAKE_ENGINE_MODE", "truncate");
	{
		ProcessService service;
		service.set_exe_directory(bin);
		const JobOutcome outcome = run_job(service, request, 20000);
		QVERIFY(outcome.m_finished);
		QVERIFY2(!outcome.m_success, qPrintable(outcome.m_message));
		QCOMPARE(read_file_bytes(final_path), QByteArray("OLD-PRODUCT"));
		QVERIFY(!QFileInfo::exists(staged_path));
	}

	// 2) 正常成功 → 提交临时产物到最终路径，且不残留 .resr-part 文件。
	qputenv("RESR_FAKE_ENGINE_MODE", "ok");
	{
		ProcessService service;
		service.set_exe_directory(bin);
		const JobOutcome outcome = run_job(service, request, 20000);
		QVERIFY2(outcome.m_success, qPrintable(outcome.m_message));
		QVERIFY(QFileInfo(final_path).isFile());
		QCOMPARE(read_file_bytes(final_path), read_file_bytes(input));
		QVERIFY(!QFileInfo::exists(staged_path));
	}
	qunsetenv("RESR_FAKE_ENGINE_MODE");
}

void
PipelineTest::batch_per_file_output_staging()
{
#ifdef Q_OS_WIN
	QSKIP(
		"假引擎为 POSIX shell，仅 Linux 运行（平台限制；真实引擎用例默认不跑）");
#endif
	const QString root = test_work_dir() + QStringLiteral("/staging-batch");
	reset_dir(root);
	const QString bin = make_fake_engine_bin(root);
	QVERIFY(!bin.isEmpty());

	// bmp 迫使整批走「逐文件 + QImage 预转 PNG」分支（覆盖逐文件提交路径）。
	const QString input_dir = path_in_dir(root, QStringLiteral("batch_in"));
	QDir().mkpath(input_dir);
	QImage image(16, 16, QImage::Format_RGB32);
	image.fill(Qt::blue);
	QVERIFY(image.save(path_in_dir(input_dir, QStringLiteral("a.png")), "PNG"));
	QVERIFY(image.save(path_in_dir(input_dir, QStringLiteral("b.bmp")), "BMP"));

	ProcessRequest request;
	request.m_kind = MediaKind::IMAGE;
	request.m_mode = ProcessMode::BATCH_PROCESS;
	request.m_input_path = input_dir;
	request.m_settings
		= image_settings(ModelType::REAL_ESRGAN_X4PLUS, ScaleFactor::SCALE4X);
	request.m_temp_directory = path_in_dir(root, QStringLiteral("temp"));

	qputenv("RESR_FAKE_ENGINE_MODE", "ok");
	ProcessService service;
	service.set_exe_directory(bin);
	const JobOutcome outcome = run_job(service, request, 30000);
	qunsetenv("RESR_FAKE_ENGINE_MODE");

	QVERIFY2(outcome.m_success, qPrintable(outcome.m_message));
	const QString output_dir
		= path_in_dir(root, QStringLiteral("batch_in_real"));
	QVERIFY(
		QFileInfo(path_in_dir(output_dir, QStringLiteral("a.png"))).isFile());
	QVERIFY(
		QFileInfo(path_in_dir(output_dir, QStringLiteral("b.png"))).isFile());
	// 不残留 .resr-part 暂存文件。
	const QStringList leftovers
		= QDir(output_dir)
			  .entryList(QStringList{ QStringLiteral(".resr-part*"),
									  QStringLiteral(".*.resr-part*") },
						 QDir::Files | QDir::Hidden);
	QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(',')));
}

void
PipelineTest::stop_then_restart_no_stale_signals()
{
#ifdef Q_OS_WIN
	QSKIP(
		"假引擎为 POSIX shell，仅 Linux 运行（平台限制；真实引擎用例默认不跑）");
#endif
	const QString root = test_work_dir() + QStringLiteral("/restart");
	reset_dir(root);
	const QString bin = make_fake_engine_bin(root);
	QVERIFY(!bin.isEmpty());

	const QString input = path_in_dir(root, QStringLiteral("1.png"));
	QImage image(16, 16, QImage::Format_RGB32);
	image.fill(Qt::green);
	QVERIFY(image.save(input, "PNG"));
	const QString output_root = path_in_dir(root, QStringLiteral("out"));
	QDir().mkpath(output_root);

	ProcessRequest request;
	request.m_kind = MediaKind::IMAGE;
	request.m_mode = ProcessMode::SINGLE_FILE;
	request.m_input_path = input;
	request.m_output_path = output_root;
	request.m_settings
		= image_settings(ModelType::REAL_ESRGAN_X4PLUS, ScaleFactor::SCALE4X);
	request.m_temp_directory = path_in_dir(root, QStringLiteral("temp"));

	ProcessService service;
	service.set_exe_directory(bin);

	int finished_count = 0;
	bool finished_success = false;
	QStringList messages_after_restart;
	bool capturing = false;
	const QMetaObject::Connection status_connection
		= QObject::connect(&service, &ProcessService::status_changed,
						   [&](const ProcessStatus &v_status)
						   {
							   if (capturing && !v_status.m_message.isEmpty())
							   {
								   messages_after_restart << v_status.m_message;
							   }
						   });
	const QMetaObject::Connection finished_connection
		= QObject::connect(&service, &ProcessService::finished,
						   [&](bool v_success, const QString &)
						   {
							   ++finished_count;
							   finished_success = v_success;
						   });

	// 1) 长任务中停止：取消不算失败（不发 finished）。
	qputenv("RESR_FAKE_ENGINE_MODE", "slow");
	qputenv("RESR_FAKE_ENGINE_SLEEP", "3");
	QVERIFY(service.start(request));
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < 5000 && service.state() != ProcessState::RUNNING)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QTest::qSleep(10);
	}
	QTest::qSleep(300); // 让假引擎真正启动
	service.stop();
	timer.restart();
	while (timer.elapsed() < 10000 && service.is_busy())
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QTest::qSleep(10);
	}
	QCOMPARE(service.state(), ProcessState::IDLE);
	QCOMPARE(finished_count, 0);
	QVERIFY(!service.is_busy());

	// 2) 立即重启：旧任务的迟到信号不得污染新任务（不能出现失败/取消文案）。
	qputenv("RESR_FAKE_ENGINE_MODE", "ok");
	capturing = true;
	QVERIFY(service.start(request));
	timer.restart();
	while (timer.elapsed() < 20000 && finished_count == 0)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QTest::qSleep(10);
	}
	capturing = false;
	qunsetenv("RESR_FAKE_ENGINE_MODE");
	qunsetenv("RESR_FAKE_ENGINE_SLEEP");

	QCOMPARE(finished_count, 1);
	QVERIFY2(finished_success, qPrintable(service.current_status().m_message));
	const QString joined = messages_after_restart.join(QStringLiteral(" | "));
	QVERIFY2(!joined.contains(QStringLiteral("处理失败")), qPrintable(joined));
	QVERIFY2(!joined.contains(QStringLiteral("已取消")), qPrintable(joined));

	QObject::disconnect(status_connection);
	QObject::disconnect(finished_connection);
}

void
PipelineTest::video_worker_defers_language_switch()
{
#ifdef Q_OS_WIN
	QSKIP(
		"假引擎为 POSIX shell，仅 Linux 运行（平台限制；真实引擎用例默认不跑）");
#endif
	// 集成验证 task-31 的翻译器延迟策略（P2-2 数据竞争修复）：视频 worker
	// 线程运行期间 set_language() 只持久化选择、不装卸 translator、不广播；
	// worker 结束后（on_worker_finished）才应用并广播一次。
	const QString root = test_work_dir() + QStringLiteral("/video-defer");
	reset_dir(root);
	const QString bin = make_fake_engine_bin(root);
	QVERIFY(!bin.isEmpty());
	// 视频预检要求模型文件存在：伪造 animevideo 2x 模型占位文件。
	const QString models_dir
		= path_in_dir(bin, QStringLiteral("realesrgan/models"));
	for (const QString &name :
		 { QStringLiteral("realesr-animevideov3-x2.param"),
		   QStringLiteral("realesr-animevideov3-x2.bin") })
	{
		QFile model(path_in_dir(models_dir, name));
		QVERIFY(model.open(QIODevice::WriteOnly));
		model.write("stub");
	}

	const QString input_video
		= project_root() + QStringLiteral("/input/video/1.mp4");
	QVERIFY2(QFileInfo::exists(input_video), "缺少测试素材 input/video/1.mp4");
	const QString output_dir = path_in_dir(root, QStringLiteral("out"));
	QDir().mkpath(output_dir);

	ProcessRequest request;
	request.m_kind = MediaKind::VIDEO;
	request.m_mode = ProcessMode::SINGLE_FILE;
	request.m_input_path = input_video;
	request.m_output_path = output_dir;
	request.m_settings = video_settings(ModelType::REAL_ESR_ANIMEVIDEOV3,
										ScaleFactor::SCALE2X);
	request.m_temp_directory = path_in_dir(root, QStringLiteral("temp"));
	request.m_max_video_frames = 4;

	ProcessService service;
	service.set_exe_directory(bin);

	bool finished = false;
	bool finished_success = false;
	const QMetaObject::Connection finished_connection
		= QObject::connect(&service, &ProcessService::finished,
						   [&](bool v_success, const QString &)
						   {
							   finished = true;
							   finished_success = v_success;
						   });

	LanguageManager &language_manager = LanguageManager::instance();
	// 归位到中文，避免默认「跟随系统」影响信号计数。
	language_manager.set_language(LanguageManager::Language::ZH_CN);

	int language_signals = 0;
	const QMetaObject::Connection language_connection = QObject::connect(
		&language_manager, &LanguageManager::language_changed,
		[&language_signals]() { ++language_signals; });

	// 慢引擎保证调用 set_language 时 worker 仍在运行。
	qputenv("RESR_FAKE_ENGINE_MODE", "slow");
	qputenv("RESR_FAKE_ENGINE_SLEEP", "2");
	QVERIFY(service.start(request));
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < 5000 && service.state() != ProcessState::RUNNING)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QTest::qSleep(10);
	}
	QTest::qSleep(300); // worker 已进入拆帧/引擎阶段
	QVERIFY(service.is_busy());

	// 暂停 → 恢复：恢复后的状态文案不能停留在「已暂停」（task-31 发现
	// publish 的显式文案会 sticky 到下一状态）。
	service.pause();
	timer.restart();
	while (timer.elapsed() < 5000 && service.state() != ProcessState::PAUSED)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QTest::qSleep(10);
	}
	QCOMPARE(service.state(), ProcessState::PAUSED);
	QCOMPARE(service.current_status().m_message, QStringLiteral("已暂停"));
	service.resume();
	timer.restart();
	while (timer.elapsed() < 5000 && service.state() != ProcessState::RUNNING)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QTest::qSleep(10);
	}
	QCOMPARE(service.state(), ProcessState::RUNNING);
	QVERIFY2(
		!service.current_status().m_message.contains(QStringLiteral("已暂停")),
		qPrintable(service.current_status().m_message));

	language_manager.set_language(LanguageManager::Language::EN);
	QCOMPARE(language_signals, 0);
	QCOMPARE(static_cast<int>(language_manager.language()),
			 static_cast<int>(LanguageManager::Language::EN));

	// 任务结束后释放延迟：应用一次并广播一次。
	timer.restart();
	while (timer.elapsed() < 120000 && !finished)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QTest::qSleep(10);
	}
	qunsetenv("RESR_FAKE_ENGINE_MODE");
	qunsetenv("RESR_FAKE_ENGINE_SLEEP");

	QVERIFY2(finished, "视频任务未在超时内结束");
	QVERIFY2(finished_success, qPrintable(service.current_status().m_message));
	QCOMPARE(language_signals, 1);

	QObject::disconnect(finished_connection);
	QObject::disconnect(language_connection);
	language_manager.set_language(LanguageManager::Language::ZH_CN);
}

void
PipelineTest::image_single_native_smoke()
{
	if (!engine_available())
	{
		QSKIP(qPrintable(engine_unavailable_reason()));
	}

	const QString root = project_root();
	const QString input_file = root + QStringLiteral("/input/3.jpg");
	QVERIFY2(QFileInfo::exists(input_file), "缺少测试素材 input/3.jpg");

	const QString out_dir = test_work_dir() + QStringLiteral("/image_single");
	reset_dir(out_dir);

	const QImage source(input_file);
	QVERIFY2(!source.isNull(), "源图片无法加载");

	ProcessRequest request;
	request.m_kind = MediaKind::IMAGE;
	request.m_mode = ProcessMode::SINGLE_FILE;
	request.m_input_path = input_file;
	request.m_output_path = out_dir;
	request.m_settings = image_settings(ModelType::REAL_ESRGAN_X4PLUS_ANIME,
										ScaleFactor::SCALE2X);
	request.m_temp_directory = test_work_dir() + QStringLiteral("/temp");

	ProcessService service;
	const JobOutcome outcome = run_job(service, request, 120000);

	QVERIFY2(outcome.m_success, qPrintable(outcome.m_message));
	QCOMPARE(outcome.m_last_progress, 100);

	const QString expected = out_dir + QStringLiteral("/3_jpg_anime.jpg");
	QCOMPARE(QDir::cleanPath(outcome.m_output), QDir::cleanPath(expected));
	QVERIFY(QFileInfo(expected).isFile());
	QVERIFY(QFileInfo(expected).size() > 0);

	const QImage result(expected);
	QVERIFY2(!result.isNull(), "输出图片无法加载");
	QCOMPARE(result.width(), source.width() * 2);
	QCOMPARE(result.height(), source.height() * 2);
}

void
PipelineTest::image_batch_native_smoke()
{
	if (!engine_available())
	{
		QSKIP(qPrintable(engine_unavailable_reason()));
	}

	const QString root = project_root();
	const QString batch_in = test_work_dir() + QStringLiteral("/batch_in");
	const QString batch_out = test_work_dir() + QStringLiteral("/batch_out");
	reset_dir(batch_in);
	reset_dir(batch_out);
	QVERIFY(QFile::copy(root + QStringLiteral("/input/3.jpg"),
						batch_in + QStringLiteral("/3.jpg")));
	QVERIFY(QFile::copy(root + QStringLiteral("/input/5.jpg"),
						batch_in + QStringLiteral("/5.jpg")));

	ProcessRequest request;
	request.m_kind = MediaKind::IMAGE;
	request.m_mode = ProcessMode::BATCH_PROCESS;
	request.m_input_path = batch_in;
	request.m_output_path = batch_out;
	request.m_settings = image_settings(ModelType::REAL_ESRGAN_X4PLUS_ANIME,
										ScaleFactor::SCALE2X);
	request.m_temp_directory = test_work_dir() + QStringLiteral("/temp");

	ProcessService service;
	const JobOutcome outcome = run_job(service, request, 180000);

	QVERIFY2(outcome.m_success, qPrintable(outcome.m_message));
	QCOMPARE(outcome.m_last_progress, 100);

	// 批量输出目录：{输入目录名}_{model}；目录模式无 -f 时引擎统一输出 PNG
	const QString expected_dir = batch_out + QStringLiteral("/batch_in_anime");
	QCOMPARE(QDir::cleanPath(outcome.m_output), QDir::cleanPath(expected_dir));
	QVERIFY(QFileInfo(expected_dir).isDir());

	const QStringList outputs
		= QDir(expected_dir)
			  .entryList(QStringList{ QStringLiteral("*.png") }, QDir::Files);
	QCOMPARE(outputs.size(), 2);
	QVERIFY(outputs.contains(QStringLiteral("3.png")));
	QVERIFY(outputs.contains(QStringLiteral("5.png")));
}

void
PipelineTest::image_engine_crash_retry()
{
#ifndef Q_OS_WIN
	QSKIP("0xC0000409 崩溃退出码仅 Windows 可产生；Linux 用 ok/truncate "
		  "假引擎覆盖服务层失败路径");
#else
	// 假引擎：首次启动以 0xC0000409 崩溃；状态文件存在后复制输入并成功退出。
	// 回归验证 ImageProcessService 的崩溃降级重试：只重试一次，且重试参数
	// 必须是 -t 128 / -j 1:1:1（静态审查 B2）。
	const QString helper_exe
		= QDir(QCoreApplication::applicationDirPath())
			  .filePath(QStringLiteral("resr_fake_engine_crash.exe"));
	if (!QFileInfo::exists(helper_exe))
	{
		QSKIP(
			"未找到 resr_fake_engine_crash.exe（Windows 测试辅助程序未构建）");
	}

	const QString root
		= test_work_dir() + QStringLiteral("/engine-crash-retry");
	reset_dir(root);
	const QString bin = path_in_dir(root, QStringLiteral("fakebin"));
	if (!QDir().mkpath(path_in_dir(bin, QStringLiteral("realesrgan/models"))))
	{
		QFAIL("无法创建假引擎目录布局");
	}
	const QString engine_path = path_in_dir(
		bin, QStringLiteral("realesrgan/realesrgan-ncnn-vulkan.exe"));
	QVERIFY2(QFile::copy(helper_exe, engine_path), "复制假引擎失败");

	const QString input_file = project_root() + QStringLiteral("/input/3.jpg");
	QVERIFY2(QFileInfo::exists(input_file), "缺少测试素材 input/3.jpg");
	const QString out_dir = path_in_dir(root, QStringLiteral("out"));
	QDir().mkpath(out_dir);

	const QString call_log
		= path_in_dir(root, QStringLiteral("engine-calls.log"));
	const QString state_file = path_in_dir(root, QStringLiteral("crash-state"));
	qputenv("RESR_FAKE_ENGINE_LOG", call_log.toUtf8());
	qputenv("RESR_FAKE_ENGINE_STATE", state_file.toUtf8());

	ProcessRequest request;
	request.m_kind = MediaKind::IMAGE;
	request.m_mode = ProcessMode::SINGLE_FILE;
	request.m_input_path = input_file;
	request.m_output_path = out_dir;
	request.m_settings = image_settings(ModelType::REAL_ESRGAN_X4PLUS_ANIME,
										ScaleFactor::SCALE2X);
	request.m_temp_directory = test_work_dir() + QStringLiteral("/temp");

	ProcessService service;
	service.set_exe_directory(bin);
	const JobOutcome outcome = run_job(service, request, 120000);
	qunsetenv("RESR_FAKE_ENGINE_LOG");
	qunsetenv("RESR_FAKE_ENGINE_STATE");

	QVERIFY2(outcome.m_success, qPrintable(outcome.m_message));
	QCOMPARE(outcome.m_last_progress, 100);

	QFile log_file(call_log);
	QVERIFY(log_file.open(QIODevice::ReadOnly | QIODevice::Text));
	const QStringList calls = QString::fromUtf8(log_file.readAll())
								  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
	QCOMPARE(calls.size(), 2); // 首次崩溃 + 仅一次降级重试
	QVERIFY2(
		calls.at(0).contains(QStringLiteral("-t"))
			&& calls.at(0).contains(QStringLiteral("256")),
		qPrintable(QStringLiteral("首次调用参数异常: %1").arg(calls.at(0))));
	QVERIFY2(
		calls.at(0).contains(QStringLiteral("-j"))
			&& calls.at(0).contains(QStringLiteral("1:1:2")),
		qPrintable(QStringLiteral("首次调用参数异常: %1").arg(calls.at(0))));
	QVERIFY2(
		calls.at(1).contains(QStringLiteral("-t"))
			&& calls.at(1).contains(QStringLiteral("128")),
		qPrintable(
			QStringLiteral("重试参数未使用安全 tile: %1").arg(calls.at(1))));
	QVERIFY2(
		calls.at(1).contains(QStringLiteral("-j"))
			&& calls.at(1).contains(QStringLiteral("1:1:1")),
		qPrintable(
			QStringLiteral("重试参数未使用安全线程配置: %1").arg(calls.at(1))));

	const QString expected = out_dir + QStringLiteral("/3_jpg_anime.jpg");
	QVERIFY2(QFileInfo::exists(expected), "重试成功后缺少输出文件");
	QVERIFY(QFileInfo(expected).size() > 0);
#endif
}

void
PipelineTest::video_single_native_smoke()
{
	if (!engine_available())
	{
		QSKIP(qPrintable(engine_unavailable_reason()));
	}

	const QString root = project_root();
	const QString input_file = root + QStringLiteral("/input/video/1.mp4");
	QVERIFY2(QFileInfo::exists(input_file), "缺少测试素材 input/video/1.mp4");

	const QString out_dir = test_work_dir() + QStringLiteral("/video_single");
	reset_dir(out_dir);

	const MediaProbeResult source_probe = MediaProbe::probe(input_file);
	QVERIFY2(source_probe.m_ok, qPrintable(source_probe.m_error.m_message));

	ProcessRequest request;
	request.m_kind = MediaKind::VIDEO;
	request.m_mode = ProcessMode::SINGLE_FILE;
	request.m_input_path = input_file;
	request.m_output_path = out_dir;
	request.m_settings = video_settings(ModelType::REAL_ESR_ANIMEVIDEOV3,
										ScaleFactor::SCALE2X);
	// 只处理前 12 帧，保证冒烟测试耗时可控
	request.m_max_video_frames = 12;
	request.m_temp_directory = test_work_dir() + QStringLiteral("/temp");

	ProcessService service;
	const JobOutcome outcome = run_job(service, request, 300000);

	QVERIFY2(outcome.m_success, qPrintable(outcome.m_message));
	QCOMPARE(outcome.m_last_progress, 100);

	const QString expected = out_dir + QStringLiteral("/1_mp4_anime_video.mp4");
	QCOMPARE(QDir::cleanPath(outcome.m_output), QDir::cleanPath(expected));
	QVERIFY(QFileInfo(expected).isFile());
	QVERIFY(QFileInfo(expected).size() > 0);

	const MediaProbeResult result_probe = MediaProbe::probe(expected);
	QVERIFY2(result_probe.m_ok, qPrintable(result_probe.m_error.m_message));
	QVERIFY(result_probe.m_info.m_has_video);
	QCOMPARE(result_probe.m_info.m_video.m_width,
			 source_probe.m_info.m_video.m_width * 2);
	QCOMPARE(result_probe.m_info.m_video.m_height,
			 source_probe.m_info.m_video.m_height * 2);
	// 原音频应被保留（素材含 AAC 音轨）
	QCOMPARE(result_probe.m_info.m_has_audio, source_probe.m_info.m_has_audio);
}

void
PipelineTest::video_engine_batches_frames()
{
#ifdef Q_OS_WIN
	QSKIP(
		"假引擎为 POSIX shell，仅 Linux 运行；Windows 分批覆盖待补（真实引擎用例默认不跑）");
#endif
	const QString root = test_work_dir() + QStringLiteral("/video_batching");
	reset_dir(root);
	const QString bin = make_fake_engine_bin(root);
	QVERIFY(!bin.isEmpty());
	// 视频链路启动引擎前会校验模型文件；假引擎目录补一份占位模型。
	const QString models_dir
		= QDir(bin).filePath(QStringLiteral("realesrgan/models"));
	QVERIFY(QDir().mkpath(models_dir));
	for (const QString &suffix :
		 { QStringLiteral("param"), QStringLiteral("bin") })
	{
		QFile model(
			QDir(models_dir)
				.filePath(QStringLiteral("realesr-animevideov3-x2.") + suffix));
		QVERIFY(model.open(QIODevice::WriteOnly));
		model.write("fake-model");
	}

	const QString input_file
		= project_root() + QStringLiteral("/input/video/1.mp4");
	QVERIFY2(QFileInfo::exists(input_file), "缺少测试素材 input/video/1.mp4");

	const QString out_dir = path_in_dir(root, QStringLiteral("out"));
	QDir().mkpath(out_dir);
	const QString call_log
		= path_in_dir(root, QStringLiteral("engine-calls.log"));
	qputenv("RESR_FAKE_ENGINE_MODE", "ok");
	qputenv("RESR_FAKE_ENGINE_LOG", call_log.toUtf8());

	ProcessRequest request;
	request.m_kind = MediaKind::VIDEO;
	request.m_mode = ProcessMode::SINGLE_FILE;
	request.m_input_path = input_file;
	request.m_output_path = out_dir;
	request.m_settings = video_settings(ModelType::REAL_ESR_ANIMEVIDEOV3,
										ScaleFactor::SCALE2X);
	// 120 帧按 50 帧/批 => 必须启动 3 次引擎；旧 Python 版同款分批语义。
	request.m_max_video_frames = 120;
	request.m_temp_directory = path_in_dir(root, QStringLiteral("temp"));

	ProcessService service;
	service.set_exe_directory(bin);
	const JobOutcome outcome = run_job(service, request, 180000);
	qunsetenv("RESR_FAKE_ENGINE_MODE");
	qunsetenv("RESR_FAKE_ENGINE_LOG");

	QVERIFY2(outcome.m_success, qPrintable(outcome.m_message));
	QCOMPARE(outcome.m_last_progress, 100);

	QFile log_file(call_log);
	QVERIFY(log_file.open(QIODevice::ReadOnly | QIODevice::Text));
	const QStringList calls = QString::fromUtf8(log_file.readAll())
								  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
	QCOMPARE(calls.size(), 3);
	// 每批输入目录必须对应 50/50/20 帧，且顺序为 batch_0/1/2（静态审查 C1：
	// 只断言“调用 3 次”不足以发现切分/复制错误）。
	const QList<int> expected_batch_sizes{ 50, 50, 20 };
	for (int index = 0; index < calls.size(); ++index)
	{
		QVERIFY2(
			calls.at(index).contains(QStringLiteral("batch_%1").arg(index)),
			qPrintable(QStringLiteral("第 %1 批输入目录异常: %2")
						   .arg(index)
						   .arg(calls.at(index))));
		const QStringList fields
			= calls.at(index).split(QLatin1Char(' '), Qt::SkipEmptyParts);
		bool ok = false;
		const int frame_count
			= fields.isEmpty() ? -1 : fields.last().toInt(&ok);
		QVERIFY2(ok && frame_count == expected_batch_sizes.at(index),
				 qPrintable(QStringLiteral("第 %1 批帧数异常（期望 %2）: %3")
								.arg(index)
								.arg(expected_batch_sizes.at(index))
								.arg(calls.at(index))));
	}

	const QString expected = out_dir + QStringLiteral("/1_mp4_anime_video.mp4");
	QCOMPARE(QDir::cleanPath(outcome.m_output), QDir::cleanPath(expected));
	QVERIFY(QFileInfo(expected).isFile());
	QVERIFY(QFileInfo(expected).size() > 0);
}

void
PipelineTest::stop_semantics()
{
	if (!engine_available())
	{
		QSKIP(qPrintable(engine_unavailable_reason()));
	}

	const QString root = project_root();
	const QString out_dir = test_work_dir() + QStringLiteral("/stop_case");
	reset_dir(out_dir);

	ProcessRequest request;
	request.m_kind = MediaKind::IMAGE;
	request.m_mode = ProcessMode::BATCH_PROCESS;
	request.m_input_path = root + QStringLiteral("/input");
	request.m_output_path = out_dir;
	request.m_settings = image_settings(ModelType::REAL_ESRGAN_X4PLUS_ANIME,
										ScaleFactor::SCALE2X);
	request.m_temp_directory = test_work_dir() + QStringLiteral("/temp");

	ProcessService service;

	bool cancelled_message_seen = false;
	bool finished_emitted = false;
	const QMetaObject::Connection status_connection = QObject::connect(
		&service, &ProcessService::status_changed,
		[&cancelled_message_seen](const ProcessStatus &v_status)
		{
			if (v_status.m_message.contains(QStringLiteral("已取消处理")))
			{
				cancelled_message_seen = true;
			}
		});
	const QMetaObject::Connection finished_connection
		= QObject::connect(&service, &ProcessService::finished,
						   [&finished_emitted](bool, const QString &)
						   { finished_emitted = true; });

	QVERIFY(service.start(request));

	// 等到引擎真正开始运行后再取消
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < 5000 && service.state() != ProcessState::RUNNING)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QTest::qSleep(10);
	}
	QTest::qSleep(300);
	service.stop();

	timer.restart();
	while (timer.elapsed() < 5000 && service.state() != ProcessState::IDLE)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QTest::qSleep(10);
	}

	QObject::disconnect(status_connection);
	QObject::disconnect(finished_connection);

	// 取消不算失败：状态回到 IDLE、有「已取消处理」提示、不发 finished 信号
	QCOMPARE(service.state(), ProcessState::IDLE);
	QVERIFY(cancelled_message_seen);
	QVERIFY(!finished_emitted);
}

void
PipelineTest::engine_serialization_and_cleanup()
{
#ifndef Q_OS_WIN
	QSKIP("进程级串行 / Job Object 用例仅 Windows 运行");
#else
	// 用 PowerShell Start-Sleep 作为可控子进程：ssh/session 0 受限令牌下
	// ping 可能因权限失败，而 PowerShell 在该环境稳定可用。
	const QStringList short_args{
		QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
		QStringLiteral("Start-Sleep -Milliseconds 900")
	};
	const QStringList long_args{ QStringLiteral("-NoProfile"),
								 QStringLiteral("-Command"),
								 QStringLiteral("Start-Sleep -Seconds 8") };

	// 1) 全局串行 + 冷却：第一个引擎运行期间第二个请求进入异步队列，必须等
	//    第一个结束并冷却 >= 500ms 后才真正启动（不阻塞调用线程）。
	{
		ExeRunner first;
		ExeRunner second;
		bool first_done = false;
		bool second_done = false;
		bool first_ok = false;
		bool second_ok = false;
		QObject::connect(&first, &ExeRunner::finished,
						 [&](bool v_ok, int, const QStringList &)
						 {
							 first_ok = v_ok;
							 first_done = true;
						 });
		QObject::connect(&second, &ExeRunner::finished,
						 [&](bool v_ok, int, const QStringList &)
						 {
							 second_ok = v_ok;
							 second_done = true;
						 });

		first.start(QStringLiteral("powershell.exe"), short_args);
		second.start(QStringLiteral("powershell.exe"), short_args);
		QTRY_VERIFY_WITH_TIMEOUT(first_done, 15000);

		QElapsedTimer cooldown;
		cooldown.start();
		QTRY_VERIFY_WITH_TIMEOUT(second.process_id() != 0, 15000);
		// 允许少量定时器/调度抖动，但必须接近 500ms 冷却而不是立即启动。
		QVERIFY2(cooldown.elapsed() >= 400,
				 qPrintable(QStringLiteral("第二个引擎在冷却结束前启动了：%1ms")
								.arg(cooldown.elapsed())));
		QTRY_VERIFY_WITH_TIMEOUT(second_done, 15000);
		QVERIFY(first_ok);
		QVERIFY(second_ok);
	}

	// 2) 析构清理：父对象析构（stop + Job Object KILL_ON_JOB_CLOSE）后，
	//    仍在运行的控制台子进程不允许残留（孤儿进程回归）。
	{
		DWORD child_pid = 0;
		{
			ExeRunner runner;
			runner.start(QStringLiteral("powershell.exe"), long_args);
			QElapsedTimer started;
			started.start();
			while (runner.process_id() == 0 && started.elapsed() < 5000)
			{
				QTest::qWait(10);
			}
			child_pid = static_cast<DWORD>(runner.process_id());
			QVERIFY2(child_pid != 0, "子进程未启动");
		}
		QElapsedTimer waited;
		waited.start();
		while (waited.elapsed() < 5000 && process_is_alive(child_pid))
		{
			QTest::qWait(20);
		}
		QVERIFY2(!process_is_alive(child_pid),
				 "父对象析构后子进程仍然存活（孤儿进程）");
	}

	// 3) 停止后不再启动：排队中的请求被 stop() 取消，活动引擎结束后也不会
	//    启动该请求（"停止即不再启动引擎"语义不回退）。
	{
		ExeRunner active;
		ExeRunner queued;
		bool active_done = false;
		bool queued_finished = false;
		int queued_stopped = 0;
		QObject::connect(&active, &ExeRunner::finished,
						 [&](bool, int, const QStringList &)
						 { active_done = true; });
		QObject::connect(&queued, &ExeRunner::finished,
						 [&](bool, int, const QStringList &)
						 { queued_finished = true; });
		QObject::connect(&queued, &ExeRunner::stopped,
						 [&]() { ++queued_stopped; });

		active.start(QStringLiteral("powershell.exe"), long_args);
		QElapsedTimer started;
		started.start();
		while (active.process_id() == 0 && started.elapsed() < 5000)
		{
			QTest::qWait(10);
		}
		queued.start(QStringLiteral("powershell.exe"), short_args);
		QTest::qWait(50);
		queued.stop();
		QCOMPARE(queued_stopped, 1);
		QTRY_VERIFY_WITH_TIMEOUT(active_done, 20000);
		QTest::qWait(1200);
		QCOMPARE(queued.process_id(), static_cast<qint64>(0));
		QVERIFY(!queued_finished);
	}
#endif
}

QTEST_GUILESS_MAIN(PipelineTest)

#include "test_pipeline.moc"
