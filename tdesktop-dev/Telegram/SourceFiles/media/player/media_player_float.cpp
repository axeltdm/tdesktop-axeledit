/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_float.h"

#include <rpl/merge.h>
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_media_types.h"
#include "history/history_media.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "media/media_clip_reader.h"
#include "media/media_audio.h"
#include "media/view/media_clip_playback.h"
#include "media/player/media_player_instance.h"
#include "media/player/media_player_round_controller.h"
#include "window/window_controller.h"
#include "window/section_widget.h"
#include "auth_session.h"
#include "styles/style_media_player.h"
#include "styles/style_history.h"

namespace Media {
namespace Player {

Float::Float(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<HistoryItem*> item,
	Fn<void(bool visible)> toggleCallback,
	Fn<void(bool closed)> draggedCallback)
: RpWidget(parent)
, _controller(controller)
, _item(item)
, _toggleCallback(std::move(toggleCallback))
, _draggedCallback(std::move(draggedCallback)) {
	auto media = _item->media();
	Assert(media != nullptr);

	auto document = media->document();
	Assert(document != nullptr);
	Assert(document->isVideoMessage());

	auto margin = st::mediaPlayerFloatMargin;
	auto size = 2 * margin + st::mediaPlayerFloatSize;
	resize(size, size);

	prepareShadow();

	Auth().data().itemRepaintRequest(
	) | rpl::start_with_next([this](auto item) {
		if (_item == item) {
			repaintItem();
		}
	}, lifetime());
	Auth().data().itemRemoved(
	) | rpl::start_with_next([this](auto item) {
		if (_item == item) {
			detach();
		}
	}, lifetime());

	setCursor(style::cur_pointer);
}

void Float::mousePressEvent(QMouseEvent *e) {
	_down = true;
	_downPoint = e->pos();
}

void Float::mouseMoveEvent(QMouseEvent *e) {
	if (_down && (e->pos() - _downPoint).manhattanLength() > QApplication::startDragDistance()) {
		_down = false;
		_drag = true;
		_dragLocalPoint = e->pos();
	} else if (_drag) {
		auto delta = (e->pos() - _dragLocalPoint);
		move(pos() + delta);
		setOpacity(outRatio());
	}
}

float64 Float::outRatio() const {
	auto parent = parentWidget()->rect();
	auto min = 1.;
	if (x() < parent.x()) {
		accumulate_min(min, 1. - (parent.x() - x()) / float64(width()));
	}
	if (y() < parent.y()) {
		accumulate_min(min, 1. - (parent.y() - y()) / float64(height()));
	}
	if (x() + width() > parent.x() + parent.width()) {
		accumulate_min(min, 1. - (x() + width() - parent.x() - parent.width()) / float64(width()));
	}
	if (y() + height() > parent.y() + parent.height()) {
		accumulate_min(min, 1. - (y() + height() - parent.y() - parent.height()) / float64(height()));
	}
	return snap(min, 0., 1.);
}

void Float::mouseReleaseEvent(QMouseEvent *e) {
	if (base::take(_down) && _item) {
		if (const auto controller = _controller->roundVideo(_item)) {
			controller->pauseResume();
		}
	}
	if (_drag) {
		finishDrag(outRatio() < 0.5);
	}
}

void Float::finishDrag(bool closed) {
	_drag = false;
	if (_draggedCallback) {
		_draggedCallback(closed);
	}
}

void Float::mouseDoubleClickEvent(QMouseEvent *e) {
	if (_item) {
		// Handle second click.
		if (const auto controller = _controller->roundVideo(_item)) {
			controller->pauseResume();
		}
		Ui::showPeerHistoryAtItem(_item);
	}
}

void Float::detach() {
	if (_item) {
		_item = nullptr;
		if (_toggleCallback) {
			_toggleCallback(false);
		}
	}
}

void Float::prepareShadow() {
	auto shadow = QImage(size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	shadow.fill(Qt::transparent);
	shadow.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&shadow);
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::shadowFg);
		auto extend = 2 * st::lineWidth;
		p.drawEllipse(getInnerRect().marginsAdded(QMargins(extend, extend, extend, extend)));
	}
	_shadow = App::pixmapFromImageInPlace(Images::prepareBlur(std::move(shadow)));
}

QRect Float::getInnerRect() const {
	auto margin = st::mediaPlayerFloatMargin;
	return rect().marginsRemoved(QMargins(margin, margin, margin, margin));
}

