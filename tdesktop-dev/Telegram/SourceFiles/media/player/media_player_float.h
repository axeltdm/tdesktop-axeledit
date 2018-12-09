/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Window {
class Controller;
class AbstractSectionWidget;
enum class Column;
} // namespace Window

namespace Media {
namespace Clip {
class Playback;
} // namespace Clip

namespace Player {

class Float : public Ui::RpWidget, private base::Subscriber {
public:
	Float(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		not_null<HistoryItem*> item,
		Fn<void(bool visible)> toggleCallback,
		Fn<void(bool closed)> draggedCallback);

	HistoryItem *item() const {
		return _item;
	}
	void setOpacity(float64 opacity) {
		if (_opacity != opacity) {
			_opacity = opacity;
			update();
		}
	}
	float64 countOpacityByParent() const {
		return outRatio();
	}
	bool isReady() const {
		return (getReader() != nullptr);
	}
	void detach();
	bool detached() const {
		return !_item;
	}
	bool dragged() const {
		return _drag;
	}
	void resetMouseState() {
		_down = false;
		if (_drag) {
			finishDrag(false);
		}
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
	float64 outRatio() const;
	Clip::Reader *getReader() const;
	Clip::Playback *getPlayback() const;
	void repaintItem();
	void prepareShadow();
	bool hasFrame() const;
	bool fillFrame();
	QRect getInnerRect() const;
	void finishDrag(bool closed);

	not_null<Window::Controller*> _controller;
	HistoryItem *_item = nullptr;
	Fn<void(bool visible)> _toggleCallback;

	float64 _opacity = 1.;

	QPixmap _shadow;
	QImage _frame;
	bool _down = false;
	QPoint _downPoint;

	bool _drag = false;
	QPoint _dragLocalPoint;
	Fn<void(bool closed)> _draggedCallback;

};

class FloatDelegate {
public:
	virtual not_null<Ui::RpWidget*> floatPlayerWidget() = 0;
	virtual not_null<Window::Controller*> floatPlayerController() = 0;
	virtual not_null<Window::AbstractSectionWidget*> floatPlayerGetSection(
		Window::Column column) = 0;
	virtual void floatPlayerEnumerateSections(Fn<void(
		not_null<Window::AbstractSectionWidget*> widget,
		Window::Column widgetColumn)> callback) = 0;
	virtual bool floatPlayerIsVisible(not_null<HistoryItem*> item) = 0;

	virtual rpl::producer<> floatPlayerCheckVisibilityRequests() {
		return _checkVisibility.events();
	}
	virtual rpl::producer<> floatPlayerHideAllRequests() {
		return _hideAll.events();
	}
	virtual rpl::producer<> floatPlayerShowVisibleRequests() {
		return _showVisible.events();
	}
	virtual rpl::producer<> floatPlayerRaiseAllRequests() {
		return _raiseAll.events();
	}
	virtual rpl::producer<> floatPlayerUpdatePositionsRequests() {
		return _updatePositions.events();;
	}

	struct FloatPlayerFilterWheelEventRequest {
		not_null<QObject*> object;
		not_null<QEvent*> event;
		not_null<std::optional<bool>*> result;
	};
	virtual auto floatPlayerFilterWheelEventRequests()
	-> rpl::producer<FloatPlayerFilterWheelEventRequest> {
		return _filterWheelEvent.events();
	}

protected:
	void floatPlayerCheckVisibility() {
		_checkVisibility.fire({});
	}
	void floatPlayerHideAll() {
		_hideAll.fire({});
	}
	void floatPlayerShowVisible() {
		_showVisible.fire({});
	}
	void floatPlayerRaiseAll() {
		_raiseAll.fire({});
	}
	void floatPlayerUpdatePositions() {
		_updatePositions.fire({});
	}
	std::optional<bool> floatPlayerFilterWheelEvent(
			not_null<QObject*> object,
			not_null<QEvent*> event) {
		auto result = std::optional<bool>();
		_filterWheelEvent.fire({ object, event, &result });
		return result;
	}

private:
	rpl::event_stream<> _checkVisibility;
	rpl::event_stream<> _hideAll;
	rpl::event_stream<> _showVisible;
	rpl::event_stream<> _raiseAll;
	rpl::event_stream<> _updatePositions;
	rpl::event_stream<FloatPlayerFilterWheelEventRequest> _filterWheelEvent;

};

class FloatController : private base::Subscriber {
public:
	explicit FloatController(not_null<FloatDelegate*> delegate);

	void replaceDelegate(not_null<FloatDelegate*> delegate);
	rpl::producer<FullMsgId> closeEvents() const {
		return _closeEvents.events();
	}

private:
	struct Item {
		template <typename ToggleCallback, typename DraggedCallback>
		Item(
			not_null<QWidget*> parent,
			not_null<Window::Controller*> controller,
			not_null<HistoryItem*> item,
			ToggleCallback toggle,
			DraggedCallback dragged);

		bool hiddenByWidget = false;
		bool hiddenByHistory = false;
		bool visible = false;
		RectPart animationSide;
		Animation visibleAnimation;
		Window::Column column;
		RectPart corner;
		QPoint dragFrom;
		Animation draggedAnimation;
		bool hiddenByDrag = false;
		object_ptr<Float> widget;
	};

	void checkCurrent();
	void create(not_null<HistoryItem*> item);
	void toggle(not_null<Item*> instance);
	void updatePosition(not_null<Item*> instance);
	void remove(not_null<Item*> instance);
	Item *current() const {
		return _items.empty() ? nullptr : _items.back().get();
	}
	void finishDrag(
		not_null<Item*> instance,
		bool closed);
	void updateColumnCorner(QPoint center);
	QPoint getPosition(not_null<Item*> instance) const;
	QPoint getHiddenPosition(
		QPoint position,
		QSize size,
		RectPart side) const;
	RectPart getSide(QPoint center) const;

	void startDelegateHandling();
	void checkVisibility();
	void hideAll();
	void showVisible();
	void raiseAll();
	void updatePositions();
	std::optional<bool> filterWheelEvent(
		not_null<QObject*> object,
		not_null<QEvent*> event);

	not_null<FloatDelegate*> _delegate;
	not_null<Ui::RpWidget*> _parent;
	not_null<Window::Controller*> _controller;
	std::vector<std::unique_ptr<Item>> _items;

	rpl::event_stream<FullMsgId> _closeEvents;
	rpl::lifetime _delegateLifetime;

};

} // namespace Player
} // namespace Media
