#include "qtdialogservice.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QMessageBox>
#include <QUrl>

QString
QtDialogService::get_open_file_name(QWidget *v_parent, const QString &v_title,
									const QString &v_filter)
{
	return QFileDialog::getOpenFileName(v_parent, v_title, QString(), v_filter);
}

QString
QtDialogService::get_existing_directory(QWidget *v_parent,
										const QString &v_title)
{
	return QFileDialog::getExistingDirectory(v_parent, v_title, QString());
}

QString
QtDialogService::get_save_file_name(QWidget *v_parent, const QString &v_title,
									const QString &v_default_path,
									const QString &v_filter)
{
	return QFileDialog::getSaveFileName(v_parent, v_title, v_default_path,
										v_filter);
}

bool
QtDialogService::ask_confirmation(QWidget *v_parent, const QString &v_title,
								  const QString &v_text)
{
	const QMessageBox::StandardButton answer = QMessageBox::question(
		v_parent, v_title, v_text, QMessageBox::Yes | QMessageBox::No,
		QMessageBox::No);
	return answer == QMessageBox::Yes;
}

void
QtDialogService::show_information(QWidget *v_parent, const QString &v_title,
								  const QString &v_text)
{
	QMessageBox::information(v_parent, v_title, v_text);
}

void
QtDialogService::show_critical(QWidget *v_parent, const QString &v_title,
							   const QString &v_text)
{
	QMessageBox::critical(v_parent, v_title, v_text);
}

void
QtDialogService::open_folder(const QString &v_path)
{
	QDesktopServices::openUrl(QUrl::fromLocalFile(v_path));
}