void Float::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.setOpacity(_opacity);
	p.drawPixmap(0, 0, _shadow);

	if (!fillFrame() && _toggleCallback) {
		_toggleCallback(false);
	}

	auto inner = getInnerRect();
	p.drawImage(inner.topLeft(), _frame);

	const auto playback = getPlayback();
	const auto progress = playback ? playback->value(getms()) : 1.;
	if (progress > 0.) {
		auto pen = st::historyVideoMessageProgressFg->p;
		auto was = p.pen();
		pen.setWidth(st::radialLine);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		p.setOpacity(_opacity * st::historyVideoMessageProgressOpacity);

		auto from = QuarterArcLength;
		auto len = -qRound(FullArcLength * progress);
		auto stepInside = st::radialLine / 2;
		{
			PainterHighQualityEnabler hq(p);
			p.drawArc(inner.marginsRemoved(QMargins(stepInside, stepInside, stepInside, stepInside)), from, len);
		}

		//p.setPen(was);
		//p.setOpacity(_opacity);
	}
}

Clip::Reader *Float::getReader() const {
	if (detached()) {
		return nullptr;
	}
	if (const auto controller = _controller->roundVideo(_item)) {
		if (const auto reader = controller->reader()) {
			if (reader->started()) {
				return reader;
			}
		}
	}
	return nullptr;
}

Clip::Playback *Float::getPlayback() const {
	if (detached()) {
		return nullptr;
	}
	if (const auto controller = _controller->roundVideo(_item)) {
		return controller->playback();
	}
	return nullptr;
}

bool Float::hasFrame() const {
	if (const auto reader = getReader()) {
		return !reader->current().isNull();
	}
	return false;
}

bool Float::fillFrame() {
	auto creating = _frame.isNull();
	if (creating) {
		_frame = QImage(
			getInnerRect().size() * cIntRetinaFactor(),
			QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(cRetinaFactor());
	}
	auto frameInner = [&] {
		return QRect(QPoint(), _frame.size() / cIntRetinaFactor());
	};
	if (const auto reader = getReader()) {
		auto frame = reader->current();
		if (!frame.isNull()) {
			_frame.fill(Qt::transparent);

			Painter p(&_frame);
			PainterHighQualityEnabler hq(p);
			p.drawPixmap(frameInner(), frame);
			return true;
		}
	}
	if (creating) {
		_frame.fill(Qt::transparent);

		Painter p(&_frame);
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::imageBg);
		p.drawEllipse(frameInner());
	}
	return false;
}

void Float::repaintItem() {
	update();
	if (hasFrame() && _toggleCallback) {
		_toggleCallback(true);
	}
}


template <typename ToggleCallback, typename DraggedCallback>
FloatController::Item::Item(
	not_null<QWidget*> parent,
	not_null<Window::Controller*> controller,
	not_null<HistoryItem*> item,
	ToggleCallback toggle,
	DraggedCallback dragged)
: animationSide(RectPart::Right)
, column(Window::Column::Second)
, corner(RectPart::TopRight)
, widget(
	parent,
	controller,
	item,
	[=, toggle = std::move(toggle)](bool visible) {
		toggle(this, visible);
	},
	[=, dragged = std::move(dragged)](bool closed) {
		dragged(this, closed);
	}) {
}

FloatController::FloatController(not_null<FloatDelegate*> delegate)
: _delegate(delegate)
, _parent(_delegate->floatPlayerWidget())
, _controller(_delegate->floatPlayerController()) {
	subscribe(_controller->floatPlayerAreaUpdated(), [=] {
		checkVisibility();
	});

	subscribe(Media::Player::instance()->trackChangedNotifier(), [=](
			AudioMsgId::Type type) {
		if (type == AudioMsgId::Type::Voice) {
			checkCurrent();
		}
	});

	startDelegateHandling();
}

void FloatController::replaceDelegate(not_null<FloatDelegate*> delegate) {
	_delegateLifetime.destroy();

	_delegate = delegate;
	_parent = _delegate->floatPlayerWidget();

	// Currently moving floats between windows is not supported.
	Assert(_controller == _delegate->floatPlayerController());

	startDelegateHandling();

	for (const auto &item : _items) {
		item->widget->setParent(_parent);
	}
	checkVisibility();
}

void FloatController::startDelegateHandling() {
	_delegate->floatPlayerCheckVisibilityRequests(
	) | rpl::start_with_next([=] {
		checkVisibility();
	}, _delegateLifetime);

	_delegate->floatPlayerHideAllRequests(
	) | rpl::start_with_next([=] {
		hideAll();
	}, _delegateLifetime);

	_delegate->floatPlayerShowVisibleRequests(
	) | rpl::start_with_next([=] {
		showVisible();
	}, _delegateLifetime);

	_delegate->floatPlayerRaiseAllRequests(
	) | rpl::start_with_next([=] {
		raiseAll();
	}, _delegateLifetime);

	_delegate->floatPlayerUpdatePositionsRequests(
	) | rpl::start_with_next([=] {
		updatePositions();
	}, _delegateLifetime);

	_delegate->floatPlayerFilterWheelEventRequests(
	) | rpl::start_with_next([=](
			const FloatDelegate::FloatPlayerFilterWheelEventRequest &request) {
		*request.result = filterWheelEvent(request.object, request.event);
	}, _delegateLifetime);
}

