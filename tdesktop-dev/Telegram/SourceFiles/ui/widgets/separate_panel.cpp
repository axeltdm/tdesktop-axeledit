/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/separate_panel.h"

#include "window/main_window.h"
#include "platform/platform_specific.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/toast/toast.h"
#include "ui/widgets/tooltip.h"
#include "window/layer_widget.h"
#include "window/themes/window_theme.h"
#include "messenger.h"
#include "styles/style_widgets.h"
#include "styles/style_info.h"
#include "styles/style_calls.h"

namespace Ui {

SeparatePanel::SeparatePanel()
: _close(this, st::separatePanelClose)
, _back(this, object_ptr<Ui::IconButton>(this, st::separatePanelBack))
, _body(this) {
	setMouseTracking(true);
	setWindowIcon(Window::CreateIcon());
	initControls();
	initLayout();
}

void SeparatePanel::setTitle(rpl::producer<QString> title) {
	_title.create(this, std::move(title), st::separatePanelTitle);
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_title->show();
	updateTitleGeometry(width());
}

void SeparatePanel::initControls() {
	widthValue(
	) | rpl::start_with_next([=](int width) {
		_back->moveToLeft(_padding.left(), _padding.top());
		_close->moveToRight(_padding.right(), _padding.top());
		if (_title) {
			updateTitleGeometry(width);
		}
	}, lifetime());

	_back->toggledValue(
	) | rpl::start_with_next([=](bool toggled) {
		_titleLeft.start(
			[=] { updateTitlePosition(); },
			toggled ? 0. : 1.,
			toggled ? 1. : 0.,
			st::fadeWrapDuration);
	}, _back->lifetime());
	_back->hide(anim::type::instant);
	_titleLeft.finish();
}

void SeparatePanel::updateTitleGeometry(int newWidth) {
	_title->resizeToWidth(newWidth
		- _padding.left() - _back->width()
		- _padding.right() - _close->width());
	updateTitlePosition();
}

void SeparatePanel::updateTitlePosition() {
	if (!_title) {
		return;
	}
	const auto progress = _titleLeft.current(_back->toggled() ? 1. : 0.);
	const auto left = anim::interpolate(
		st::separatePanelTitleLeft,
		_back->width() + st::separatePanelTitleSkip,
		progress);
	_title->moveToLeft(
		_padding.left() + left,
		_padding.top() + st::separatePanelTitleTop);
}

rpl::producer<> SeparatePanel::backRequests() const {
	return rpl::merge(
		_back->entity()->clicks(
		) | rpl::map([] { return rpl::empty_value(); }),
		_synteticBackRequests.events());
}

rpl::producer<> SeparatePanel::closeRequests() const {
	return rpl::merge(
		_close->clicks(
		) | rpl::map([] { return rpl::empty_value(); }),
		_userCloseRequests.events());
}

rpl::producer<> SeparatePanel::closeEvents() const {
	return _closeEvents.events();
}

void SeparatePanel::setBackAllowed(bool allowed) {
	if (allowed != _back->toggled()) {
		_back->toggle(allowed, anim::type::normal);
	}
}

void SeparatePanel::setHideOnDeactivate(bool hideOnDeactivate) {
	_hideOnDeactivate = hideOnDeactivate;
	if (!_hideOnDeactivate) {
		showAndActivate();
	} else if (!isActiveWindow()) {
		LOG(("Export Info: Panel Hide On Inactive Change."));
		hideGetDuration();
	}
}

void SeparatePanel::showAndActivate() {
	toggleOpacityAnimation(true);
	raise();
	setWindowState(windowState() | Qt::WindowActive);
	activateWindow();
	setFocus();
}

void SeparatePanel::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape && _back->toggled()) {
		_synteticBackRequests.fire({});
	}
	return RpWidget::keyPressEvent(e);
}

