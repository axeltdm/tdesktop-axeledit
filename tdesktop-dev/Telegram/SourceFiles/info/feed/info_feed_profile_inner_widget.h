/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/variable.h>
#include "ui/rp_widget.h"

namespace Window {
class Controller;
} // namespace Window

namespace Ui {
class VerticalLayout;
template <typename Widget>
class SlideWrap;
struct ScrollToRequest;
class MultiSlideTracker;
} // namespace Ui

namespace Info {

enum class Wrap;
class Controller;

namespace FeedProfile {

class Memento;
class Channels;
class Cover;

class InnerWidget final : public Ui::RpWidget {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller);

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	void setIsStackBottom(bool isStackBottom) {
		_isStackBottom = isStackBottom;
	}
	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;
	rpl::producer<int> desiredHeightValue() const override;

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	object_ptr<RpWidget> setupContent(not_null<RpWidget*> parent);

	int countDesiredHeight() const;
	void updateDesiredHeight() {
		_desiredHeight.fire(countDesiredHeight());
	}

	rpl::variable<bool> _isStackBottom = true;

	const not_null<Controller*> _controller;
	const not_null<Data::Feed*> _feed;

	Channels *_channels = nullptr;
	Cover *_cover = nullptr;
	object_ptr<RpWidget> _content;

	bool _inResize = false;
	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
	rpl::event_stream<int> _desiredHeight;

};

} // namespace FeedProfile
} // namespace Info
