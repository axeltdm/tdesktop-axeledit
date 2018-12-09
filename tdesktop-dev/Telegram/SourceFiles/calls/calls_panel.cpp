/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_panel.h"

#include "data/data_photo.h"
#include "data/data_session.h"
#include "calls/calls_emoji_fingerprint.h"
#include "styles/style_calls.h"
#include "styles/style_history.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/effects/ripple_animation.h"
#include "ui/image/image.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/empty_userpic.h"
#include "ui/emoji_config.h"
#include "messenger.h"
#include "mainwindow.h"
#include "lang/lang_keys.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "observer_peer.h"
#include "platform/platform_specific.h"
#include "window/main_window.h"
#include "layout.h"

namespace Calls {
namespace {

constexpr auto kTooltipShowTimeoutMs = 1000;

} // namespace

class Panel::Button : public Ui::RippleButton {
public:
	Button(QWidget *parent, const style::CallButton &stFrom, const style::CallButton *stTo = nullptr);

	void setProgress(float64 progress);
	void setOuterValue(float64 value);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	QPoint iconPosition(not_null<const style::CallButton*> st) const;
	void mixIconMasks();

	not_null<const style::CallButton*> _stFrom;
	const style::CallButton *_stTo = nullptr;
	float64 _progress = 0.;

	QImage _bgMask, _bg;
	QPixmap _bgFrom, _bgTo;
	QImage _iconMixedMask, _iconFrom, _iconTo, _iconMixed;

	float64 _outerValue = 0.;
	Animation _outerAnimation;

};

SignalBars::SignalBars(
	QWidget *parent,
	not_null<Call*> call,
	const style::CallSignalBars &st,
	Fn<void()> displayedChangedCallback)
: RpWidget(parent)
, _st(st)
, _displayedChangedCallback(std::move(displayedChangedCallback)) {
	resize(
		_st.width + (_st.width + _st.skip) * (Call::kSignalBarCount - 1),
		_st.width * Call::kSignalBarCount);
	subscribe(call->signalBarCountChanged(), [=](int count) {
		changed(count);
	});
}

bool SignalBars::isDisplayed() const {
	return (_count >= 0);
}

void SignalBars::paintEvent(QPaintEvent *e) {
	if (!isDisplayed()) {
		return;
	}

	Painter p(this);

	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(_st.color);
	for (auto i = 0; i < Call::kSignalBarCount; ++i) {
		p.setOpacity((i < _count) ? 1. : _st.inactiveOpacity);
		const auto barHeight = (i + 1) * _st.width;
		const auto barLeft = i * (_st.width + _st.skip);
		const auto barTop = height() - barHeight;
		p.drawRoundedRect(
			barLeft,
			barTop,
			_st.width,
			barHeight,
			_st.radius,
			_st.radius);
	}
	p.setOpacity(1.);
}

void SignalBars::changed(int count) {
	if (_count == Call::kSignalBarFinished) {
		return;
	}
	if (_count != count) {
		const auto wasDisplayed = isDisplayed();
		_count = count;
		if (isDisplayed() != wasDisplayed && _displayedChangedCallback) {
			_displayedChangedCallback();
		}
		update();
	}
}

Panel::Button::Button(QWidget *parent, const style::CallButton &stFrom, const style::CallButton *stTo) : Ui::RippleButton(parent, stFrom.button.ripple)
, _stFrom(&stFrom)
, _stTo(stTo) {
	resize(_stFrom->button.width, _stFrom->button.height);

	_bgMask = prepareRippleMask();
	_bgFrom = App::pixmapFromImageInPlace(style::colorizeImage(_bgMask, _stFrom->bg));
	if (_stTo) {
		Assert(_stFrom->button.width == _stTo->button.width);
		Assert(_stFrom->button.height == _stTo->button.height);
		Assert(_stFrom->button.rippleAreaPosition == _stTo->button.rippleAreaPosition);
		Assert(_stFrom->button.rippleAreaSize == _stTo->button.rippleAreaSize);

		_bg = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_bg.setDevicePixelRatio(cRetinaFactor());
		_bgTo = App::pixmapFromImageInPlace(style::colorizeImage(_bgMask, _stTo->bg));
		_iconMixedMask = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconMixedMask.setDevicePixelRatio(cRetinaFactor());
		_iconFrom = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconFrom.setDevicePixelRatio(cRetinaFactor());
		_iconFrom.fill(Qt::black);
		{
			Painter p(&_iconFrom);
			p.drawImage((_stFrom->button.rippleAreaSize - _stFrom->button.icon.width()) / 2, (_stFrom->button.rippleAreaSize - _stFrom->button.icon.height()) / 2, _stFrom->button.icon.instance(Qt::white));
		}
		_iconTo = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconTo.setDevicePixelRatio(cRetinaFactor());
		_iconTo.fill(Qt::black);
		{
			Painter p(&_iconTo);
			p.drawImage((_stTo->button.rippleAreaSize - _stTo->button.icon.width()) / 2, (_stTo->button.rippleAreaSize - _stTo->button.icon.height()) / 2, _stTo->button.icon.instance(Qt::white));
		}
		_iconMixed = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconMixed.setDevicePixelRatio(cRetinaFactor());
	}
}

void Panel::Button::setOuterValue(float64 value) {
	if (_outerValue != value) {
		_outerAnimation.start([this] {
			if (_progress == 0. || _progress == 1.) {
				update();
			}
		}, _outerValue, value, Call::kSoundSampleMs);
		_outerValue = value;
	}
}

void Panel::Button::setProgress(float64 progress) {
	_progress = progress;
	update();
}

void Panel::Button::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto bgPosition = myrtlpoint(_stFrom->button.rippleAreaPosition);
	auto paintFrom = (_progress == 0.) || !_stTo;
	auto paintTo = !paintFrom && (_progress == 1.);

