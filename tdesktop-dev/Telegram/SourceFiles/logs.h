/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

namespace Core {
class Launcher;
} // namespace Core

namespace Logs {

void SetDebugEnabled(bool enabled);
bool DebugEnabled();

void start(not_null<Core::Launcher*> launcher);
bool started();
void finish();

bool instanceChecked();
void multipleInstances();

void closeMain();

void writeMain(const QString &v);

void writeDebug(const char *file, int32 line, const QString &v);
void writeTcp(const QString &v);
void writeMtp(int32 dc, const QString &v);

QString full();

inline const char *b(bool v) {
	return v ? "[TRUE]" : "[FALSE]";
}

struct MemoryBuffer {
	MemoryBuffer(const void *ptr, uint32 size) : p(ptr), s(size) {
	}
	QString str() const {
		QString result;
		const uchar *buf((const uchar*)p);
		const char *hex = "0123456789ABCDEF";
		result.reserve(s * 3);
		for (uint32 i = 0; i < s; ++i) {
			result += hex[(buf[i] >> 4)];
			result += hex[buf[i] & 0x0F];
			result += ' ';
		}
		result.chop(1);
		return result;
	}

	const void *p;
	uint32 s;

};

inline MemoryBuffer mb(const void *ptr, uint32 size) {
	return MemoryBuffer(ptr, size);
}

} // namespace Logs

#define LOG(msg) (Logs::writeMain(QString msg))
//usage LOG(("log: %1 %2").arg(1).arg(2))

#define DEBUG_LOG(msg) { if (Logs::DebugEnabled() || !Logs::started()) Logs::writeDebug(__FILE__, __LINE__, QString msg); }
//usage DEBUG_LOG(("log: %1 %2").arg(1).arg(2))

#define TCP_LOG(msg) { if (Logs::DebugEnabled() || !Logs::started()) Logs::writeTcp(QString msg); }
//usage TCP_LOG(("log: %1 %2").arg(1).arg(2))

#define MTP_LOG(dc, msg) { if (Logs::DebugEnabled() || !Logs::started()) Logs::writeMtp(dc, QString msg); }
//usage MTP_LOG(dc, ("log: %1 %2").arg(1).arg(2))
