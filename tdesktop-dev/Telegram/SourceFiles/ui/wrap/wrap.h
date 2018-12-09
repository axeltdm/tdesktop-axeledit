/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {

template <typename Widget, typename ParentType = RpWidget>
class Wrap;

namespace details {

struct UnwrapHelper {
	template <
		typename Widget,
		typename = typename std::decay_t<Widget>::WrapParentType>
	static std::true_type Is(Widget &&widget);
	static std::false_type Is(...);

	template <typename Entity>
	static auto Unwrap(Entity *entity, std::true_type) {
		return entity
			? entity->entity()
			: nullptr;
	}
	template <typename Entity>
	static Entity *Unwrap(Entity *entity, std::false_type) {
		return entity;
	}
	template <typename Entity>
	static auto Unwrap(Entity *entity) {
		using Selector = decltype(Is(std::declval<Entity>()));
		return Unwrap(
			entity,
			Selector());
	}

};

} // namespace details

template <typename Widget>
class Wrap<Widget, RpWidget> : public RpWidget {
public:
	Wrap(QWidget *parent, object_ptr<Widget> &&child);

	Widget *wrapped() {
		return _wrapped;
	}
	const Widget *wrapped() const {
		return _wrapped;
	}
	auto entity() {
		return details::UnwrapHelper::Unwrap(wrapped());
	}
	auto entity() const {
		return details::UnwrapHelper::Unwrap(wrapped());
	}

	QMargins getMargins() const override {
		if (auto weak = wrapped()) {
			return weak->getMargins();
		}
		return RpWidget::getMargins();
	}
	int naturalWidth() const override {
		if (auto weak = wrapped()) {
			return weak->naturalWidth();
		}
		return RpWidget::naturalWidth();
	}

protected:
	int resizeGetHeight(int newWidth) override {
		if (auto weak = wrapped()) {
			weak->resizeToWidth(newWidth);
			return weak->heightNoMargins();
		}
		return heightNoMargins();
	}
	void visibleTopBottomUpdated(
			int visibleTop,
			int visibleBottom) override {
		setChildVisibleTopBottom(
			wrapped(),
			visibleTop,
			visibleBottom);
	}
	virtual void wrappedSizeUpdated(QSize size) {
		resize(size);
	}

private:
	object_ptr<Widget> _wrapped;

};

template <typename Widget>
Wrap<Widget, RpWidget>::Wrap(
	QWidget *parent,
	object_ptr<Widget> &&child)
: RpWidget(parent)
, _wrapped(std::move(child)) {
	if (_wrapped) {
		_wrapped->sizeValue(
		) | rpl::start_with_next([this](const QSize &value) {
			wrappedSizeUpdated(value);
		}, lifetime());
		AttachParentChild(this, _wrapped);
		_wrapped->move(0, 0);
		_wrapped->alive(
		) | rpl::start_with_done([this] {
			_wrapped->setParent(nullptr);
			_wrapped = nullptr;
			delete this;
		}, lifetime());
	}
}

template <typename Widget, typename ParentType>
class Wrap : public ParentType {
public:
	using ParentType::ParentType;

	Widget *wrapped() {
		return static_cast<Widget*>(ParentType::wrapped());
	}
	const Widget *wrapped() const {
		return static_cast<const Widget*>(ParentType::wrapped());
	}
	auto entity() {
		return details::UnwrapHelper::Unwrap(wrapped());
	}
	auto entity() const {
		return details::UnwrapHelper::Unwrap(wrapped());
	}

	using WrapParentType = ParentType;

};

class OverrideMargins : public Wrap<RpWidget> {
	using Parent = Wrap<RpWidget>;

public:
	OverrideMargins(
		QWidget *parent,
		object_ptr<RpWidget> &&child,
		QMargins margins = QMargins())
	: Parent(parent, std::move(child)), _margins(margins) {
		if (auto weak = wrapped()) {
			auto margins = weak->getMargins();
			resizeToWidth(weak->width()
				- margins.left()
				- margins.right());
		}
	}

	QMargins getMargins() const override {
		return _margins;
	}

protected:
	int resizeGetHeight(int newWidth) override {
		if (auto weak = wrapped()) {
			weak->resizeToWidth(newWidth);
			weak->moveToLeft(_margins.left(), _margins.top());
			return weak->heightNoMargins();
		}
		return height();
	}

private:
	void wrappedSizeUpdated(QSize size) override {
		auto margins = wrapped()->getMargins();
		resize(
			(size.width()
				- margins.left()
				- margins.right()
				+ _margins.left()
				+ _margins.right()),
			(size.height()
				- margins.top()
				- margins.bottom()
				+ _margins.top()
				+ _margins.bottom()));
	}

	QMargins _margins;

};

} // namespace Ui
