/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/round_checkbox.h"

namespace Ui {
namespace {

static constexpr int kWideScale = 3;

class CheckCaches : public QObject {
public:
	CheckCaches(QObject *parent) : QObject(parent) {
		Expects(parent != nullptr);
	}

	void clear();

	QPixmap frame(
		const style::RoundCheckbox *st,
		bool displayInactive,
		float64 progress);

private:
	struct Frames {
		bool displayInactive = false;
		std::vector<QPixmap> list;
		QPixmap outerWide;
		QPixmap inner;
		QPixmap check;
	};

	int countFramesCount(const style::RoundCheckbox *st);
	Frames &framesForStyle(
		const style::RoundCheckbox *st,
		bool displayInactive);
	void prepareFramesData(
		const style::RoundCheckbox *st,
		bool displayInactive,
		Frames &frames);
	QPixmap paintFrame(
		const style::RoundCheckbox *st,
		const Frames &frames,
		float64 progress);

	std::map<const style::RoundCheckbox *, Frames> _data;

};

QPixmap PrepareOuterWide(const style::RoundCheckbox *st) {
	const auto size = st->size;
	const auto wideSize = size * kWideScale;
	auto result = QImage(
		QSize(wideSize, wideSize) * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		PainterHighQualityEnabler hq(p);

		p.setPen(Qt::NoPen);
		p.setBrush(st->border);
		const auto half = st->width / 2.;
		p.drawEllipse(QRectF(
			(wideSize - size) / 2 - half,
			(wideSize - size) / 2 - half,
			size + 2. * half,
			size + 2. * half));
	}
	return App::pixmapFromImageInPlace(std::move(result));
}

QPixmap PrepareInner(const style::RoundCheckbox *st, bool displayInactive) {
	const auto size = st->size;
	auto result = QImage(
		QSize(size, size) * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		PainterHighQualityEnabler hq(p);

		p.setPen(Qt::NoPen);
		p.setBrush(st->bgActive);
		const auto half = st->width / 2.;
		p.drawEllipse(QRectF(
			displayInactive ? 0. : half,
			displayInactive ? 0. : half,
			size - (displayInactive ? 0. : 2. * half),
			size - (displayInactive ? 0. : 2. * half)));
	}
	return App::pixmapFromImageInPlace(std::move(result));
}

QPixmap PrepareCheck(const style::RoundCheckbox *st) {
	const auto size = st->size;
	auto result = QImage(
		QSize(size, size) * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		st->check.paint(p, 0, 0, size);
	}
	return App::pixmapFromImageInPlace(std::move(result));
}

QRect WideDestRect(
		const style::RoundCheckbox *st,
		int x,
		int y,
		float64 scale) {
	auto iconSizeFull = kWideScale * st->size;
	auto iconSize = qRound(iconSizeFull * scale);
	if (iconSize % 2 != iconSizeFull % 2) {
		++iconSize;
	}
	auto iconShift = (iconSizeFull - iconSize) / 2;
	auto iconLeft = x - (kWideScale - 1) * st->size / 2 + iconShift;
	auto iconTop = y - (kWideScale - 1) * st->size / 2 + iconShift;
	return QRect(iconLeft, iconTop, iconSize, iconSize);
}

void CheckCaches::clear() {
	_data.clear();
}

int CheckCaches::countFramesCount(const style::RoundCheckbox *st) {
	return (st->duration / AnimationTimerDelta) + 1;
}

CheckCaches::Frames &CheckCaches::framesForStyle(
		const style::RoundCheckbox *st,
		bool displayInactive) {
	auto i = _data.find(st);
	if (i == _data.end()) {
		i = _data.emplace(st, Frames()).first;
		prepareFramesData(st, displayInactive, i->second);
	} else if (i->second.displayInactive != displayInactive) {
		i->second = Frames();
		prepareFramesData(st, displayInactive, i->second);
	}
	return i->second;
}

void CheckCaches::prepareFramesData(
		const style::RoundCheckbox *st,
		bool displayInactive,
		Frames &frames) {
	frames.list.resize(countFramesCount(st));
	frames.displayInactive = displayInactive;

	if (!frames.displayInactive) {
		frames.outerWide = PrepareOuterWide(st);
	}
	frames.inner = PrepareInner(st, frames.displayInactive);
	frames.check = PrepareCheck(st);
}

QPixmap CheckCaches::frame(
		const style::RoundCheckbox *st,
		bool displayInactive,
		float64 progress) {
	auto &frames = framesForStyle(st, displayInactive);

	const auto frameCount = int(frames.list.size());
	const auto frameIndex = int(std::round(progress * (frameCount - 1)));
	Assert(frameIndex >= 0 && frameIndex < frameCount);

	if (!frames.list[frameIndex]) {
		const auto frameProgress = frameIndex / float64(frameCount - 1);
		frames.list[frameIndex] = paintFrame(st, frames, frameProgress);
	}
	return frames.list[frameIndex];
}

QPixmap CheckCaches::paintFrame(
		const style::RoundCheckbox *st,
		const Frames &frames,
		float64 progress) {
	const auto size = st->size;
	const auto wideSize = size * kWideScale;
	const auto skip = (wideSize - size) / 2;
	auto result = QImage(wideSize * cIntRetinaFactor(), wideSize * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);

	const auto roundProgress = (progress >= st->bgDuration)
		? 1.
		: (progress / st->bgDuration);
	const auto checkProgress = (1. - progress >= st->fgDuration)
		? 0.
		: (1. - (1. - progress) / st->fgDuration);
	{
		Painter p(&result);
		PainterHighQualityEnabler hq(p);

		if (!frames.displayInactive) {
			const auto outerMaxScale = (size - st->width) / float64(size);
			const auto outerScale = roundProgress
				+ (1. - roundProgress) * outerMaxScale;
			const auto outerTo = WideDestRect(st, skip, skip, outerScale);
			const auto outerFrom = QRect(
				QPoint(0, 0),
				QSize(wideSize, wideSize) * cIntRetinaFactor());
			p.drawPixmap(outerTo, frames.outerWide, outerFrom);
		}
		p.drawPixmap(skip, skip, frames.inner);

		const auto divider = checkProgress * st->size;
		const auto checkTo = QRect(skip, skip, divider, st->size);
		const auto checkFrom = QRect(
			QPoint(0, 0),
			QSize(divider, st->size) * cIntRetinaFactor());
		p.drawPixmap(checkTo, frames.check, checkFrom);

		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::transparent);
		const auto remove = size * (1. - roundProgress);
		p.drawEllipse(QRectF(
			(wideSize - remove) / 2.,
			(wideSize - remove) / 2.,
			remove,
			remove));
	}
	return App::pixmapFromImageInPlace(std::move(result));
}

CheckCaches *FrameCaches() {
	static QPointer<CheckCaches> Instance;

	if (auto instance = Instance.data()) {
		return instance;
	}
	auto result = new CheckCaches(QGuiApplication::instance());
	Instance = result;
	return result;
}

void prepareCheckCaches(const style::RoundCheckbox *st, bool displayInactive, QPixmap &checkBgCache, QPixmap &checkFullCache) {
	auto size = st->size;
	auto wideSize = size * kWideScale;
	auto cache = QImage(wideSize * cIntRetinaFactor(), wideSize * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(cRetinaFactor());
	cache.fill(Qt::transparent);
	{
		Painter p(&cache);
		PainterHighQualityEnabler hq(p);

		if (displayInactive) {
			p.setPen(Qt::NoPen);
		} else {
			auto pen = st->border->p;
			pen.setWidth(st->width);
			p.setPen(pen);
		}
		p.setBrush(st->bgActive);
		auto ellipse = QRect((wideSize - size) / 2, (wideSize - size) / 2, size, size);
		p.drawEllipse(ellipse);
	}
	auto cacheIcon = cache;
	{
		Painter p(&cacheIcon);
		auto ellipse = QRect((wideSize - size) / 2, (wideSize - size) / 2, size, size);
		st->check.paint(p, ellipse.topLeft(), wideSize);
	}
	checkBgCache = App::pixmapFromImageInPlace(std::move(cache));
	checkFullCache = App::pixmapFromImageInPlace(std::move(cacheIcon));
}

} // namespace

RoundCheckbox::RoundCheckbox(const style::RoundCheckbox &st, Fn<void()> updateCallback)
: _st(st)
, _updateCallback(updateCallback) {
}

void RoundCheckbox::paint(Painter &p, TimeMs ms, int x, int y, int outerWidth, float64 masterScale) {
	if (!_checkedProgress.animating() && !_checked && !_displayInactive) {
		return;
	}

	auto cacheSize = kWideScale * _st.size * cIntRetinaFactor();
	auto cacheFrom = QRect(0, 0, cacheSize, cacheSize);
	auto displayInactive = !_inactiveCacheBg.isNull();
	auto inactiveTo = WideDestRect(&_st, x, y, masterScale);

	PainterHighQualityEnabler hq(p);
	if (!_inactiveCacheBg.isNull()) {
		p.drawPixmap(inactiveTo, _inactiveCacheBg, cacheFrom);
	}

	const auto progress = _checkedProgress.current(ms, _checked ? 1. : 0.);
	if (progress > 0.) {
		auto frame = FrameCaches()->frame(&_st, _displayInactive, progress);
		p.drawPixmap(inactiveTo, frame, cacheFrom);
	}

	if (!_inactiveCacheFg.isNull()) {
		p.drawPixmap(inactiveTo, _inactiveCacheFg, cacheFrom);
	}
}

void RoundCheckbox::setChecked(bool newChecked, SetStyle speed) {
	if (_checked == newChecked) {
		if (speed != SetStyle::Animated) {
			_checkedProgress.finish();
		}
		return;
	}
	_checked = newChecked;
	_checkedProgress.start(
		_updateCallback,
		_checked ? 0. : 1.,
		_checked ? 1. : 0.,
		_st.duration,
		anim::linear);
}

void RoundCheckbox::invalidateCache() {
	FrameCaches()->clear();
	if (!_inactiveCacheBg.isNull() || !_inactiveCacheFg.isNull()) {
		prepareInactiveCache();
	}
}

void RoundCheckbox::setDisplayInactive(bool displayInactive) {
	if (_displayInactive != displayInactive) {
		_displayInactive = displayInactive;
		if (_displayInactive) {
			prepareInactiveCache();
		} else {
			_inactiveCacheBg = _inactiveCacheFg = QPixmap();
		}
	}
}

void RoundCheckbox::prepareInactiveCache() {
	auto wideSize = _st.size * kWideScale;
	auto ellipse = QRect((wideSize - _st.size) / 2, (wideSize - _st.size) / 2, _st.size, _st.size);

	auto cacheBg = QImage(wideSize * cIntRetinaFactor(), wideSize * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	cacheBg.setDevicePixelRatio(cRetinaFactor());
	cacheBg.fill(Qt::transparent);
	auto cacheFg = cacheBg;
	if (_st.bgInactive) {
		Painter p(&cacheBg);
		PainterHighQualityEnabler hq(p);

		p.setPen(Qt::NoPen);
		p.setBrush(_st.bgInactive);
		p.drawEllipse(ellipse);
	}
	_inactiveCacheBg = App::pixmapFromImageInPlace(std::move(cacheBg));

	{
		Painter p(&cacheFg);
		PainterHighQualityEnabler hq(p);

		auto pen = _st.border->p;
		pen.setWidth(_st.width);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(ellipse);
	}
	_inactiveCacheFg = App::pixmapFromImageInPlace(std::move(cacheFg));
}

RoundImageCheckbox::RoundImageCheckbox(const style::RoundImageCheckbox &st, Fn<void()> updateCallback, PaintRoundImage &&paintRoundImage)
: _st(st)
, _updateCallback(updateCallback)
, _paintRoundImage(std::move(paintRoundImage))
, _check(_st.check, _updateCallback) {
}

void RoundImageCheckbox::paint(Painter &p, TimeMs ms, int x, int y, int outerWidth) {
	_selection.step(ms);

	auto selectionLevel = _selection.current(checked() ? 1. : 0.);
	if (_selection.animating()) {
		auto userpicRadius = qRound(kWideScale * (_st.imageRadius + (_st.imageSmallRadius - _st.imageRadius) * selectionLevel));
		auto userpicShift = kWideScale * _st.imageRadius - userpicRadius;
		auto userpicLeft = x - (kWideScale - 1) * _st.imageRadius + userpicShift;
		auto userpicTop = y - (kWideScale - 1) * _st.imageRadius + userpicShift;
		auto to = QRect(userpicLeft, userpicTop, userpicRadius * 2, userpicRadius * 2);
		auto from = QRect(QPoint(0, 0), _wideCache.size());

		PainterHighQualityEnabler hq(p);
		p.drawPixmapLeft(to, outerWidth, _wideCache, from);
	} else {
		if (!_wideCache.isNull()) {
			_wideCache = QPixmap();
		}
		auto userpicRadius = checked() ? _st.imageSmallRadius : _st.imageRadius;
		auto userpicShift = _st.imageRadius - userpicRadius;
		auto userpicLeft = x + userpicShift;
		auto userpicTop = y + userpicShift;
		_paintRoundImage(p, userpicLeft, userpicTop, outerWidth, userpicRadius * 2);
	}

	if (selectionLevel > 0) {
		PainterHighQualityEnabler hq(p);
		p.setOpacity(snap(selectionLevel, 0., 1.));
		p.setBrush(Qt::NoBrush);
		auto pen = _st.selectFg->p;
		pen.setWidth(_st.selectWidth);
		p.setPen(pen);
		p.drawEllipse(rtlrect(x, y, _st.imageRadius * 2, _st.imageRadius * 2, outerWidth));
		p.setOpacity(1.);
	}

	auto iconLeft = x + 2 * _st.imageRadius + _st.selectWidth - _st.check.size;
	auto iconTop = y + 2 * _st.imageRadius + _st.selectWidth - _st.check.size;
	_check.paint(p, ms, iconLeft, iconTop, outerWidth);
}

float64 RoundImageCheckbox::checkedAnimationRatio() const {
	return snap(_selection.current(checked() ? 1. : 0.), 0., 1.);
}

void RoundImageCheckbox::setChecked(bool newChecked, SetStyle speed) {
	auto changed = (checked() != newChecked);
	_check.setChecked(newChecked, speed);
	if (!changed) {
		if (speed != SetStyle::Animated) {
			_selection.finish();
		}
		return;
	}
	if (speed == SetStyle::Animated) {
		prepareWideCache();
		_selection.start(_updateCallback, checked() ? 0 : 1, checked() ? 1 : 0, _st.selectDuration, anim::bumpy(1.25));
	} else {
		_selection.finish();
	}
}

void RoundImageCheckbox::prepareWideCache() {
	if (_wideCache.isNull()) {
		auto size = _st.imageRadius * 2;
		auto wideSize = size * kWideScale;
		QImage cache(wideSize * cIntRetinaFactor(), wideSize * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(cRetinaFactor());
		{
			Painter p(&cache);
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.fillRect(0, 0, wideSize, wideSize, Qt::transparent);
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			_paintRoundImage(p, (wideSize - size) / 2, (wideSize - size) / 2, wideSize, size);
		}
		_wideCache = App::pixmapFromImageInPlace(std::move(cache));
	}
}

} // namespace Ui