bool SeparatePanel::eventHook(QEvent *e) {
	if (e->type() == QEvent::WindowDeactivate && _hideOnDeactivate) {
		LOG(("Export Info: Panel Hide On Inactive Window."));
		hideGetDuration();
	}
	return RpWidget::eventHook(e);
}

void SeparatePanel::initLayout() {
	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint)
		| Qt::WindowStaysOnTopHint
		| Qt::NoDropShadowWindowHint
		| Qt::Dialog);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);

	createBorderImage();
	subscribe(Window::Theme::Background(), [=](
			const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			createBorderImage();
			Ui::ForceFullRepaint(this);
		}
	});

	Platform::InitOnTopPanel(this);
}

void SeparatePanel::createBorderImage() {
	const auto shadowPadding = st::callShadow.extend;
	const auto cacheSize = st::separatePanelBorderCacheSize;
	auto cache = QImage(
		cacheSize * cIntRetinaFactor(),
		cacheSize * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(cRetinaFactor());
	cache.fill(Qt::transparent);
	{
		Painter p(&cache);
		auto inner = QRect(0, 0, cacheSize, cacheSize).marginsRemoved(
			shadowPadding);
		Ui::Shadow::paint(p, inner, cacheSize, st::callShadow);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(st::windowBg);
		p.setPen(Qt::NoPen);
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(
			myrtlrect(inner),
			st::callRadius,
			st::callRadius);
	}
	_borderParts = App::pixmapFromImageInPlace(std::move(cache));
}

void SeparatePanel::toggleOpacityAnimation(bool visible) {
	if (_visible == visible) {
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
			[this] { opacityCallback(); },
			_visible ? 0. : 1.,
			_visible ? 1. : 0.,
			st::callPanelDuration,
			_visible ? anim::easeOutCirc : anim::easeInCirc);
	}
	if (isHidden() && _visible) {
		show();
	}
}

void SeparatePanel::opacityCallback() {
	update();
	if (!_visible && !_opacityAnimation.animating()) {
		finishAnimating();
	}
}

void SeparatePanel::finishAnimating() {
	_animationCache = QPixmap();
	if (_visible) {
		showControls();
		if (_inner) {
			_inner->setFocus();
		}
	} else {
		finishClose();
	}
}

void SeparatePanel::showControls() {
	showChildren();
	if (!_back->toggled()) {
		_back->setVisible(false);
	}
}

void SeparatePanel::finishClose() {
	hide();
	crl::on_main(this, [=] {
		if (isHidden() && !_visible && !_opacityAnimation.animating()) {
			LOG(("Export Info: Panel Closed."));
			_closeEvents.fire({});
		}
	});
}

int SeparatePanel::hideGetDuration() {
	LOG(("Export Info: Panel Hide Requested."));
	toggleOpacityAnimation(false);
	if (_animationCache.isNull()) {
		finishClose();
		return 0;
	}
	return st::callPanelDuration;
}

void SeparatePanel::showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) {
	ensureLayerCreated();
	_layer->showBox(std::move(box), options, animated);
}

void SeparatePanel::showToast(const QString &text) {
	auto toast = Ui::Toast::Config();
	toast.text = text;
	Ui::Toast::Show(this, toast);
}

void SeparatePanel::ensureLayerCreated() {
	if (_layer) {
		return;
	}
	_layer = base::make_unique_q<Window::LayerStackWidget>(_body);
	_layer->setHideByBackgroundClick(false);
	_layer->move(0, 0);
	_body->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_layer->resize(size);
	}, _layer->lifetime());
	_layer->hideFinishEvents(
	) | rpl::start_with_next([=]{
		if (Ui::InFocusChain(_layer)) {
			setFocus();
		}
		_layer = nullptr;
	}, _layer->lifetime());
}

void SeparatePanel::showInner(base::unique_qptr<Ui::RpWidget> inner) {
	Expects(!size().isEmpty());

	_inner = std::move(inner);
	_inner->setParent(_body);
	_inner->move(0, 0);
	_body->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_inner->resize(size);
	}, _inner->lifetime());
	_inner->show();

	if (_layer) {
		_layer->raise();
	}

	showAndActivate();
}

