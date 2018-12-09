/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "boxes/abstract_box.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/view/history_view_element.h"
#include "history/history.h"

class AuthSession;

namespace Ui {
class ScrollArea;
class InputField;
} // namespace Ui

namespace Support {

struct Contact {
	QString comment;
	QString phone;
	QString firstName;
	QString lastName;
};

class Autocomplete : public Ui::RpWidget {
public:
	Autocomplete(QWidget *parent, not_null<AuthSession*> session);

	void activate(not_null<Ui::InputField*> field);
	void deactivate();
	void setBoundings(QRect rect);

	rpl::producer<QString> insertRequests() const;
	rpl::producer<Contact> shareContactRequests() const;

protected:
	void keyPressEvent(QKeyEvent *e) override;

private:
	void setupContent();
	void submitValue(const QString &value);

	not_null<AuthSession*> _session;
	Fn<void()> _activate;
	Fn<void()> _deactivate;
	Fn<void(int delta)> _moveSelection;

	rpl::event_stream<QString> _insertRequests;
	rpl::event_stream<Contact> _shareContactRequests;

};

class ConfirmContactBox
	: public BoxContent
	, public HistoryView::ElementDelegate {
public:
	ConfirmContactBox(
		QWidget*,
		not_null<History*> history,
		const Contact &data,
		Fn<void(Qt::KeyboardModifiers)> submit);

	using Element = HistoryView::Element;
	HistoryView::Context elementContext() override;
	std::unique_ptr<Element> elementCreate(
		not_null<HistoryMessage*> message) override;
	std::unique_ptr<Element> elementCreate(
		not_null<HistoryService*> message) override;
	bool elementUnderCursor(not_null<const Element*> view) override;
	void elementAnimationAutoplayAsync(
		not_null<const Element*> element) override;
	TimeMs elementHighlightTime(
		not_null<const Element*> element) override;
	bool elementInSelectionMode() override;

protected:
	void prepare() override;
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	AdminLog::OwnedItem _comment;
	AdminLog::OwnedItem _contact;
	Fn<void(Qt::KeyboardModifiers)> _submit;

};

} //namespace Support
