#ifndef QTDIALOGSERVICE_H
#define QTDIALOGSERVICE_H

// ============================================================================
// 控制器层 - 基于 Qt 的对话框服务实现
// ============================================================================

#include "idialogservice.h"

class QtDialogService : public IDialogService
{
  public:
	QString
	get_open_file_name(QWidget *v_parent, const QString &v_title,
					   const QString &v_filter) override;
	QString
	get_existing_directory(QWidget *v_parent, const QString &v_title) override;
	QString
	get_save_file_name(QWidget *v_parent, const QString &v_title,
					   const QString &v_default_path,
					   const QString &v_filter) override;

	bool
	ask_confirmation(QWidget *v_parent, const QString &v_title,
					 const QString &v_text) override;

	void
	show_information(QWidget *v_parent, const QString &v_title,
					 const QString &v_text) override;
	void
	show_critical(QWidget *v_parent, const QString &v_title,
				  const QString &v_text) override;

	void
	open_folder(const QString &v_path) override;
};

#endif // QTDIALOGSERVICE_H