/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class Checkbox;
class FlatLabel;
class EmptyUserpic;
} // namespace Ui

class InformBox;
class ConfirmBox : public BoxContent, public ClickHandlerHost {
public:
	ConfirmBox(QWidget*, const QString &text, FnMut<void()> confirmedCallback = FnMut<void()>(), FnMut<void()> cancelledCallback = FnMut<void()>());
	ConfirmBox(QWidget*, const QString &text, const QString &confirmText, FnMut<void()> confirmedCallback = FnMut<void()>(), FnMut<void()> cancelledCallback = FnMut<void()>());
	ConfirmBox(QWidget*, const QString &text, const QString &confirmText, const style::RoundButton &confirmStyle, FnMut<void()> confirmedCallback = FnMut<void()>(), FnMut<void()> cancelledCallback = FnMut<void()>());
	ConfirmBox(QWidget*, const QString &text, const QString &confirmText, const QString &cancelText, FnMut<void()> confirmedCallback = FnMut<void()>(), FnMut<void()> cancelledCallback = FnMut<void()>());
	ConfirmBox(QWidget*, const QString &text, const QString &confirmText, const style::RoundButton &confirmStyle, const QString &cancelText, FnMut<void()> confirmedCallback = FnMut<void()>(), FnMut<void()> cancelledCallback = FnMut<void()>());
	ConfirmBox(QWidget*, const TextWithEntities &text, const QString &confirmText, FnMut<void()> confirmedCallback = nullptr, FnMut<void()> cancelledCallback = nullptr);

	void updateLink();

	// If strict cancel is set the cancelledCallback is only called if the cancel button was pressed.
	void setStrictCancel(bool strictCancel) {
		_strictCancel = strictCancel;
	}

	void setMaxLineCount(int count);

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

protected:
	void prepare() override;

	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct InformBoxTag {
	};
	ConfirmBox(const InformBoxTag &, const QString &text, const QString &doneText, Fn<void()> closedCallback);
	ConfirmBox(const InformBoxTag &, const TextWithEntities &text, const QString &doneText, Fn<void()> closedCallback);
	FnMut<void()> generateInformCallback(Fn<void()> closedCallback);
	friend class InformBox;

	void confirmed();
	void init(const QString &text);
	void init(const TextWithEntities &text);
	void textUpdated();
	void updateHover();

	QString _confirmText;
	QString _cancelText;
	const style::RoundButton &_confirmStyle;
	bool _informative = false;

	Text _text;
	int _textWidth = 0;
	int _textHeight = 0;
	int _maxLineCount = 16;

	QPoint _lastMousePos;

	bool _confirmed = false;
	bool _cancelled = false;
	bool _strictCancel = false;
	FnMut<void()> _confirmedCallback;
	FnMut<void()> _cancelledCallback;

};

class InformBox : public ConfirmBox {
public:
	InformBox(QWidget*, const QString &text, Fn<void()> closedCallback = nullptr);
	InformBox(QWidget*, const QString &text, const QString &doneText, Fn<void()> closedCallback = nullptr);
	InformBox(QWidget*, const TextWithEntities &text, Fn<void()> closedCallback = nullptr);
	InformBox(QWidget*, const TextWithEntities &text, const QString &doneText, Fn<void()> closedCallback = nullptr);

};

class MaxInviteBox : public BoxContent {
public:
	MaxInviteBox(QWidget*, not_null<ChannelData*> channel);

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void updateSelected(const QPoint &cursorGlobalPosition);

	not_null<ChannelData*> _channel;

	Text _text;
	int32 _textWidth, _textHeight;

	QRect _invitationLink;
	bool _linkOver = false;

	QPoint _lastMousePos;

};

class ConvertToSupergroupBox : public BoxContent, public RPCSender {
public:
	ConvertToSupergroupBox(QWidget*, ChatData *chat);

protected:
	void prepare() override;

	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void convertToSupergroup();
	void convertDone(const MTPUpdates &updates);
	bool convertFail(const RPCError &error);

	ChatData *_chat;
	Text _text, _note;
	int32 _textWidth, _textHeight;

};

class PinMessageBox : public BoxContent, public RPCSender {
public:
	PinMessageBox(QWidget*, not_null<PeerData*> peer, MsgId msgId);

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void pinMessage();
	void pinDone(const MTPUpdates &updates);
	bool pinFail(const RPCError &error);

	not_null<PeerData*> _peer;
	MsgId _msgId;

	object_ptr<Ui::FlatLabel> _text;
	object_ptr<Ui::Checkbox> _notify = { nullptr };

	mtpRequestId _requestId = 0;

};

class DeleteMessagesBox : public BoxContent, public RPCSender {
public:
	DeleteMessagesBox(
		QWidget*,
		not_null<HistoryItem*> item,
		bool suggestModerateActions);
	DeleteMessagesBox(QWidget*, MessageIdsList &&selected);

	void setDeleteConfirmedCallback(Fn<void()> callback) {
		_deleteConfirmedCallback = std::move(callback);
	}

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void deleteAndClear();

	const MessageIdsList _ids;
	const bool _singleItem = false;
	UserData *_moderateFrom = nullptr;
	ChannelData *_moderateInChannel = nullptr;
	bool _moderateBan = false;
	bool _moderateDeleteAll = false;

	object_ptr<Ui::FlatLabel> _text = { nullptr };
	object_ptr<Ui::Checkbox> _forEveryone = { nullptr };
	object_ptr<Ui::Checkbox> _banUser = { nullptr };
	object_ptr<Ui::Checkbox> _reportSpam = { nullptr };
	object_ptr<Ui::Checkbox> _deleteAll = { nullptr };

	Fn<void()> _deleteConfirmedCallback;

};

class ConfirmInviteBox : public BoxContent, public RPCSender {
public:
	ConfirmInviteBox(
		QWidget*,
		const MTPDchatInvite &data,
		Fn<void()> submit);
	~ConfirmInviteBox();

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	static std::vector<not_null<UserData*>> GetParticipants(
		const MTPDchatInvite &data);

	Fn<void()> _submit;
	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::FlatLabel> _status;
	ImagePtr _photo;
	std::unique_ptr<Ui::EmptyUserpic> _photoEmpty;
	std::vector<not_null<UserData*>> _participants;
	bool _isChannel = false;

	int _userWidth = 0;

};

class ConfirmDontWarnBox : public BoxContent {
public:
	ConfirmDontWarnBox(
		QWidget*,
		const QString &text,
		const QString &checkbox,
		const QString &confirm,
		FnMut<void(bool)> callback);

protected:
	void prepare() override;

private:
	not_null<Ui::RpWidget*> setupContent(
		const QString &text,
		const QString &checkbox,
		FnMut<void(bool)> callback);

	QString _confirm;
	FnMut<void()> _callback;
	not_null<Ui::RpWidget*> _content;

};