	auto outerValue = _outerAnimation.current(ms, _outerValue);
	if (outerValue > 0.) {
		auto outerRadius = paintFrom ? _stFrom->outerRadius : paintTo ? _stTo->outerRadius : (_stFrom->outerRadius * (1. - _progress) + _stTo->outerRadius * _progress);
		auto outerPixels = outerValue * outerRadius;
		auto outerRect = QRectF(myrtlrect(bgPosition.x(), bgPosition.y(), _stFrom->button.rippleAreaSize, _stFrom->button.rippleAreaSize));
		outerRect = outerRect.marginsAdded(QMarginsF(outerPixels, outerPixels, outerPixels, outerPixels));

		PainterHighQualityEnabler hq(p);
		if (paintFrom) {
			p.setBrush(_stFrom->outerBg);
		} else if (paintTo) {
			p.setBrush(_stTo->outerBg);
		} else {
			p.setBrush(anim::brush(_stFrom->outerBg, _stTo->outerBg, _progress));
		}
		p.setPen(Qt::NoPen);
		p.drawEllipse(outerRect);
	}

	if (paintFrom) {
		p.drawPixmap(bgPosition, _bgFrom);
	} else if (paintTo) {
		p.drawPixmap(bgPosition, _bgTo);
	} else {
		style::colorizeImage(_bgMask, anim::color(_stFrom->bg, _stTo->bg, _progress), &_bg);
		p.drawImage(bgPosition, _bg);
	}

	auto rippleColorInterpolated = QColor();
	auto rippleColorOverride = &rippleColorInterpolated;
	if (paintFrom) {
		rippleColorOverride = nullptr;
	} else if (paintTo) {
		rippleColorOverride = &_stTo->button.ripple.color->c;
	} else {
		rippleColorInterpolated = anim::color(_stFrom->button.ripple.color, _stTo->button.ripple.color, _progress);
	}
	paintRipple(p, _stFrom->button.rippleAreaPosition.x(), _stFrom->button.rippleAreaPosition.y(), ms, rippleColorOverride);