void SeparatePanel::focusInEvent(QFocusEvent *e) {
	crl::on_main(this, [=] {
		if (_layer) {
			_layer->setInnerFocus();
		} else if (_inner && !_inner->isHidden()) {
			_inner->setFocus();
		}
	});
}

void SeparatePanel::setInnerSize(QSize size) {
	Expects(!size.isEmpty());

	if (rect().isEmpty()) {
		initGeometry(size);
	} else {
		updateGeometry(size);
	}
}

void SeparatePanel::initGeometry(QSize size) {
	const auto center = Messenger::Instance().getPointForCallPanelCenter();
	_useTransparency = Platform::TranslucentWindowsSupported(center);
	_padding = _useTransparency
		? st::callShadow.extend
		: style::margins(
			st::lineWidth,
			st::lineWidth,
			st::lineWidth,
			st::lineWidth);
	setAttribute(Qt::WA_OpaquePaintEvent, !_useTransparency);
	const auto screen = QApplication::desktop()->screenGeometry(center);
	const auto rect = QRect(QPoint(), size);
	setGeometry(
		rect.translated(center - rect.center()).marginsAdded(_padding));
	updateControlsGeometry();
}

void SeparatePanel::updateGeometry(QSize size) {
	setGeometry(
		x(),
		y(),
		_padding.left() + size.width() + _padding.right(),
		_padding.top() + size.height() + _padding.bottom());
	updateControlsGeometry();
	update();
}

void SeparatePanel::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void SeparatePanel::updateControlsGeometry() {
	const auto top = _padding.top() + st::separatePanelTitleHeight;
	_body->setGeometry(
		_padding.left(),
		top,
		width() - _padding.left() - _padding.right(),
		height() - top - _padding.bottom());
}

void SeparatePanel::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (!_animationCache.isNull()) {
		auto opacity = _opacityAnimation.current(
			getms(),
			_visible ? 1. : 0.);
		if (!_opacityAnimation.animating()) {
			finishAnimating();
			if (isHidden()) return;
		} else {
			Platform::StartTranslucentPaint(p, e);
			p.setOpacity(opacity);

			PainterHighQualityEnabler hq(p);
			auto marginRatio = (1. - opacity) / 5;
			auto marginWidth = qRound(width() * marginRatio);
			auto marginHeight = qRound(height() * marginRatio);
			p.drawPixmap(
				rect().marginsRemoved(
					QMargins(
						marginWidth,
						marginHeight,
						marginWidth,
						marginHeight)),
				_animationCache,
				QRect(QPoint(0, 0), _animationCache.size()));
			return;
		}
	}

	if (_useTransparency) {
		Platform::StartTranslucentPaint(p, e);
		paintShadowBorder(p);
	} else {
		paintOpaqueBorder(p);
	}
}

