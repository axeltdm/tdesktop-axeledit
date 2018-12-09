/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_top_bar.h"

#include "styles/style_calls.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "lang/lang_keys.h"
#include "calls/calls_call.h"
#include "calls/calls_instance.h"
#include "calls/calls_panel.h"
#include "styles/style_boxes.h"
#include "observer_peer.h"
#include "boxes/abstract_box.h"
#include "base/timer.h"
#include "layout.h"

namespace Calls {
namespace {

constexpr auto kUpdateDebugTimeoutMs = TimeMs(500);

class DebugInfoBox : public BoxContent {
public:
	DebugInfoBox(QWidget*, base::weak_ptr<Call> call);

protected:
	void prepare() override;

private:
	void updateText();

	base::weak_ptr<Call> _call;
	QPointer<Ui::FlatLabel> _text;
	base::Timer _updateTextTimer;

};

DebugInfoBox::DebugInfoBox(QWidget*, base::weak_ptr<Call> call)
: _call(call) {
}

void DebugInfoBox::prepare() {
	setTitle([] { return QString("Call Debug"); });

	addButton(langFactory(lng_close), [this] { closeBox(); });
	_text = setInnerWidget(
		object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
			this,
			object_ptr<Ui::FlatLabel>(this, st::callDebugLabel),
			st::callDebugPadding))->entity();
	_text->setSelectable(true);
	updateText();
	_updateTextTimer.setCallback([this] { updateText(); });
	_updateTextTimer.callEach(kUpdateDebugTimeoutMs);
	setDimensions(st::boxWideWidth, st::boxMaxListHeight);
}

void DebugInfoBox::updateText() {
	if (auto call = _call.get()) {
		_text->setText(call->getDebugLog());
	}
}

} // namespace

TopBar::TopBar(
	QWidget *parent,
	const base::weak_ptr<Call> &call)
: RpWidget(parent)
, _call(call)
, _durationLabel(this, st::callBarLabel)
, _signalBars(this, _call.get(), st::callBarSignalBars)
, _fullInfoLabel(this, st::callBarInfoLabel)
, _shortInfoLabel(this, st::callBarInfoLabel)
, _hangupLabel(this, st::callBarLabel, lang(lng_call_bar_hangup).toUpper())
, _mute(this, st::callBarMuteToggle)
, _info(this)
, _hangup(this, st::callBarHangup) {
	initControls();
	resize(width(), st::callBarHeight);
}

void TopBar::initControls() {
	_mute->setClickedCallback([this] {
		if (auto call = _call.get()) {
			call->setMute(!call->isMute());
		}
	});
	setMuted(_call->isMute());
	subscribe(_call->muteChanged(), [this](bool mute) {
		setMuted(mute);
		update();
	});
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::NameChanged, [this](const Notify::PeerUpdate &update) {
		if (auto call = _call.get()) {
			if (update.peer == call->user()) {
				updateInfoLabels();
			}
		}
	}));
	setInfoLabels();
	_info->setClickedCallback([this] {
		if (auto call = _call.get()) {
			if (Logs::DebugEnabled()
				&& (_info->clickModifiers() & Qt::ControlModifier)) {
				Ui::show(Box<DebugInfoBox>(_call));
			} else {
				Current().showInfoPanel(call);
			}
		}
	});
	_hangup->setClickedCallback([this] {
		if (auto call = _call.get()) {
			call->hangup();
		}
	});
	_updateDurationTimer.setCallback([this] { updateDurationText(); });
	updateDurationText();
}

void TopBar::updateInfoLabels() {
	setInfoLabels();
	updateControlsGeometry();
}

void TopBar::setInfoLabels() {
	if (auto call = _call.get()) {
		auto user = call->user();
		auto fullName = App::peerName(user);
		auto shortName = user->firstName;
		_fullInfoLabel->setText(fullName.toUpper());
		_shortInfoLabel->setText(shortName.toUpper());
	}
}

void TopBar::setMuted(bool mute) {
	_mute->setIconOverride(mute ? &st::callBarUnmuteIcon : nullptr);
	_mute->setRippleColorOverride(mute ? &st::callBarUnmuteRipple : nullptr);
	_hangup->setRippleColorOverride(mute ? &st::callBarUnmuteRipple : nullptr);
	_muted = mute;
}

void TopBar::updateDurationText() {
	if (!_call) {
		return;
	}
	auto wasWidth = _durationLabel->width();
	auto durationMs = _call->getDurationMs();
	auto durationSeconds = durationMs / 1000;
	startDurationUpdateTimer(durationMs);
	_durationLabel->setText(formatDurationText(durationSeconds));
	if (_durationLabel->width() != wasWidth) {
		updateControlsGeometry();
	}
}

void TopBar::startDurationUpdateTimer(TimeMs currentDuration) {
	auto msTillNextSecond = 1000 - (currentDuration % 1000);
	_updateDurationTimer.callOnce(msTillNextSecond + 5);
}

void TopBar::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void TopBar::updateControlsGeometry() {
	auto left = 0;
	_mute->moveToLeft(left, 0);
	left += _mute->width();
	_durationLabel->moveToLeft(left, st::callBarLabelTop);
	left += _durationLabel->width() + st::callBarSkip;
	_signalBars->moveToLeft(left, (height() - _signalBars->height()) / 2);
	left += _signalBars->width() + st::callBarSkip;

	auto right = st::callBarRightSkip;
	_hangupLabel->moveToRight(right, st::callBarLabelTop);
	right += _hangupLabel->width();
	right += st::callBarHangup.width;
	_hangup->setGeometryToRight(0, 0, right, height());
	_info->setGeometryToLeft(
		_mute->width(),
		0,
		width() - _mute->width() - _hangup->width(),
		height());

	auto fullWidth = _fullInfoLabel->naturalWidth();
	auto showFull = (left + fullWidth + right <= width());
	_fullInfoLabel->setVisible(showFull);
	_shortInfoLabel->setVisible(!showFull);

	auto setInfoLabelGeometry = [this, left, right](auto &&infoLabel) {
		auto minPadding = qMax(left, right);
		auto infoWidth = infoLabel->naturalWidth();
		auto infoLeft = (width() - infoWidth) / 2;
		if (infoLeft < minPadding) {
			infoLeft = left;
			infoWidth = width() - left - right;
		}
		infoLabel->setGeometryToLeft(infoLeft, st::callBarLabelTop, infoWidth, st::callBarInfoLabel.style.font->height);
	};
	setInfoLabelGeometry(_fullInfoLabel);
	setInfoLabelGeometry(_shortInfoLabel);
}

void TopBar::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), _muted ? st::callBarBgMuted : st::callBarBg);
}

TopBar::~TopBar() = default;

} // namespace Calls