	auto positionFrom = iconPosition(_stFrom);
	if (paintFrom) {
		_stFrom->button.icon.paint(p, positionFrom, width());
	} else {
		auto positionTo = iconPosition(_stTo);
		if (paintTo) {
			_stTo->button.icon.paint(p, positionTo, width());
		} else {
			mixIconMasks();
			style::colorizeImage(_iconMixedMask, st::callIconFg->c, &_iconMixed);
			p.drawImage(myrtlpoint(_stFrom->button.rippleAreaPosition), _iconMixed);
		}
	}
}

QPoint Panel::Button::iconPosition(not_null<const style::CallButton*> st) const {
	auto result = st->button.iconPosition;
	if (result.x() < 0) {
		result.setX((width() - st->button.icon.width()) / 2);
	}
	if (result.y() < 0) {
		result.setY((height() - st->button.icon.height()) / 2);
	}
	return result;
}

void Panel::Button::mixIconMasks() {
	_iconMixedMask.fill(Qt::black);

	Painter p(&_iconMixedMask);
	PainterHighQualityEnabler hq(p);
	auto paintIconMask = [this, &p](const QImage &mask, float64 angle) {
		auto skipFrom = _stFrom->button.rippleAreaSize / 2;
		p.translate(skipFrom, skipFrom);
		p.rotate(angle);
		p.translate(-skipFrom, -skipFrom);
		p.drawImage(0, 0, mask);
	};
	p.save();
	paintIconMask(_iconFrom, (_stFrom->angle - _stTo->angle) * _progress);
	p.restore();
	p.setOpacity(_progress);
	paintIconMask(_iconTo, (_stTo->angle - _stFrom->angle) * (1. - _progress));
}

void Panel::Button::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);

	auto over = isOver();
	auto wasOver = static_cast<bool>(was & StateFlag::Over);
	if (over != wasOver) {
		update();
	}
}

QPoint Panel::Button::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _stFrom->button.rippleAreaPosition;
}

QImage Panel::Button::prepareRippleMask() const {
	return Ui::RippleAnimation::ellipseMask(QSize(_stFrom->button.rippleAreaSize, _stFrom->button.rippleAreaSize));
}

Panel::Panel(not_null<Call*> call)
: _call(call)
, _user(call->user())
, _answerHangupRedial(this, st::callAnswer, &st::callHangup)
, _decline(this, object_ptr<Button>(this, st::callHangup))
, _cancel(this, object_ptr<Button>(this, st::callCancel))
, _mute(this, st::callMuteToggle)
, _name(this, st::callName)
, _status(this, st::callStatus)
, _signalBars(this, call, st::callPanelSignalBars) {
	_decline->setDuration(st::callPanelDuration);
	_cancel->setDuration(st::callPanelDuration);

	setMouseTracking(true);
	setWindowIcon(Window::CreateIcon());
	initControls();
	initLayout();
	showAndActivate();
}

void Panel::showAndActivate() {
	toggleOpacityAnimation(true);
	raise();
	setWindowState(windowState() | Qt::WindowActive);
	activateWindow();
	setFocus();
}

void Panel::replaceCall(not_null<Call*> call) {
	_call = call;
	_user = call->user();
	reinitControls();
	updateControlsGeometry();
}

bool Panel::eventHook(QEvent *e) {
	if (e->type() == QEvent::WindowDeactivate) {
		if (_call && _call->state() == State::Established) {
			hideDeactivated();
		}
	}
	return RpWidget::eventHook(e);
}

void Panel::hideDeactivated() {
	toggleOpacityAnimation(false);
}