void SeparatePanel::paintShadowBorder(Painter &p) const {
	const auto factor = cIntRetinaFactor();
	const auto size = st::separatePanelBorderCacheSize;
	const auto part1 = size / 3;
	const auto part2 = size - part1;
	const auto corner = QSize(part1, part1) * factor;

	const auto topleft = QRect(QPoint(0, 0), corner);
	p.drawPixmap(QRect(0, 0, part1, part1), _borderParts, topleft);

	const auto topright = QRect(QPoint(part2, 0) * factor, corner);
	p.drawPixmap(
		QRect(width() - part1, 0, part1, part1),
		_borderParts,
		topright);

	const auto bottomleft = QRect(QPoint(0, part2) * factor, corner);
	p.drawPixmap(
		QRect(0, height() - part1, part1, part1),
		_borderParts,
		bottomleft);

	const auto bottomright = QRect(QPoint(part2, part2) * factor, corner);
	p.drawPixmap(
		QRect(width() - part1, height() - part1, part1, part1),
		_borderParts,
		bottomright);

	const auto left = QRect(
		QPoint(0, part1) * factor,
		QSize(_padding.left(), part2 - part1) * factor);
	p.drawPixmap(
		QRect(0, part1, _padding.left(), height() - 2 * part1),
		_borderParts,
		left);

	const auto top = QRect(
		QPoint(part1, 0) * factor,
		QSize(part2 - part1, _padding.top() + st::callRadius) * factor);
	p.drawPixmap(
		QRect(
			part1,
			0,
			width() - 2 * part1,
			_padding.top() + st::callRadius),
		_borderParts,
		top);

	const auto right = QRect(
		QPoint(size - _padding.right(), part1) * factor,
		QSize(_padding.right(), part2 - part1) * factor);
	p.drawPixmap(
		QRect(
			width() - _padding.right(),
			part1,
			_padding.right(),
			height() - 2 * part1),
		_borderParts,
		right);

	const auto bottom = QRect(
		QPoint(part1, size - _padding.bottom() - st::callRadius) * factor,
		QSize(part2 - part1, _padding.bottom() + st::callRadius) * factor);
	p.drawPixmap(
		QRect(
			part1,
			height() - _padding.bottom() - st::callRadius,
			width() - 2 * part1,
			_padding.bottom() + st::callRadius),
		_borderParts,
		bottom);

	p.fillRect(
		_padding.left(),
		_padding.top() + st::callRadius,
		width() - _padding.left() - _padding.right(),
		height() - _padding.top() - _padding.bottom() - 2 * st::callRadius,
		st::windowBg);
}

void SeparatePanel::paintOpaqueBorder(Painter &p) const {
	const auto border = st::windowShadowFgFallback;
	p.fillRect(0, 0, width(), _padding.top(), border);
	p.fillRect(
		myrtlrect(
			0,
			_padding.top(),
			_padding.left(),
			height() - _padding.top()),
		border);
	p.fillRect(
		myrtlrect(
			width() - _padding.right(),
			_padding.top(),
			_padding.right(),
			height() - _padding.top()),
		border);
	p.fillRect(
		_padding.left(),
		height() - _padding.bottom(),
		width() - _padding.left() - _padding.right(),
		_padding.bottom(),
		border);

	p.fillRect(
		_padding.left(),
		_padding.top(),
		width() - _padding.left() - _padding.right(),
		height() - _padding.top() - _padding.bottom(),
		st::windowBg);
}

void SeparatePanel::closeEvent(QCloseEvent *e) {
	e->ignore();
	_userCloseRequests.fire({});
}

void SeparatePanel::mousePressEvent(QMouseEvent *e) {
	auto dragArea = myrtlrect(
		_padding.left(),
		_padding.top(),
		width() - _padding.left() - _padding.right(),
		st::separatePanelTitleHeight);
	if (e->button() == Qt::LeftButton) {
		if (dragArea.contains(e->pos())) {
			_dragging = true;
			_dragStartMousePosition = e->globalPos();
			_dragStartMyPosition = QPoint(x(), y());
		} else if (!rect().contains(e->pos()) && _hideOnDeactivate) {
			LOG(("Export Info: Panel Hide On Click."));
			hideGetDuration();
		}
	}
}

void SeparatePanel::mouseMoveEvent(QMouseEvent *e) {
	if (_dragging) {
		if (!(e->buttons() & Qt::LeftButton)) {
			_dragging = false;
		} else {
			move(_dragStartMyPosition
				+ (e->globalPos() - _dragStartMousePosition));
		}
	}
}

void SeparatePanel::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_dragging = false;
	}
}

void SeparatePanel::leaveEventHook(QEvent *e) {
	Ui::Tooltip::Hide();
}

void SeparatePanel::leaveToChildEvent(QEvent *e, QWidget *child) {
	Ui::Tooltip::Hide();
}

} // namespace Ui
