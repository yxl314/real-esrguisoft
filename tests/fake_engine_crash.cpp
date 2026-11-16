// ============================================================================
// 测试辅助：Windows 假超分引擎（仅用于 tests/test_pipeline.cpp 的崩溃重试回归）
//
// 行为：
//   1. 每次调用把完整命令行追加到 RESR_FAKE_ENGINE_LOG（UTF-8，供测试断言参数）；
//   2. 若 RESR_FAKE_ENGINE_STATE 状态文件不存在：创建它，然后以 0xC0000409
//      （STATUS_STACK_BUFFER_OVERRUN）崩溃——模拟 realesrgan 的崩溃退出码；
//   3. 状态文件已存在：把 -i 输入（文件或目录内的 PNG）复制到 -o 后退出 0。
//
// 不启动真实引擎/GPU、不参与产品打包；仅 Windows 构建（tests/CMakeLists.txt）。
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>

#include <cstdlib>
#include <stdlib.h>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace
{

std::wstring
env_value(const wchar_t *v_name)
{
	const wchar_t *value = ::_wgetenv(v_name);
	return value == nullptr ? std::wstring() : std::wstring(value);
}

void
append_utf8(HANDLE v_file, const std::wstring &v_text)
{
	const int size = ::WideCharToMultiByte(CP_UTF8, 0, v_text.c_str(), -1,
										   nullptr, 0, nullptr, nullptr);
	if (size <= 1)
	{
		return;
	}
	std::vector<char> buffer(static_cast<std::size_t>(size));
	(void)::WideCharToMultiByte(CP_UTF8, 0, v_text.c_str(), -1, buffer.data(),
								size, nullptr, nullptr);
	DWORD written = 0;
	(void)::WriteFile(v_file, buffer.data(), static_cast<DWORD>(size - 1),
					  &written, nullptr);
}

void
append_log(const std::wstring &v_path, int v_argc, wchar_t **v_argv)
{
	if (v_path.empty())
	{
		return;
	}
	HANDLE file
		= ::CreateFileW(v_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
						nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		return;
	}
	for (int index = 0; index < v_argc; ++index)
	{
		if (index > 0)
		{
			DWORD written = 0;
			(void)::WriteFile(file, " ", 1, &written, nullptr);
		}
		append_utf8(file, v_argv[index]);
	}
	DWORD written = 0;
	(void)::WriteFile(file, "\n", 1, &written, nullptr);
	::CloseHandle(file);
}

bool
copy_input_to_output(const std::filesystem::path &v_input,
					 const std::filesystem::path &v_output)
{
	std::error_code error;
	if (std::filesystem::is_directory(v_input, error))
	{
		std::filesystem::create_directories(v_output, error);
		if (error)
		{
			return false;
		}
		for (const auto &entry :
			 std::filesystem::directory_iterator(v_input, error))
		{
			if (error)
			{
				return false;
			}
			if (!entry.is_regular_file())
			{
				continue;
			}
			if (entry.path().extension() != std::filesystem::path(L".png"))
			{
				continue;
			}
			std::filesystem::copy_file(
				entry.path(), v_output / entry.path().filename(),
				std::filesystem::copy_options::overwrite_existing, error);
			if (error)
			{
				return false;
			}
		}
		return true;
	}

	if (v_output.has_parent_path())
	{
		std::filesystem::create_directories(v_output.parent_path(), error);
		if (error)
		{
			return false;
		}
	}
	std::filesystem::copy_file(
		v_input, v_output, std::filesystem::copy_options::overwrite_existing,
		error);
	return !error;
}

} // namespace

int
main()
{
	// 避免 Windows Error Reporting 弹窗/阻塞（测试环境无人值守）。
	::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX
				   | SEM_NOOPENFILEERRORBOX);

	int argc = 0;
	LPWSTR *argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
	if (argv == nullptr)
	{
		return 3;
	}

	append_log(env_value(L"RESR_FAKE_ENGINE_LOG"), argc, argv);

	const std::wstring state_path = env_value(L"RESR_FAKE_ENGINE_STATE");
	if (!state_path.empty()
		&& ::GetFileAttributesW(state_path.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		HANDLE marker
			= ::CreateFileW(state_path.c_str(), GENERIC_WRITE, 0, nullptr,
							CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (marker != INVALID_HANDLE_VALUE)
		{
			::CloseHandle(marker);
		}
		::LocalFree(argv);
		// 与 realesrgan 崩溃一致：STATUS_STACK_BUFFER_OVERRUN（0xC0000409）。
		::RaiseException(0xC0000409u, EXCEPTION_NONCONTINUABLE, 0, nullptr);
		return 1;
	}

	std::filesystem::path input;
	std::filesystem::path output;
	for (int index = 1; index < argc; ++index)
	{
		const std::wstring argument = argv[index];
		if (argument == L"-i" && index + 1 < argc)
		{
			input = argv[++index];
		}
		else if (argument == L"-o" && index + 1 < argc)
		{
			output = argv[++index];
		}
	}

	const bool ok = !input.empty() && !output.empty()
					&& copy_input_to_output(input, output);
	::LocalFree(argv);
	return ok ? 0 : 2;
}