void Panel::initControls() {
	_hangupShown = (_call->type() == Type::Outgoing);
	_mute->setClickedCallback([this] {
		if (_call) {
			_call->setMute(!_call->isMute());
		}
	});
	subscribe(_call->muteChanged(), [this](bool mute) {
		_mute->setIconOverride(mute ? &st::callUnmuteIcon : nullptr);
	});
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::NameChanged, [this](const Notify::PeerUpdate &update) {
		if (!_call || update.peer != _call->user()) {
			return;
		}
		_name->setText(App::peerName(_call->user()));
		updateControlsGeometry();
	}));
	_updateDurationTimer.setCallback([this] {
		if (_call) {
			updateStatusText(_call->state());
		}
	});
	_updateOuterRippleTimer.setCallback([this] {
		if (_call) {
			_answerHangupRedial->setOuterValue(_call->getWaitingSoundPeakValue());
		} else {
			_answerHangupRedial->setOuterValue(0.);
			_updateOuterRippleTimer.cancel();
		}
	});
	_answerHangupRedial->setClickedCallback([this] {
		if (!_call || _hangupShownProgress.animating()) {
			return;
		}
		auto state = _call->state();
		if (state == State::Busy) {
			_call->redial();
		} else if (_call->isIncomingWaiting()) {
			_call->answer();
		} else {
			_call->hangup();
		}
	});
	auto hangupCallback = [this] {
		if (_call) {
			_call->hangup();
		}
	};
	_decline->entity()->setClickedCallback(hangupCallback);
	_cancel->entity()->setClickedCallback(hangupCallback);

	reinitControls();

	_decline->finishAnimating();
	_cancel->finishAnimating();
}

void Panel::reinitControls() {
	Expects(_call != nullptr);

	unsubscribe(base::take(_stateChangedSubscription));
	_stateChangedSubscription = subscribe(
		_call->stateChanged(),
		[=](State state) { stateChanged(state); });
	stateChanged(_call->state());

	_signalBars.create(
		this,
		_call,
		st::callPanelSignalBars,
		[=] { rtlupdate(signalBarsRect()); });

	_name->setText(App::peerName(_call->user()));
	updateStatusText(_call->state());
}

void Panel::initLayout() {
	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint) | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint | Qt::Dialog);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);

	initGeometry();

	Notify::PeerUpdateValue(
		_user,
		Notify::PeerUpdate::Flag::PhotoChanged
	) | rpl::start_with_next(
		[this] { processUserPhoto(); },
		lifetime());
	subscribe(Auth().downloaderTaskFinished(), [this] {
		refreshUserPhoto();
	});
	createDefaultCacheImage();

	Platform::InitOnTopPanel(this);
}

void Panel::toggleOpacityAnimation(bool visible) {
	if (!_call || _visible == visible) {
		return;
	}

	_visible = visible;
	if (_useTransparency) {
		if (_animationCache.isNull()) {
			showControls();
			_animationCache = Ui::GrabWidget(this);
			hideChildren();
		}
		_opacityAnimation.start(
			[this] { update(); },
			_visible ? 0. : 1.,
			_visible ? 1. : 0.,
			st::callPanelDuration,
			_visible ? anim::easeOutCirc : anim::easeInCirc);
	}
	if (isHidden() && _visible) {
		show();
	}
}

void Panel::finishAnimating() {
	_animationCache = QPixmap();
	if (_call) {
		if (!_visible) {
			hide();
		} else {
			showControls();
		}
	} else {
		destroyDelayed();
	}
}

void Panel::showControls() {
	Expects(_call != nullptr);
	showChildren();
	_decline->setVisible(_decline->toggled());
	_cancel->setVisible(_cancel->toggled());
}

void Panel::destroyDelayed() {
	hide();
	crl::on_main(this, [=] {
		delete this;
	});
}

void Panel::hideAndDestroy() {
	toggleOpacityAnimation(false);
	_call = nullptr;
	if (_animationCache.isNull()) {
		destroyDelayed();
	}
}

void Panel::processUserPhoto() {
	if (!_user->userpicLoaded()) {
		_user->loadUserpic(true);
	}
	const auto photo = _user->userpicPhotoId()
		? Auth().data().photo(_user->userpicPhotoId()).get()
		: nullptr;
	if (isGoodUserPhoto(photo)) {
		photo->full->load(_user->userpicPhotoOrigin(), true);
	} else if (_user->userpicPhotoUnknown() || (photo && !photo->date)) {
		Auth().api().requestFullPeer(_user);
	}
	refreshUserPhoto();
}

