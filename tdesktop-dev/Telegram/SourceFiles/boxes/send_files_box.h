/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/variable.h>
#include "boxes/abstract_box.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"

namespace Window {
class Controller;
} // namespace Window

namespace ChatHelpers {
class TabbedPanel;
} // namespace ChatHelpers

namespace Ui {
template <typename Enum>
class Radioenum;
template <typename Enum>
class RadioenumGroup;
class RoundButton;
class InputField;
struct GroupMediaLayout;
class EmojiButton;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

enum class SendFilesWay {
	Album,
	Photos,
	Files,
};

class SendFilesBox : public BoxContent {
public:
	SendFilesBox(
		QWidget*,
		not_null<Window::Controller*> controller,
		Storage::PreparedList &&list,
		const TextWithTags &caption,
		CompressConfirm compressed);

	void setConfirmedCallback(
		Fn<void(
			Storage::PreparedList &&list,
			SendFilesWay way,
			TextWithTags &&caption,
			bool ctrlShiftEnter)> callback) {
		_confirmedCallback = std::move(callback);
	}
	void setCancelledCallback(Fn<void()> callback) {
		_cancelledCallback = std::move(callback);
	}

	~SendFilesBox();

protected:
	void prepare() override;
	void setInnerFocus() override;

	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	class AlbumPreview;

	void initSendWay();
	void initPreview(rpl::producer<int> desiredPreviewHeight);

	void setupControls();
	void setupSendWayControls();
	void setupCaption();
	void setupShadows(
		not_null<Ui::ScrollArea*> wrap,
		not_null<AlbumPreview*> content);

	void setupEmojiPanel();
	void updateEmojiPanelGeometry();
	bool emojiFilter(not_null<QEvent*> event);

	void refreshAlbumMediaCount();
	void preparePreview();
	void prepareSingleFilePreview();
	void prepareAlbumPreview();
	void applyAlbumOrder();

	void send(bool ctrlShiftEnter = false);
	void captionResized();

	void setupTitleText();
	void updateBoxSize();
	void updateControlsGeometry();

	bool canAddFiles(not_null<const QMimeData*> data) const;
	bool canAddUrls(const QList<QUrl> &urls) const;
	bool addFiles(not_null<const QMimeData*> data);

	not_null<Window::Controller*> _controller;

	QString _titleText;
	int _titleHeight = 0;

	Storage::PreparedList _list;

	CompressConfirm _compressConfirmInitial = CompressConfirm::None;
	CompressConfirm _compressConfirm = CompressConfirm::None;

	Fn<void(
		Storage::PreparedList &&list,
		SendFilesWay way,
		TextWithTags &&caption,
		bool ctrlShiftEnter)> _confirmedCallback;
	Fn<void()> _cancelledCallback;
	bool _confirmed = false;

	object_ptr<Ui::InputField> _caption = { nullptr };
	object_ptr<Ui::EmojiButton> _emojiToggle = { nullptr };
	base::unique_qptr<ChatHelpers::TabbedPanel> _emojiPanel;
	base::unique_qptr<QObject> _emojiFilter;

	object_ptr<Ui::Radioenum<SendFilesWay>> _sendAlbum = { nullptr };
	object_ptr<Ui::Radioenum<SendFilesWay>> _sendPhotos = { nullptr };
	object_ptr<Ui::Radioenum<SendFilesWay>> _sendFiles = { nullptr };
	std::shared_ptr<Ui::RadioenumGroup<SendFilesWay>> _sendWay;

	rpl::variable<int> _footerHeight = 0;

	QWidget *_preview = nullptr;
	AlbumPreview *_albumPreview = nullptr;
	int _albumVideosCount = 0;
	int _albumPhotosCount = 0;

	QPointer<Ui::RoundButton> _send;

};