void FloatController::checkCurrent() {
	const auto state = Media::Player::instance()->current(AudioMsgId::Type::Voice);
	const auto fullId = state.contextId();
	const auto last = current();
	if (last
		&& !last->widget->detached()
		&& last->widget->item()->fullId() == fullId) {
		return;
	}
	if (last) {
		last->widget->detach();
	}
	if (const auto item = App::histItemById(fullId)) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				if (document->isVideoMessage()) {
					create(item);
				}
			}
		}
	}
}

void FloatController::create(not_null<HistoryItem*> item) {
	_items.push_back(std::make_unique<Item>(
		_parent,
		_controller,
		item,
		[=](not_null<Item*> instance, bool visible) {
			instance->hiddenByWidget = !visible;
			toggle(instance);
		},
		[=](not_null<Item*> instance, bool closed) {
			finishDrag(instance, closed);
		}));
	current()->column = Auth().settings().floatPlayerColumn();
	current()->corner = Auth().settings().floatPlayerCorner();
	checkVisibility();
}

void FloatController::toggle(not_null<Item*> instance) {
	auto visible = !instance->hiddenByHistory && !instance->hiddenByWidget && instance->widget->isReady();
	if (instance->visible != visible) {
		instance->widget->resetMouseState();
		instance->visible = visible;
		if (!instance->visibleAnimation.animating() && !instance->hiddenByDrag) {
			auto finalRect = QRect(getPosition(instance), instance->widget->size());
			instance->animationSide = getSide(finalRect.center());
		}
		instance->visibleAnimation.start([=] {
			updatePosition(instance);
		}, visible ? 0. : 1., visible ? 1. : 0., st::slideDuration, visible ? anim::easeOutCirc : anim::linear);
		updatePosition(instance);
	}
}

void FloatController::checkVisibility() {
	const auto instance = current();
	if (!instance) {
		return;
	}

	const auto item = instance->widget->item();
	instance->hiddenByHistory = item
		? _delegate->floatPlayerIsVisible(item)
		: false;
	toggle(instance);
	updatePosition(instance);
}

void FloatController::hideAll() {
	for (const auto &instance : _items) {
		instance->widget->hide();
	}
}

void FloatController::showVisible() {
	for (const auto &instance : _items) {
		if (instance->visible) {
			instance->widget->show();
		}
	}
}

void FloatController::raiseAll() {
	for (const auto &instance : _items) {
		instance->widget->raise();
	}
}

void FloatController::updatePositions() {
	for (const auto &instance : _items) {
		updatePosition(instance.get());
	}
}

std::optional<bool> FloatController::filterWheelEvent(
		not_null<QObject*> object,
		not_null<QEvent*> event) {
	for (const auto &instance : _items) {
		if (instance->widget == object) {
			const auto section = _delegate->floatPlayerGetSection(
				instance->column);
			return section->wheelEventFromFloatPlayer(event);
		}
	}
	return std::nullopt;
}

void FloatController::updatePosition(not_null<Item*> instance) {
	auto visible = instance->visibleAnimation.current(instance->visible ? 1. : 0.);
	if (visible == 0. && !instance->visible) {
		instance->widget->hide();
		if (instance->widget->detached()) {
			InvokeQueued(instance->widget, [=] {
				remove(instance);
			});
		}
		return;
	}

	if (!instance->widget->dragged()) {
		if (instance->widget->isHidden()) {
			instance->widget->show();
		}

		auto dragged = instance->draggedAnimation.current(1.);
		auto position = QPoint();
		if (instance->hiddenByDrag) {
			instance->widget->setOpacity(instance->widget->countOpacityByParent());
			position = getHiddenPosition(instance->dragFrom, instance->widget->size(), instance->animationSide);
		} else {
			instance->widget->setOpacity(visible * visible);
			position = getPosition(instance);
			if (visible < 1.) {
				auto hiddenPosition = getHiddenPosition(position, instance->widget->size(), instance->animationSide);
				position.setX(anim::interpolate(hiddenPosition.x(), position.x(), visible));
				position.setY(anim::interpolate(hiddenPosition.y(), position.y(), visible));
			}
		}
		if (dragged < 1.) {
			position.setX(anim::interpolate(instance->dragFrom.x(), position.x(), dragged));
			position.setY(anim::interpolate(instance->dragFrom.y(), position.y(), dragged));
		}
		instance->widget->move(position);
	}
}

