#include "icon_provider.h"

#include <QDebug>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QPixmap>

#include "theme_manager.h"

QHash<QString, QPixmap> IconProvider::s_tinted_cache;

QColor
IconProvider::tone_color(Tone v_tone)
{
	const ThemeTokens &tokens = ThemeManager::instance().tokens();
	switch (v_tone)
	{
		case Tone::PRIMARY:
			return tokens.m_text_1;
		case Tone::SECONDARY:
			return tokens.m_text_2;
		case Tone::TERTIARY:
			return tokens.m_text_3;
		case Tone::ON_ACCENT:
			return tokens.m_action_fg;
		case Tone::ACCENT:
			return tokens.m_action_text;
		case Tone::DANGER:
			return tokens.m_danger_text;
		case Tone::SUCCESS:
			return tokens.m_success_text;
		case Tone::WARNING:
			return tokens.m_warning_text;
	}
	return tokens.m_text_2;
}

QIcon
IconProvider::icon(const QString &v_name, Tone v_tone, int v_logical_size)
{
	const QPixmap normal = pixmap(v_name, tone_color(v_tone), v_logical_size);
	if (normal.isNull())
	{
		return QIcon();
	}

	QIcon result;
	result.addPixmap(normal, QIcon::Normal);
	// 禁用态用三级文字色实色，而不是把语义色整体降不透明度：细描边图标
	// （如 square）降透明度后在浅色底上几乎不可见；实色与 QSS 禁用文字一致。
	result.addPixmap(pixmap(v_name, tone_color(Tone::TERTIARY), v_logical_size),
					 QIcon::Disabled);
	return result;
}

QPixmap
IconProvider::pixmap(const QString &v_name, const QColor &v_color,
					 int v_logical_size)
{
	const QPixmap tinted = tinted_pixmap(v_name, v_color);
	if (tinted.isNull())
	{
		return QPixmap();
	}

	qreal dpr = 1.0;
	if (QGuiApplication::primaryScreen() != nullptr)
	{
		dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
	}
	// 按逻辑尺寸缩放（96px 源图 → 目标），并标记 DPR 让 Qt 以逻辑尺寸布局
	const int target = qMax(1, qRound(v_logical_size * dpr));
	QPixmap scaled = tinted.scaled(target, target, Qt::KeepAspectRatio,
								   Qt::SmoothTransformation);
	scaled.setDevicePixelRatio(dpr);
	return scaled;
}

QPixmap
IconProvider::tinted_pixmap(const QString &v_name, const QColor &v_color)
{
	const QString key
		= QStringLiteral("%1|%2").arg(v_name, v_color.name(QColor::HexArgb));
	const auto cached = s_tinted_cache.constFind(key);
	if (cached != s_tinted_cache.constEnd())
	{
		return cached.value();
	}

	// 源图是白色描边 + 透明底
	QPixmap source(QStringLiteral(":/icons/%1.png").arg(v_name));
	if (source.isNull())
	{
		qWarning() << "[IconProvider] 缺少图标资源:" << v_name;
		return QPixmap();
	}

	QPixmap tinted(source.size());
	tinted.fill(Qt::transparent);

	QPainter painter(&tinted);
	painter.drawPixmap(0, 0, source);
	// SourceIn：只保留源图 alpha，用目标色着色
	painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
	painter.fillRect(tinted.rect(), v_color);
	painter.end();

	s_tinted_cache.insert(key, tinted);
	return tinted;
}
