/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "layout.h"

#include "data/data_document.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "application.h"
#include "storage/file_upload.h"
#include "mainwindow.h"
#include "core/file_utilities.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "media/media_audio.h"
#include "storage/localstorage.h"
#include "history/view/history_view_cursor_state.h"

QString formatSizeText(qint64 size) {
	if (size >= 1024 * 1024) { // more than 1 mb
		qint64 sizeTenthMb = (size * 10 / (1024 * 1024));
		return QString::number(sizeTenthMb / 10) + '.' + QString::number(sizeTenthMb % 10) + qsl(" MB");
	}
	if (size >= 1024) {
		qint64 sizeTenthKb = (size * 10 / 1024);
		return QString::number(sizeTenthKb / 10) + '.' + QString::number(sizeTenthKb % 10) + qsl(" KB");
	}
	return QString::number(size) + qsl(" B");
}

QString formatDownloadText(qint64 ready, qint64 total) {
	QString readyStr, totalStr, mb;
	if (total >= 1024 * 1024) { // more than 1 mb
		qint64 readyTenthMb = (ready * 10 / (1024 * 1024)), totalTenthMb = (total * 10 / (1024 * 1024));
		readyStr = QString::number(readyTenthMb / 10) + '.' + QString::number(readyTenthMb % 10);
		totalStr = QString::number(totalTenthMb / 10) + '.' + QString::number(totalTenthMb % 10);
		mb = qsl("MB");
	} else if (total >= 1024) {
		qint64 readyKb = (ready / 1024), totalKb = (total / 1024);
		readyStr = QString::number(readyKb);
		totalStr = QString::number(totalKb);
		mb = qsl("KB");
	} else {
		readyStr = QString::number(ready);
		totalStr = QString::number(total);
		mb = qsl("B");
	}
	return lng_save_downloaded(lt_ready, readyStr, lt_total, totalStr, lt_mb, mb);
}

QString formatDurationText(qint64 duration) {
	qint64 hours = (duration / 3600), minutes = (duration % 3600) / 60, seconds = duration % 60;
	return (hours ? QString::number(hours) + ':' : QString()) + (minutes >= 10 ? QString() : QString('0')) + QString::number(minutes) + ':' + (seconds >= 10 ? QString() : QString('0')) + QString::number(seconds);
}

QString formatDurationWords(qint64 duration) {
	if (duration > 59) {
		auto minutes = (duration / 60);
		auto minutesCount = lng_duration_minsec_minutes(lt_count, minutes);
		auto seconds = (duration % 60);
		auto secondsCount = lng_duration_minsec_seconds(lt_count, seconds);
		return lng_duration_minutes_seconds(lt_minutes_count, minutesCount, lt_seconds_count, secondsCount);
	}
	return lng_duration_seconds(lt_count, duration);
}

QString formatDurationAndSizeText(qint64 duration, qint64 size) {
	return lng_duration_and_size(lt_duration, formatDurationText(duration), lt_size, formatSizeText(size));
}

QString formatGifAndSizeText(qint64 size) {
	return lng_duration_and_size(lt_duration, qsl("GIF"), lt_size, formatSizeText(size));
}

QString formatPlayedText(qint64 played, qint64 duration) {
	return lng_duration_played(lt_played, formatDurationText(played), lt_duration, formatDurationText(duration));
}

int32 documentColorIndex(DocumentData *document, QString &ext) {
	auto colorIndex = 0;

	auto name = document
		? (document->filename().isEmpty()
			? (document->sticker()
				? lang(lng_in_dlg_sticker)
				: qsl("Unknown File"))
			: document->filename())
		: lang(lng_message_empty);
	name = name.toLower();
	auto lastDot = name.lastIndexOf('.');
	auto mime = document
		? document->mimeString().toLower()
		: QString();
	if (name.endsWith(qstr(".doc")) ||
		name.endsWith(qstr(".txt")) ||
		name.endsWith(qstr(".psd")) ||
		mime.startsWith(qstr("text/"))) {
		colorIndex = 0;
	} else if (
		name.endsWith(qstr(".xls")) ||
		name.endsWith(qstr(".csv"))) {
		colorIndex = 1;
	} else if (
		name.endsWith(qstr(".pdf")) ||
		name.endsWith(qstr(".ppt")) ||
		name.endsWith(qstr(".key"))) {
		colorIndex = 2;
	} else if (
		name.endsWith(qstr(".zip")) ||
		name.endsWith(qstr(".rar")) ||
		name.endsWith(qstr(".ai")) ||
		name.endsWith(qstr(".mp3")) ||
		name.endsWith(qstr(".mov")) ||
		name.endsWith(qstr(".avi"))) {
		colorIndex = 3;
	} else {
		auto ch = (lastDot >= 0 && lastDot + 1 < name.size())
			? name.at(lastDot + 1)
			: (name.isEmpty()
				? (mime.isEmpty() ? '0' : mime.at(0))
				: name.at(0));
		colorIndex = (ch.unicode() % 4);
	}

	ext = document
		? ((lastDot < 0 || lastDot + 2 > name.size())
			? name
			: name.mid(lastDot + 1))
		: QString();

	return colorIndex;
}

style::color documentColor(int32 colorIndex) {
	const style::color colors[] = {
		st::msgFile1Bg,
		st::msgFile2Bg,
		st::msgFile3Bg,
		st::msgFile4Bg
	};
	return colors[colorIndex & 3];
}

style::color documentDarkColor(int32 colorIndex) {
	static style::color colors[] = {
		st::msgFile1BgDark,
		st::msgFile2BgDark,
		st::msgFile3BgDark,
		st::msgFile4BgDark
	};
	return colors[colorIndex & 3];
}

style::color documentOverColor(int32 colorIndex) {
	static style::color colors[] = {
		st::msgFile1BgOver,
		st::msgFile2BgOver,
		st::msgFile3BgOver,
		st::msgFile4BgOver
	};
	return colors[colorIndex & 3];
}

style::color documentSelectedColor(int32 colorIndex) {
	static style::color colors[] = {
		st::msgFile1BgSelected,
		st::msgFile2BgSelected,
		st::msgFile3BgSelected,
		st::msgFile4BgSelected
	};
	return colors[colorIndex & 3];
}

RoundCorners documentCorners(int32 colorIndex) {
	return RoundCorners(Doc1Corners + (colorIndex & 3));
}

[[nodiscard]] HistoryView::TextState LayoutItemBase::getState(
		QPoint point,
		StateRequest request) const {
	return {};
}

[[nodiscard]] TextSelection LayoutItemBase::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	return selection;
}