QPoint FloatController::getHiddenPosition(
		QPoint position,
		QSize size,
		RectPart side) const {
	switch (side) {
	case RectPart::Left: return { -size.width(), position.y() };
	case RectPart::Top: return { position.x(), -size.height() };
	case RectPart::Right: return { _parent->width(), position.y() };
	case RectPart::Bottom: return { position.x(), _parent->height() };
	}
	Unexpected("Bad side in MainWidget::getFloatPlayerHiddenPosition().");
}

QPoint FloatController::getPosition(not_null<Item*> instance) const {
	const auto section = _delegate->floatPlayerGetSection(instance->column);
	const auto rect = section->rectForFloatPlayer();
	auto position = rect.topLeft();
	if (IsBottomCorner(instance->corner)) {
		position.setY(position.y() + rect.height() - instance->widget->height());
	}
	if (IsRightCorner(instance->corner)) {
		position.setX(position.x() + rect.width() - instance->widget->width());
	}
	return _parent->mapFromGlobal(position);
}

RectPart FloatController::getSide(QPoint center) const {
	const auto left = std::abs(center.x());
	const auto right = std::abs(_parent->width() - center.x());
	const auto top = std::abs(center.y());
	const auto bottom = std::abs(_parent->height() - center.y());
	if (left < right && left < top && left < bottom) {
		return RectPart::Left;
	} else if (right < top && right < bottom) {
		return RectPart::Right;
	} else if (top < bottom) {
		return RectPart::Top;
	}
	return RectPart::Bottom;
}

void FloatController::remove(not_null<Item*> instance) {
	auto widget = std::move(instance->widget);
	auto i = ranges::find_if(_items, [&](auto &item) {
		return (item.get() == instance);
	});
	Assert(i != _items.end());
	_items.erase(i);

	// ~QWidget() can call HistoryInner::enterEvent() which can
	// lead to repaintHistoryItem() and we'll have an instance
	// in _items with destroyed widget. So we destroy the
	// instance first and only after that destroy the widget.
	widget.destroy();
}

void FloatController::updateColumnCorner(QPoint center) {
	Expects(!_items.empty());

	auto size = _items.back()->widget->size();
	auto min = INT_MAX;
	auto column = Auth().settings().floatPlayerColumn();
	auto corner = Auth().settings().floatPlayerCorner();
	auto checkSection = [&](
			not_null<Window::AbstractSectionWidget*> widget,
			Window::Column widgetColumn) {
		auto rect = _parent->mapFromGlobal(widget->rectForFloatPlayer());
		auto left = rect.x() + (size.width() / 2);
		auto right = rect.x() + rect.width() - (size.width() / 2);
		auto top = rect.y() + (size.height() / 2);
		auto bottom = rect.y() + rect.height() - (size.height() / 2);
		auto checkCorner = [&](QPoint point, RectPart checked) {
			auto distance = (point - center).manhattanLength();
			if (min > distance) {
				min = distance;
				column = widgetColumn;
				corner = checked;
			}
		};
		checkCorner({ left, top }, RectPart::TopLeft);
		checkCorner({ right, top }, RectPart::TopRight);
		checkCorner({ left, bottom }, RectPart::BottomLeft);
		checkCorner({ right, bottom }, RectPart::BottomRight);
	};

	_delegate->floatPlayerEnumerateSections(checkSection);

	if (Auth().settings().floatPlayerColumn() != column) {
		Auth().settings().setFloatPlayerColumn(column);
		Auth().saveSettingsDelayed();
	}
	if (Auth().settings().floatPlayerCorner() != corner) {
		Auth().settings().setFloatPlayerCorner(corner);
		Auth().saveSettingsDelayed();
	}
}

void FloatController::finishDrag(not_null<Item*> instance, bool closed) {
	instance->dragFrom = instance->widget->pos();
	const auto center = instance->widget->geometry().center();
	if (closed) {
		instance->hiddenByDrag = true;
		instance->animationSide = getSide(center);
	}
	updateColumnCorner(center);
	instance->column = Auth().settings().floatPlayerColumn();
	instance->corner = Auth().settings().floatPlayerCorner();

	instance->draggedAnimation.finish();
	instance->draggedAnimation.start(
		[=] { updatePosition(instance); },
		0.,
		1.,
		st::slideDuration,
		anim::sineInOut);
	updatePosition(instance);

	if (closed) {
		if (const auto item = instance->widget->item()) {
			_closeEvents.fire(item->fullId());
		}
		instance->widget->detach();
	}
}

} // namespace Player
} // namespace Media
