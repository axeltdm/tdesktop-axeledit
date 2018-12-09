/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_element.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/admin_log/history_admin_log_section.h"
#include "ui/widgets/tooltip.h"
#include "ui/rp_widget.h"
#include "mtproto/sender.h"
#include "base/timer.h"

namespace HistoryView {
class Element;
struct TextState;
struct StateRequest;
enum class CursorState : char;
enum class PointState : char;
} // namespace HistoryView

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

namespace AdminLog {

class SectionMemento;

class InnerWidget final
	: public Ui::RpWidget
	, public Ui::AbstractTooltipShower
	, public HistoryView::ElementDelegate
	, private MTP::Sender
	, private base::Subscriber {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		not_null<ChannelData*> channel);

	base::Observable<void> showSearchSignal;
	base::Observable<int> scrollToSignal;
	base::Observable<void> cancelledSignal;

	not_null<ChannelData*> channel() const {
		return _channel;
	}

	// Set the correct scroll position after being resized.
	void restoreScrollPosition();

	void resizeToWidth(int newWidth, int minHeight) {
		_minHeight = minHeight;
		return TWidget::resizeToWidth(newWidth);
	}

	void saveState(not_null<SectionMemento*> memento);
	void restoreState(not_null<SectionMemento*> memento);

	// Empty "flags" means all events.
	void applyFilter(FilterValue &&value);
	void applySearch(const QString &query);
	void showFilter(Fn<void(FilterValue &&filter)> callback);

	// Ui::AbstractTooltipShower interface.
	QString tooltipText() const override;
	QPoint tooltipPos() const override;

	// HistoryView::ElementDelegate interface.
	HistoryView::Context elementContext() override;
	std::unique_ptr<HistoryView::Element> elementCreate(
		not_null<HistoryMessage*> message) override;
	std::unique_ptr<HistoryView::Element> elementCreate(
		not_null<HistoryService*> message) override;
	bool elementUnderCursor(
		not_null<const HistoryView::Element*> view) override;
	void elementAnimationAutoplayAsync(
		not_null<const HistoryView::Element*> view) override;
	TimeMs elementHighlightTime(
		not_null<const HistoryView::Element*> element) override;
	bool elementInSelectionMode() override;

	~InnerWidget();

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

private:
	using Element = HistoryView::Element;
	enum class Direction {
		Up,
		Down,
	};
	enum class MouseAction {
		None,
		PrepareDrag,
		Dragging,
		Selecting,
	};
	enum class EnumItemsDirection {
		TopToBottom,
		BottomToTop,
	};
	using TextState = HistoryView::TextState;
	using CursorState = HistoryView::CursorState;
	using PointState = HistoryView::PointState;
	using StateRequest = HistoryView::StateRequest;

	void mouseActionStart(const QPoint &screenPos, Qt::MouseButton button);
	void mouseActionUpdate(const QPoint &screenPos);
	void mouseActionFinish(const QPoint &screenPos, Qt::MouseButton button);
	void mouseActionCancel();
	void updateSelected();
	void performDrag();
	int itemTop(not_null<const Element*> view) const;
	void repaintItem(const Element *view);
	void refreshItem(not_null<const Element*> view);
	void resizeItem(not_null<Element*> view);
	QPoint mapPointToItem(QPoint point, const Element *view) const;

	void showContextMenu(QContextMenuEvent *e, bool showFromTouch = false);
	void savePhotoToFile(PhotoData *photo);
	void saveDocumentToFile(DocumentData *document);
	void copyContextImage(PhotoData *photo);
	void showStickerPackInfo(not_null<DocumentData*> document);
	void cancelContextDownload(not_null<DocumentData*> document);
	void showContextInFolder(not_null<DocumentData*> document);
	void openContextGif(FullMsgId itemId);
	void copyContextText(FullMsgId itemId);
	void copySelectedText();
	TextWithEntities getSelectedText() const;
	void suggestRestrictUser(not_null<UserData*> user);
	void restrictUser(not_null<UserData*> user, const MTPChannelBannedRights &oldRights, const MTPChannelBannedRights &newRights);
	void restrictUserDone(not_null<UserData*> user, const MTPChannelBannedRights &rights);

