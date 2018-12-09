/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/wrap/padding_wrap.h"
#include "boxes/abstract_box.h"
#include "styles/style_widgets.h"

namespace Ui {

class PopupMenu;

class CrossFadeAnimation {
public:
	CrossFadeAnimation(style::color bg);

	struct Part {
		QPixmap snapshot;
		QPoint position;
	};
	void addLine(Part was, Part now);

	void paintFrame(Painter &p, float64 dt) {
		auto progress = anim::linear(1., dt);
		paintFrame(p, progress, 1. - progress, progress);
	}

	void paintFrame(Painter &p, float64 positionReady, float64 alphaWas, float64 alphaNow);

private:
	struct Line {
		Line(Part was, Part now) : was(std::move(was)), now(std::move(now)) {
		}
		Part was;
		Part now;
	};
	void paintLine(Painter &p, const Line &line, float64 positionReady, float64 alphaWas, float64 alphaNow);

	style::color _bg;
	QList<Line> _lines;

};

class LabelSimple : public RpWidget {
public:
	LabelSimple(
		QWidget *parent,
		const style::LabelSimple &st = st::defaultLabelSimple,
		const QString &value = QString());

	// This method also resizes the label.
	void setText(const QString &newText, bool *outTextChanged = nullptr);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QString _fullText;
	int _fullTextWidth;

	QString _text;
	int _textWidth;

	const style::LabelSimple &_st;

};

class FlatLabel : public RpWidget, public ClickHandlerHost {
	Q_OBJECT

public:
	FlatLabel(QWidget *parent, const style::FlatLabel &st = st::defaultFlatLabel);

	enum class InitType {
		Simple,
		Rich,
	};
	FlatLabel(
		QWidget *parent,
		const QString &text,
		InitType initType,
		const style::FlatLabel &st = st::defaultFlatLabel);

	FlatLabel(
		QWidget *parent,
		rpl::producer<QString> &&text,
		const style::FlatLabel &st = st::defaultFlatLabel);
	FlatLabel(
		QWidget *parent,
		rpl::producer<TextWithEntities> &&text,
		const style::FlatLabel &st = st::defaultFlatLabel);

	void setOpacity(float64 o);

	void setText(const QString &text);
	void setRichText(const QString &text);
	void setMarkedText(const TextWithEntities &textWithEntities);
	void setSelectable(bool selectable);
	void setDoubleClickSelectsParagraph(bool doubleClickSelectsParagraph);
	void setContextCopyText(const QString &copyText);
	void setExpandLinksMode(ExpandLinksMode mode);
	void setBreakEverywhere(bool breakEverywhere);

	int naturalWidth() const override;
	QMargins getMargins() const override;

	void setLink(uint16 lnkIndex, const ClickHandlerPtr &lnk);

	using ClickHandlerFilter = Fn<bool(const ClickHandlerPtr&, Qt::MouseButton)>;
	void setClickHandlerFilter(ClickHandlerFilter &&filter);

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &action, bool pressed) override;

	static std::unique_ptr<CrossFadeAnimation> CrossFade(
		not_null<FlatLabel*> from,
		not_null<FlatLabel*> to,
		style::color bg,
		QPoint fromPosition = QPoint(),
		QPoint toPosition = QPoint());

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	bool eventHook(QEvent *e) override; // calls touchEvent when necessary
	void touchEvent(QTouchEvent *e);

	int resizeGetHeight(int newWidth) override;

private slots:
	void onCopySelectedText();
	void onCopyContextText();

	void onTouchSelect();
	void onContextMenuDestroy(QObject *obj);

	void onExecuteDrag();

private:
	void init();
	void textUpdated();

	Text::StateResult dragActionUpdate();
	Text::StateResult dragActionStart(const QPoint &p, Qt::MouseButton button);
	Text::StateResult dragActionFinish(const QPoint &p, Qt::MouseButton button);
	void updateHover(const Text::StateResult &state);
	Text::StateResult getTextState(const QPoint &m) const;
	void refreshCursor(bool uponSymbol);

	int countTextWidth() const;
	int countTextHeight(int textWidth);
	void refreshSize();

	enum class ContextMenuReason {
		FromEvent,
		FromTouch,
	};
	void showContextMenu(QContextMenuEvent *e, ContextMenuReason reason);

	Text _text;
	const style::FlatLabel &_st;
	float64 _opacity = 1.;

	int _allowedWidth = 0;
	int _fullTextHeight = 0;
	bool _breakEverywhere = false;

	style::cursor _cursor = style::cur_default;
	bool _selectable = false;
	TextSelection _selection, _savedSelection;
	TextSelectType _selectionType = TextSelectType::Letters;
	bool _doubleClickSelectsParagraph = false;

	enum DragAction {
		NoDrag = 0x00,
		PrepareDrag = 0x01,
		Dragging = 0x02,
		Selecting = 0x04,
	};
	DragAction _dragAction = NoDrag;
	QPoint _dragStartPosition;
	uint16 _dragSymbol = 0;
	bool _dragWasInactive = false;

	QPoint _lastMousePos;

	QPoint _trippleClickPoint;
	QTimer _trippleClickTimer;

	Ui::PopupMenu *_contextMenu = nullptr;
	QString _contextCopyText;
	ExpandLinksMode _contextExpandLinksMode = ExpandLinksAll;

	ClickHandlerFilter _clickHandlerFilter;

	// text selection and context menu by touch support (at least Windows Surface tablets)
	bool _touchSelect = false;
	bool _touchInProgress = false;
	QPoint _touchStart, _touchPrevPos, _touchPos;
	QTimer _touchSelectTimer;

};

class DividerLabel : public PaddingWrap<Ui::FlatLabel> {
public:
	using PaddingWrap::PaddingWrap;

	int naturalWidth() const override;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	object_ptr<BoxContentDivider> _background
		= object_ptr<BoxContentDivider>(this);

};

} // namespace Ui
