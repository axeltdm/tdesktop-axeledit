/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "mtproto/sender.h"
#include "base/timer.h"

class ConfirmBox;

namespace Ui {
class IconButton;
class LinkButton;
} // namespace Ui

class SessionsBox : public BoxContent, private MTP::Sender {
public:
	SessionsBox(QWidget*);

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	struct Entry {
		uint64 hash = 0;

		bool incomplete = false;
		TimeId activeTime = 0;
		int nameWidth, activeWidth, infoWidth, ipWidth;
		QString name, active, info, ip;
	};
	struct Full {
		Entry current;
		std::vector<Entry> incomplete;
		std::vector<Entry> list;
	};
	class Inner;
	class List;

	static Entry ParseEntry(const MTPDauthorization &data);
	static void ResizeEntry(Entry &entry);
	void setLoading(bool loading);
	void shortPollSessions();

	void got(const MTPaccount_Authorizations &result);

	void terminateOne(uint64 hash);
	void terminateAll();

	bool _loading = false;
	Full _data;

	QPointer<Inner> _inner;
	QPointer<ConfirmBox> _terminateBox;

	base::Timer _shortPollTimer;
	mtpRequestId _shortPollRequest = 0;

};