	void requestAdmins();
	void checkPreloadMore();
	void updateVisibleTopItem();
	void preloadMore(Direction direction);
	void itemsAdded(Direction direction, int addedCount);
	void updateSize();
	void updateMinMaxIds();
	void updateEmptyText();
	void paintEmpty(Painter &p);
	void clearAfterFilterChange();
	void clearAndRequestLog();
	void addEvents(Direction direction, const QVector<MTPChannelAdminLogEvent> &events);
	Element *viewForItem(const HistoryItem *item);

	void toggleScrollDateShown();
	void repaintScrollDateCallback();
	bool displayScrollDate() const;
	void scrollDateHide();
	void scrollDateCheck();
	void scrollDateHideByTimer();

	// This function finds all history items that are displayed and calls template method
	// for each found message (in given direction) in the passed history with passed top offset.
	//
	// Method has "bool (*Method)(not_null<Element*> view, int itemtop, int itembottom)" signature
	// if it returns false the enumeration stops immidiately.
	template <EnumItemsDirection direction, typename Method>
	void enumerateItems(Method method);

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
	// Method has "bool (*Method)(not_null<HistoryItem*> item, int itemtop, int dateTop)" signature
	// if it returns false the enumeration stops immidiately.
	template <typename Method>
	void enumerateDates(Method method);

	not_null<Window::Controller*> _controller;
	not_null<ChannelData*> _channel;
	not_null<History*> _history;
	std::vector<OwnedItem> _items;
	std::map<uint64, not_null<Element*>> _itemsByIds;
	std::map<not_null<HistoryItem*>, not_null<Element*>, std::less<>> _itemsByData;
	int _itemsTop = 0;
	int _itemsWidth = 0;
	int _itemsHeight = 0;

	int _minHeight = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;
	Element *_visibleTopItem = nullptr;
	int _visibleTopFromItem = 0;

	bool _scrollDateShown = false;
	Animation _scrollDateOpacity;
	SingleQueuedInvokation _scrollDateCheck;
	base::Timer _scrollDateHideTimer;
	Element *_scrollDateLastItem = nullptr;
	int _scrollDateLastItemTop = 0;

	// Up - max, Down - min.
	uint64 _maxId = 0;
	uint64 _minId = 0;
	mtpRequestId _preloadUpRequestId = 0;
	mtpRequestId _preloadDownRequestId = 0;

	// Don't load anything until the memento was read.
	bool _upLoaded = true;
	bool _downLoaded = true;
	bool _filterChanged = false;
	Text _emptyText;

	MouseAction _mouseAction = MouseAction::None;
	TextSelectType _mouseSelectType = TextSelectType::Letters;
	QPoint _dragStartPosition;
	QPoint _mousePosition;
	Element *_mouseActionItem = nullptr;
	CursorState _mouseCursorState = CursorState();
	uint16 _mouseTextSymbol = 0;
	bool _pressWasInactive = false;

	Element *_selectedItem = nullptr;
	TextSelection _selectedText;
	bool _wasSelectedText = false; // was some text selected in current drag action
	Qt::CursorShape _cursor = style::cur_default;

	base::unique_qptr<Ui::PopupMenu> _menu;

	QPoint _trippleClickPoint;
	base::Timer _trippleClickTimer;

	FilterValue _filter;
	QString _searchQuery;
	std::vector<not_null<UserData*>> _admins;
	std::vector<not_null<UserData*>> _adminsCanEdit;
	Fn<void(FilterValue &&filter)> _showFilterCallback;

	std::shared_ptr<LocalIdManager> _idManager;

};

} // namespace AdminLog
