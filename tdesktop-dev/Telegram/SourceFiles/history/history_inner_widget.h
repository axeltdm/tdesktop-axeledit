/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/rp_widget.h"
#include "ui/widgets/tooltip.h"
#include "ui/widgets/scroll_area.h"
#include "history/view/history_view_top_bar_widget.h"

namespace Data {
struct Group;
} // namespace Data

namespace HistoryView {
class ElementDelegate;
struct TextState;
struct StateRequest;
enum class CursorState : char;
enum class PointState : char;
} // namespace HistoryView

namespace Window {
class Controller;
} // namespace Window

namespace Ui {
class PopupMenu;
} // namespace Ui

class HistoryWidget;
class HistoryInner
	: public Ui::RpWidget
	, public Ui::AbstractTooltipShower
	, private base::Subscriber {
	// The Q_OBJECT meta info is used for qobject_cast to HistoryInner!
	Q_OBJECT

public:
	using Element = HistoryView::Element;

	HistoryInner(
		not_null<HistoryWidget*> historyWidget,
		not_null<Window::Controller*> controller,
		Ui::ScrollArea *scroll,
		not_null<History*> history);

	void messagesReceived(PeerData *peer, const QVector<MTPMessage> &messages);
	void messagesReceivedDown(PeerData *peer, const QVector<MTPMessage> &messages);

	TextWithEntities getSelectedText() const;

	void touchScrollUpdated(const QPoint &screenPos);

	void recountHistoryGeometry();
	void updateSize();

	void repaintItem(const HistoryItem *item);
	void repaintItem(const Element *view);

	bool canCopySelected() const;
	bool canDeleteSelected() const;

	HistoryView::TopBarWidget::SelectedState getSelectionState() const;
	void clearSelected(bool onlyTextSelection = false);
	MessageIdsList getSelectedItems() const;
	void selectItem(not_null<HistoryItem*> item);
	bool inSelectionMode() const;

	void updateBotInfo(bool recount = true);

	bool wasSelectedText() const;
	void setFirstLoading(bool loading);

	// updates history->scrollTopItem/scrollTopOffset
	void visibleAreaUpdated(int top, int bottom);

	int historyHeight() const;
	int historyScrollTop() const;
	int migratedTop() const;
	int historyTop() const;
	int historyDrawTop() const;

	// -1 if should not be visible, -2 if bad history()
	int itemTop(const HistoryItem *item) const;
	int itemTop(const Element *view) const;

	void notifyIsBotChanged();
	void notifyMigrateUpdated();

	// When inline keyboard has moved because of the edition of its item we want
	// to move scroll position so that mouse points to the same button row.
	int moveScrollFollowingInlineKeyboard(const HistoryItem *item, int oldKeyboardTop, int newKeyboardTop);

	// Ui::AbstractTooltipShower interface.
	QString tooltipText() const override;
	QPoint tooltipPos() const override;

	// HistoryView::ElementDelegate interface.
	static not_null<HistoryView::ElementDelegate*> ElementDelegate();

	~HistoryInner();

protected:
	bool focusNextPrevChild(bool next) override;

	bool eventHook(QEvent *e) override; // calls touchEvent when necessary
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

public slots:
	void onParentGeometryChanged();

	void onTouchSelect();
	void onTouchScrollTimer();

private:
	class BotAbout;
	using SelectedItems = std::map<HistoryItem*, TextSelection, std::less<>>;
	enum class MouseAction {
		None,
		PrepareDrag,
		Dragging,
		PrepareSelect,
		Selecting,
	};
	enum class SelectAction {
		Select,
		Deselect,
		Invert,
	};
	enum class EnumItemsDirection {
		TopToBottom,
		BottomToTop,
	};
	using CursorState = HistoryView::CursorState;
	using PointState = HistoryView::PointState;
	using TextState = HistoryView::TextState;
	using StateRequest = HistoryView::StateRequest;

	// This function finds all history items that are displayed and calls template method
	// for each found message (in given direction) in the passed history with passed top offset.
	//
	// Method has "bool (*Method)(not_null<Element*> view, int itemtop, int itembottom)" signature
	// if it returns false the enumeration stops immidiately.
	template <bool TopToBottom, typename Method>
	void enumerateItemsInHistory(History *history, int historytop, Method method);

	template <EnumItemsDirection direction, typename Method>
	void enumerateItems(Method method) {
		constexpr auto TopToBottom = (direction == EnumItemsDirection::TopToBottom);
		if (TopToBottom && _migrated) {
			enumerateItemsInHistory<TopToBottom>(_migrated, migratedTop(), method);
		}
		enumerateItemsInHistory<TopToBottom>(_history, historyTop(), method);
		if (!TopToBottom && _migrated) {
			enumerateItemsInHistory<TopToBottom>(_migrated, migratedTop(), method);
		}
	}

	// This function finds all userpics on the left that are displayed and calls template method
	// for each found userpic (from the top to the bottom) using enumerateItems() method.
	//
	// Method has "bool (*Method)(not_null<Element*> view, int userpicTop)" signature
	// if it returns false the enumeration stops immidiately.
	template <typename Method>
	void enumerateUserpics(Method method);

	// This function finds all date elements that are displayed and calls template method
	// for each found date element (from the bottom to the top) using enumerateItems() method.
	//
	// Method has "bool (*Method)(not_null<Element*> view, int itemtop, int dateTop)" signature
	// if it returns false the enumeration stops immidiately.
	template <typename Method>
	void enumerateDates(Method method);

	void scrollDateCheck();
	void scrollDateHideByTimer();
	bool canHaveFromUserpics() const;
	void mouseActionStart(const QPoint &screenPos, Qt::MouseButton button);
	void mouseActionUpdate();
	void mouseActionUpdate(const QPoint &screenPos);
	void mouseActionFinish(const QPoint &screenPos, Qt::MouseButton button);
	void mouseActionCancel();
	std::unique_ptr<QMimeData> prepareDrag();
	void performDrag();

	QPoint mapPointToItem(QPoint p, const Element *view);
	QPoint mapPointToItem(QPoint p, const HistoryItem *item);

	void showContextMenu(QContextMenuEvent *e, bool showFromTouch = false);
	void cancelContextDownload(not_null<DocumentData*> document);
	void openContextGif(FullMsgId itemId);
	void saveContextGif(FullMsgId itemId);
	void copyContextText(FullMsgId itemId);
	void showContextInFolder(not_null<DocumentData*> document);
	void savePhotoToFile(not_null<PhotoData*> photo);
	void saveDocumentToFile(
		FullMsgId contextId,
		not_null<DocumentData*> document);
	void copyContextImage(not_null<PhotoData*> photo);
	void showStickerPackInfo(not_null<DocumentData*> document);

	void itemRemoved(not_null<const HistoryItem*> item);
	void viewRemoved(not_null<const Element*> view);
	void refreshView(not_null<HistoryItem*> item);

	void touchResetSpeed();
	void touchUpdateSpeed();
	void touchDeaccelerate(int32 elapsed);

	void adjustCurrent(int32 y) const;
	void adjustCurrent(int32 y, History *history) const;
	Element *prevItem(Element *item);
	Element *nextItem(Element *item);
	void updateDragSelection(Element *dragSelFrom, Element *dragSelTo, bool dragSelecting);
	TextSelection itemRenderSelection(
		not_null<Element*> view,
		int selfromy,
		int seltoy) const;
	TextSelection computeRenderSelection(
		not_null<const SelectedItems*> selected,
		not_null<Element*> view) const;

	void toggleScrollDateShown();
	void repaintScrollDateCallback();
	bool displayScrollDate() const;
	void scrollDateHide();
	void keepScrollDateForNow();

	void applyDragSelection();
	void applyDragSelection(not_null<SelectedItems*> toItems) const;
	void addSelectionRange(
		not_null<SelectedItems*> toItems,
		not_null<History*> history,
		int fromblock,
		int fromitem,
		int toblock,
		int toitem) const;
	bool isSelected(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item) const;
	bool isSelectedGroup(
		not_null<SelectedItems*> toItems,
		not_null<const Data::Group*> group) const;
	bool isSelectedAsGroup(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item) const;
	bool goodForSelection(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item,
		int &totalCount) const;
	void addToSelection(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item) const;
	void removeFromSelection(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item) const;
	void changeSelection(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item,
		SelectAction action) const;
	void changeSelectionAsGroup(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item,
		SelectAction action) const;
	void forwardItem(FullMsgId itemId);
	void forwardAsGroup(FullMsgId itemId);
	void deleteItem(not_null<HistoryItem*> item);
	void deleteItem(FullMsgId itemId);
	void deleteAsGroup(FullMsgId itemId);
	void reportItem(FullMsgId itemId);
	void reportAsGroup(FullMsgId itemId);
	void copySelectedText();

	// Does any of the shown histories has this flag set.
	bool hasPendingResizedItems() const;

	not_null<Window::Controller*> _controller;

	not_null<PeerData*> _peer;
	not_null<History*> _history;
	History *_migrated = nullptr;
	int _contentWidth = 0;
	int _historyPaddingTop = 0;

	// with migrated history we perhaps do not need to display first _history message
	// (if last _migrated message and first _history message are both isGroupMigrate)
	// or at least we don't need to display first _history date (just skip it by height)
	int _historySkipHeight = 0;

	std::unique_ptr<BotAbout> _botAbout;

	HistoryWidget *_widget = nullptr;
	Ui::ScrollArea *_scroll = nullptr;
	mutable History *_curHistory = nullptr;
	mutable int _curBlock = 0;
	mutable int _curItem = 0;

	bool _firstLoading = false;

	style::cursor _cursor = style::cur_default;
	SelectedItems _selected;

	MouseAction _mouseAction = MouseAction::None;
	TextSelectType _mouseSelectType = TextSelectType::Letters;
	QPoint _dragStartPosition;
	QPoint _mousePosition;
	HistoryItem *_mouseActionItem = nullptr;
	HistoryItem *_dragStateItem = nullptr;
	CursorState _mouseCursorState = CursorState();
	uint16 _mouseTextSymbol = 0;
	bool _pressWasInactive = false;

	QPoint _trippleClickPoint;
	QTimer _trippleClickTimer;

	Element *_dragSelFrom = nullptr;
	Element *_dragSelTo = nullptr;
	bool _dragSelecting = false;
	bool _wasSelectedText = false; // was some text selected in current drag action

	// scroll by touch support (at least Windows Surface tablets)
	bool _touchScroll = false;
	bool _touchSelect = false;
	bool _touchInProgress = false;
	QPoint _touchStart, _touchPrevPos, _touchPos;
	QTimer _touchSelectTimer;

	Ui::TouchScrollState _touchScrollState = Ui::TouchScrollState::Manual;
	bool _touchPrevPosValid = false;
	bool _touchWaitingAcceleration = false;
	QPoint _touchSpeed;
	TimeMs _touchSpeedTime = 0;
	TimeMs _touchAccelerationTime = 0;
	TimeMs _touchTime = 0;
	QTimer _touchScrollTimer;

	base::unique_qptr<Ui::PopupMenu> _menu;

	// save visible area coords for painting / pressing userpics
	int _visibleAreaTop = 0;
	int _visibleAreaBottom = 0;

	bool _scrollDateShown = false;
	Animation _scrollDateOpacity;
	SingleQueuedInvokation _scrollDateCheck;
	base::Timer _scrollDateHideTimer;
	Element *_scrollDateLastItem = nullptr;
	int _scrollDateLastItemTop = 0;
	ClickHandlerPtr _scrollDateLink;

};
