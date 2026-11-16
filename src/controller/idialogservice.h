#ifndef IDIALOGSERVICE_H
#define IDIALOGSERVICE_H

// ============================================================================
// 控制器层 - 对话框服务接口
//
// 把 QFileDialog / QMessageBox / QDesktopServices 等 UI 工具封装为接口，
// 使 AppController 可以脱离具体对话框实现进行测试与复用。
// ============================================================================

#include <QString>

class QWidget;

class IDialogService
{
  public:
	virtual ~IDialogService() = default;

	// 文件对话框
	virtual QString
	get_open_file_name(QWidget *v_parent, const QString &v_title,
					   const QString &v_filter)
		= 0;
	virtual QString
	get_existing_directory(QWidget *v_parent, const QString &v_title)
		= 0;
	virtual QString
	get_save_file_name(QWidget *v_parent, const QString &v_title,
					   const QString &v_default_path, const QString &v_filter)
		= 0;

	// 确认对话框（是 / 否）；返回 true 表示用户确认。
	// 用于「处理中关闭窗口」等需要用户拍板的流程。
	virtual bool
	ask_confirmation(QWidget *v_parent, const QString &v_title,
					 const QString &v_text)
		= 0;

	// 消息框
	virtual void
	show_information(QWidget *v_parent, const QString &v_title,
					 const QString &v_text)
		= 0;
	virtual void
	show_critical(QWidget *v_parent, const QString &v_title,
				  const QString &v_text)
		= 0;

	// 系统打开文件夹
	virtual void
	open_folder(const QString &v_path)
		= 0;
};

#endif // IDIALOGSERVICE_H