void Panel::refreshUserPhoto() {
	const auto photo = _user->userpicPhotoId()
		? Auth().data().photo(_user->userpicPhotoId()).get()
		: nullptr;
	const auto isNewPhoto = [&](not_null<PhotoData*> photo) {
		return photo->full->loaded()
			&& (photo->id != _userPhotoId || !_userPhotoFull);
	};
	if (isGoodUserPhoto(photo) && isNewPhoto(photo)) {
		_userPhotoId = photo->id;
		_userPhotoFull = true;
		createUserpicCache(photo->full, _user->userpicPhotoOrigin());
	} else if (_userPhoto.isNull()) {
		createUserpicCache(_user->currentUserpic(), _user->userpicOrigin());
	}
}

void Panel::createUserpicCache(ImagePtr image, Data::FileOrigin origin) {
	auto size = st::callWidth * cIntRetinaFactor();
	auto options = _useTransparency ? (Images::Option::RoundedLarge | Images::Option::RoundedTopLeft | Images::Option::RoundedTopRight | Images::Option::Smooth) : Images::Option::None;
	if (image) {
		auto width = image->width();
		auto height = image->height();
		if (width > height) {
			width = qMax((width * size) / height, 1);
			height = size;
		} else {
			height = qMax((height * size) / width, 1);
			width = size;
		}
		_userPhoto = image->pixNoCache(
			origin,
			width,
			height,
			options,
			st::callWidth,
			st::callWidth);
		_userPhoto.setDevicePixelRatio(cRetinaFactor());
	} else {
		auto filled = QImage(QSize(st::callWidth, st::callWidth) * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
		filled.setDevicePixelRatio(cRetinaFactor());
		{
			Painter p(&filled);
			Ui::EmptyUserpic(
				Data::PeerUserpicColor(_user->id),
				_user->name
			).paintSquare(p, 0, 0, st::callWidth, st::callWidth);
		}
		Images::prepareRound(filled, ImageRoundRadius::Large, RectPart::TopLeft | RectPart::TopRight);
		_userPhoto = App::pixmapFromImageInPlace(std::move(filled));
	}
	refreshCacheImageUserPhoto();

	update();
}

bool Panel::isGoodUserPhoto(PhotoData *photo) {
	if (!photo || !photo->date) {
		return false;
	}
	auto badAspect = [](int a, int b) {
		return a > 10 * b;
	};
	auto width = photo->full->width();
	auto height = photo->full->height();
	return !badAspect(width, height) && !badAspect(height, width);
}

void Panel::initGeometry() {
	auto center = Messenger::Instance().getPointForCallPanelCenter();
	_useTransparency = Platform::TranslucentWindowsSupported(center);
	setAttribute(Qt::WA_OpaquePaintEvent, !_useTransparency);
	_padding = _useTransparency ? st::callShadow.extend : style::margins(st::lineWidth, st::lineWidth, st::lineWidth, st::lineWidth);
	_contentTop = _padding.top() + st::callWidth;
	auto screen = QApplication::desktop()->screenGeometry(center);
	auto rect = QRect(0, 0, st::callWidth, st::callHeight);
	setGeometry(rect.translated(center - rect.center()).marginsAdded(_padding));
	createBottomImage();
	updateControlsGeometry();
}

void Panel::createBottomImage() {
	if (!_useTransparency) {
		return;
	}
	auto bottomWidth = width();
	auto bottomHeight = height() - _padding.top() - st::callWidth;
	auto image = QImage(QSize(bottomWidth, bottomHeight) * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	{
		Painter p(&image);
		Ui::Shadow::paint(p, QRect(_padding.left(), 0, st::callWidth, bottomHeight - _padding.bottom()), width(), st::callShadow, RectPart::Left | RectPart::Right | RectPart::Bottom);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(st::callBg);
		p.setPen(Qt::NoPen);
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(myrtlrect(_padding.left(), -st::callRadius, st::callWidth, bottomHeight - _padding.bottom() + st::callRadius), st::callRadius, st::callRadius);
	}
	_bottomCache = App::pixmapFromImageInPlace(std::move(image));
}

void Panel::createDefaultCacheImage() {
	if (!_useTransparency || !_cache.isNull()) {
		return;
	}
	auto cache = QImage(size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(cRetinaFactor());
	cache.fill(Qt::transparent);
	{
		Painter p(&cache);
		auto inner = rect().marginsRemoved(_padding);
		Ui::Shadow::paint(p, inner, width(), st::callShadow);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(st::callBg);
		p.setPen(Qt::NoPen);
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(myrtlrect(inner), st::callRadius, st::callRadius);
	}
	_cache = App::pixmapFromImageInPlace(std::move(cache));
}

void Panel::refreshCacheImageUserPhoto() {
	auto cache = QImage(size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(cRetinaFactor());
	cache.fill(Qt::transparent);
	{
		Painter p(&cache);
		Ui::Shadow::paint(p, QRect(_padding.left(), _padding.top(), st::callWidth, st::callWidth), width(), st::callShadow, RectPart::Top | RectPart::Left | RectPart::Right);
		p.drawPixmapLeft(_padding.left(), _padding.top(), width(), _userPhoto);
		p.drawPixmapLeft(0, _padding.top() + st::callWidth, width(), _bottomCache);
	}
	_cache = App::pixmapFromImageInPlace(std::move(cache));
}

void Panel::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void Panel::updateControlsGeometry() {
	_name->moveToLeft((width() - _name->width()) / 2, _contentTop + st::callNameTop);
	updateStatusGeometry();

	auto controlsTop = _contentTop + st::callControlsTop;
	auto bothWidth = _answerHangupRedial->width() + st::callControlsSkip + st::callCancel.button.width;
	_decline->moveToLeft((width() - bothWidth) / 2, controlsTop);
	_cancel->moveToLeft((width() - bothWidth) / 2, controlsTop);

	updateHangupGeometry();

	_mute->moveToRight(_padding.right() + st::callMuteRight, controlsTop);

	const auto skip = st::callSignalMargin + st::callSignalPadding;
	const auto delta = (_signalBars->width() - _signalBars->height());
	_signalBars->moveToLeft(
		_padding.left() + skip,
		_padding.top() + skip + delta / 2);
}

void Panel::updateHangupGeometry() {
	auto singleWidth = _answerHangupRedial->width();
	auto bothWidth = singleWidth + st::callControlsSkip + st::callCancel.button.width;
	auto rightFrom = (width() - bothWidth) / 2;
	auto rightTo = (width() - singleWidth) / 2;
	auto hangupProgress = _hangupShownProgress.current(_hangupShown ? 1. : 0.);
	auto hangupRight = anim::interpolate(rightFrom, rightTo, hangupProgress);
	auto controlsTop = _contentTop + st::callControlsTop;
	_answerHangupRedial->moveToRight(hangupRight, controlsTop);
	_answerHangupRedial->setProgress(hangupProgress);
}

void Panel::updateStatusGeometry() {
	_status->moveToLeft((width() - _status->width()) / 2, _contentTop + st::callStatusTop);
}

void Panel::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (!_animationCache.isNull()) {
		auto opacity = _opacityAnimation.current(getms(), _call ? 1. : 0.);
		if (!_opacityAnimation.animating()) {
			finishAnimating();
			if (!_call || isHidden()) return;
		} else {
			Platform::StartTranslucentPaint(p, e);
			p.setOpacity(opacity);

			PainterHighQualityEnabler hq(p);
			auto marginRatio = (1. - opacity) / 5;
			auto marginWidth = qRound(width() * marginRatio);
			auto marginHeight = qRound(height() * marginRatio);
			p.drawPixmap(rect().marginsRemoved(QMargins(marginWidth, marginHeight, marginWidth, marginHeight)), _animationCache, QRect(QPoint(0, 0), _animationCache.size()));
			return;
		}
	}

	if (_useTransparency) {
		Platform::StartTranslucentPaint(p, e);
		p.drawPixmapLeft(0, 0, width(), _cache);
	} else {
		p.drawPixmapLeft(_padding.left(), _padding.top(), width(), _userPhoto);
		auto callBgOpaque = st::callBg->c;
		callBgOpaque.setAlpha(255);
		auto brush = QBrush(callBgOpaque);
		p.fillRect(0, 0, width(), _padding.top(), brush);
		p.fillRect(myrtlrect(0, _padding.top(), _padding.left(), _contentTop - _padding.top()), brush);
		p.fillRect(myrtlrect(width() - _padding.right(), _padding.top(), _padding.right(), _contentTop - _padding.top()), brush);
		p.fillRect(0, _contentTop, width(), height() - _contentTop, brush);
	}

	if (_signalBars->isDisplayed()) {
		paintSignalBarsBg(p);
	}

	if (!_fingerprint.empty()) {
		App::roundRect(p, _fingerprintArea, st::callFingerprintBg, ImageRoundRadius::Small);

		const auto realSize = Ui::Emoji::GetSizeLarge();
		const auto size = realSize / cIntRetinaFactor();
		auto left = _fingerprintArea.left() + st::callFingerprintPadding.left();
		const auto top = _fingerprintArea.top() + st::callFingerprintPadding.top();
		for (const auto emoji : _fingerprint) {
			Ui::Emoji::Draw(p, emoji, realSize, left, top);
			left += st::callFingerprintSkip + size;
		}
	}
}

QRect Panel::signalBarsRect() const {
	const auto size = 2 * st::callSignalPadding + _signalBars->width();
	return QRect(
		_padding.left() + st::callSignalMargin,
		_padding.top() + st::callSignalMargin,
		size,
		size);
}

void Panel::paintSignalBarsBg(Painter &p) {
	App::roundRect(
		p,
		signalBarsRect(),
		st::callFingerprintBg,
		ImageRoundRadius::Small);
}

void Panel::closeEvent(QCloseEvent *e) {
	if (_call) {
		_call->hangup();
	}
}

void Panel::mousePressEvent(QMouseEvent *e) {
	auto dragArea = myrtlrect(_padding.left(), _padding.top(), st::callWidth, st::callWidth);
	if (e->button() == Qt::LeftButton) {
		if (dragArea.contains(e->pos())) {
			_dragging = true;
			_dragStartMousePosition = e->globalPos();
			_dragStartMyPosition = QPoint(x(), y());
		} else if (!rect().contains(e->pos())) {
			if (_call && _call->state() == State::Established) {
				hideDeactivated();
			}
		}
	}
}

void Panel::mouseMoveEvent(QMouseEvent *e) {
	if (_dragging) {
		Ui::Tooltip::Hide();
		if (!(e->buttons() & Qt::LeftButton)) {
			_dragging = false;
		} else {
			move(_dragStartMyPosition + (e->globalPos() - _dragStartMousePosition));
		}
	} else if (_fingerprintArea.contains(e->pos())) {
		Ui::Tooltip::Show(kTooltipShowTimeoutMs, this);
	} else {
		Ui::Tooltip::Hide();
	}
}

void Panel::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_dragging = false;
	}
}

void Panel::leaveEventHook(QEvent *e) {
	Ui::Tooltip::Hide();
}

void Panel::leaveToChildEvent(QEvent *e, QWidget *child) {
	Ui::Tooltip::Hide();
}

QString Panel::tooltipText() const {
	return lng_call_fingerprint_tooltip(lt_user, App::peerName(_user));
}

QPoint Panel::tooltipPos() const {
	return QCursor::pos();
}

bool Panel::tooltipWindowActive() const {
	return !isHidden();
}

void Panel::stateChanged(State state) {
	updateStatusText(state);

	if (_call) {
		if ((state != State::HangingUp)
			&& (state != State::Ended)
			&& (state != State::EndedByOtherDevice)
			&& (state != State::FailedHangingUp)
			&& (state != State::Failed)) {
			auto toggleButton = [this](auto &&button, bool visible) {
				button->toggle(
					visible,
					isHidden()
						? anim::type::instant
						: anim::type::normal);
			};
			auto incomingWaiting = _call->isIncomingWaiting();
			if (incomingWaiting) {
				_updateOuterRippleTimer.callEach(Call::kSoundSampleMs);
			}
			toggleButton(_decline, incomingWaiting);
			toggleButton(_cancel, (state == State::Busy));
			auto hangupShown = !_decline->toggled()
				&& !_cancel->toggled();
			if (_hangupShown != hangupShown) {
				_hangupShown = hangupShown;
				_hangupShownProgress.start([this] { updateHangupGeometry(); }, _hangupShown ? 0. : 1., _hangupShown ? 1. : 0., st::callPanelDuration, anim::sineInOut);
			}
			if (_fingerprint.empty() && _call->isKeyShaForFingerprintReady()) {
				fillFingerprint();
			}
		}
	}

	if (windowHandle()) {
		// First stateChanged() is called before the first Platform::InitOnTopPanel(this).
		if ((state == State::Starting) || (state == State::WaitingIncoming)) {
			Platform::ReInitOnTopPanel(this);
		} else {
			Platform::DeInitOnTopPanel(this);
		}
	}
	if (state == State::Established) {
		if (!isActiveWindow()) {
			hideDeactivated();
		}
	}
}

void Panel::fillFingerprint() {
	Expects(_call != nullptr);
	_fingerprint = ComputeEmojiFingerprint(_call);

	auto realSize = Ui::Emoji::GetSizeLarge();
	auto size = realSize / cIntRetinaFactor();
	auto count = _fingerprint.size();
	auto rectWidth = count * size + (count - 1) * st::callFingerprintSkip;
	auto rectHeight = size;
	auto left = (width() - rectWidth) / 2;
	auto top = _contentTop - st::callFingerprintBottom - st::callFingerprintPadding.bottom() - size;
	_fingerprintArea = QRect(left, top, rectWidth, rectHeight).marginsAdded(st::callFingerprintPadding);

	update();
}

void Panel::updateStatusText(State state) {
	auto statusText = [this, state]() -> QString {
		switch (state) {
		case State::Starting:
		case State::WaitingInit:
		case State::WaitingInitAck: return lang(lng_call_status_connecting);
		case State::Established: {
			if (_call) {
				auto durationMs = _call->getDurationMs();
				auto durationSeconds = durationMs / 1000;
				startDurationUpdateTimer(durationMs);
				return formatDurationText(durationSeconds);
			}
			return lang(lng_call_status_ended);
		} break;
		case State::FailedHangingUp:
		case State::Failed: return lang(lng_call_status_failed);
		case State::HangingUp: return lang(lng_call_status_hanging);
		case State::Ended:
		case State::EndedByOtherDevice: return lang(lng_call_status_ended);
		case State::ExchangingKeys: return lang(lng_call_status_exchanging);
		case State::Waiting: return lang(lng_call_status_waiting);
		case State::Requesting: return lang(lng_call_status_requesting);
		case State::WaitingIncoming: return lang(lng_call_status_incoming);
		case State::Ringing: return lang(lng_call_status_ringing);
		case State::Busy: return lang(lng_call_status_busy);
		}
		Unexpected("State in stateChanged()");
	};
	_status->setText(statusText());
	updateStatusGeometry();
}

void Panel::startDurationUpdateTimer(TimeMs currentDuration) {
	auto msTillNextSecond = 1000 - (currentDuration % 1000);
	_updateDurationTimer.callOnce(msTillNextSecond + 5);
}

} // namespace Calls
