/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "profile/profile_back_button.h"

//#include "history/view/history_view_top_bar_widget.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"
#include "styles/style_profile.h"
#include "styles/style_info.h"

namespace Profile {

BackButton::BackButton(QWidget *parent, const QString &text) : Ui::AbstractButton(parent)
, _text(text.toUpper()) {
	setCursor(style::cur_pointer);

	subscribe(Adaptive::Changed(), [this] { updateAdaptiveLayout(); });
	updateAdaptiveLayout();
}

void BackButton::setText(const QString &text) {
	_text = text.toUpper();
	update();
}

int BackButton::resizeGetHeight(int newWidth) {
	return st::profileTopBarHeight;
}

void BackButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::profileBg);
	st::topBarBack.paint(p, (st::topBarArrowPadding.left() - st::topBarBack.width()) / 2, (st::topBarHeight - st::topBarBack.height()) / 2, width());

	p.setFont(st::topBarButton.font);
	p.setPen(st::topBarButton.textFg);
	p.drawTextLeft(st::topBarArrowPadding.left(), st::topBarButton.padding.top() + st::topBarButton.textTop, width(), _text);
}

void BackButton::onStateChanged(State was, StateChangeSource source) {
	if (isDown() && !(was & StateFlag::Down)) {
		emit clicked();
	}
}

void BackButton::updateAdaptiveLayout() {
	if (!Adaptive::OneColumn()) {
		unsubscribe(base::take(_unreadCounterSubscription));
	} else if (!_unreadCounterSubscription) {
		_unreadCounterSubscription = subscribe(Global::RefUnreadCounterUpdate(), [this] {
			rtlupdate(0, 0, st::titleUnreadCounterRight, st::titleUnreadCounterTop);
		});
	}
}

} // namespace Profile
