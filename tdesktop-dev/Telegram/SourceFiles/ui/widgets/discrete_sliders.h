/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/event_stream.h>
#include "ui/rp_widget.h"
#include "styles/style_widgets.h"

namespace Ui {

class RippleAnimation;

class DiscreteSlider : public RpWidget {
public:
	DiscreteSlider(QWidget *parent);

	void addSection(const QString &label);
	void setSections(const QStringList &labels);
	int activeSection() const {
		return _activeIndex;
	}
	void setActiveSection(int index);
	void setActiveSectionFast(int index);
	void finishAnimating();

	auto sectionActivated() const {
		return _sectionActivated.events();
	}

protected:
	void timerEvent(QTimerEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	int resizeGetHeight(int newWidth) override = 0;

	struct Section {
		Section(const QString &label, const style::font &font);

		int left, width;
		QString label;
		int labelWidth;
		std::unique_ptr<RippleAnimation> ripple;
	};

	int getCurrentActiveLeft(TimeMs ms);

	int getSectionsCount() const {
		return _sections.size();
	}

	template <typename Lambda>
	void enumerateSections(Lambda callback);

	template <typename Lambda>
	void enumerateSections(Lambda callback) const;

	virtual void startRipple(int sectionIndex) {
	}

	void stopAnimation() {
		_a_left.finish();
	}

	void setSelectOnPress(bool selectOnPress);

private:
	void activateCallback();
	virtual const style::font &getLabelFont() const = 0;
	virtual int getAnimationDuration() const = 0;

	int getIndexFromPosition(QPoint pos);
	void setSelectedSection(int index);

	std::vector<Section> _sections;
	int _activeIndex = 0;
	bool _selectOnPress = true;

	rpl::event_stream<int> _sectionActivated;

	int _pressed = -1;
	int _selected = 0;
	Animation _a_left;

	int _timerId = -1;
	TimeMs _callbackAfterMs = 0;

};

class SettingsSlider : public DiscreteSlider {
public:
	SettingsSlider(QWidget *parent, const style::SettingsSlider &st = st::defaultSettingsSlider);

	void setRippleTopRoundRadius(int radius);

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;

	void startRipple(int sectionIndex) override;

private:
	const style::font &getLabelFont() const override;
	int getAnimationDuration() const override;
	QImage prepareRippleMask(int sectionIndex, const Section &section);

	void resizeSections(int newWidth);
	std::vector<float64> countSectionsWidths(int newWidth) const;

	const style::SettingsSlider &_st;
	int _rippleTopRoundRadius = 0;


};

} // namespace Ui
