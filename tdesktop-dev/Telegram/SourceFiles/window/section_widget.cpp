/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/section_widget.h"

#include <rpl/range.h>
#include "application.h"
#include "mainwidget.h"
#include "window/section_memento.h"
#include "window/window_slide_animation.h"
#include "window/themes/window_theme.h"

namespace Window {

SectionWidget::SectionWidget(
	QWidget *parent,
	not_null<Window::Controller*> controller)
: AbstractSectionWidget(parent, controller) {
}

void SectionWidget::setGeometryWithTopMoved(
		const QRect &newGeometry,
		int topDelta) {
	_topDelta = topDelta;
	bool willBeResized = (size() != newGeometry.size());
	if (geometry() != newGeometry) {
		auto weak = make_weak(this);
		setGeometry(newGeometry);
		if (!weak) {
			return;
		}
	}
	if (!willBeResized) {
		resizeEvent(nullptr);
	}
	_topDelta = 0;
}

void SectionWidget::showAnimated(
		SlideDirection direction,
		const SectionSlideParams &params) {
	if (_showAnimation) return;

	showChildren();
	auto myContentCache = grabForShowAnimation(params);
	hideChildren();
	showAnimatedHook(params);

	_showAnimation = std::make_unique<SlideAnimation>();
	_showAnimation->setDirection(direction);
	_showAnimation->setRepaintCallback([this] { update(); });
	_showAnimation->setFinishedCallback([this] { showFinished(); });
	_showAnimation->setPixmaps(
		params.oldContentCache,
		myContentCache);
	_showAnimation->setTopBarShadow(params.withTopBarShadow);
	_showAnimation->setWithFade(params.withFade);
	_showAnimation->start();

	show();
}

std::unique_ptr<SectionMemento> SectionWidget::createMemento() {
	return nullptr;
}

void SectionWidget::showFast() {
	show();
	showFinished();
}

void SectionWidget::PaintBackground(QWidget *widget, QPaintEvent *event) {
	Painter p(widget);

	auto clip = event->rect();
	auto fill = QRect(0, 0, widget->width(), App::main()->height());
	auto fromy = App::main()->backgroundFromY();
	auto x = 0, y = 0;
	auto cached = App::main()->cachedBackground(fill, x, y);
	if (cached.isNull()) {
		if (Window::Theme::Background()->tile()) {
			auto &pix = Window::Theme::Background()->pixmapForTiled();
			auto left = clip.left();
			auto top = clip.top();
			auto right = clip.left() + clip.width();
			auto bottom = clip.top() + clip.height();
			auto w = pix.width() / cRetinaFactor();
			auto h = pix.height() / cRetinaFactor();
			auto sx = qFloor(left / w);
			auto sy = qFloor((top - fromy) / h);
			auto cx = qCeil(right / w);
			auto cy = qCeil((bottom - fromy) / h);
			for (auto i = sx; i < cx; ++i) {
				for (auto j = sy; j < cy; ++j) {
					p.drawPixmap(QPointF(i * w, fromy + j * h), pix);
				}
			}
		} else {
			PainterHighQualityEnabler hq(p);

			auto &pix = Window::Theme::Background()->pixmap();
			QRect to, from;
			Window::Theme::ComputeBackgroundRects(fill, pix.size(), to, from);
			to.moveTop(to.top() + fromy);
			p.drawPixmap(to, pix, from);
		}
	} else {
		p.drawPixmap(x, fromy + y, cached);
	}
}

void SectionWidget::paintEvent(QPaintEvent *e) {
	if (_showAnimation) {
		Painter p(this);
		_showAnimation->paintContents(p, e->rect());
	}
}

void SectionWidget::showFinished() {
	_showAnimation.reset();
	if (isHidden()) return;

	showChildren();
	showFinishedHook();

	setInnerFocus();
}

rpl::producer<int> SectionWidget::desiredHeight() const {
	return rpl::single(height());
}

SectionWidget::~SectionWidget() = default;

} // namespace Window
