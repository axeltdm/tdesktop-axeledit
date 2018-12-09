/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/event_filter.h"

namespace Core {

EventFilter::EventFilter(
	not_null<QObject*> parent,
	Fn<bool(not_null<QEvent*>)> filter)
: QObject(parent)
, _filter(std::move(filter)) {
	parent->installEventFilter(this);
}

bool EventFilter::eventFilter(QObject *watched, QEvent *event) {
	return _filter(event);
}

not_null<QObject*> InstallEventFilter(
		not_null<QObject*> object,
		Fn<bool(not_null<QEvent*>)> filter) {
	return new EventFilter(object, std::move(filter));
}

} // namespace Core
