/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/panel_animation.h"
#include "base/unique_qptr.h"

namespace Ui {

class InnerDropdown;
class InputField;

namespace Emoji {

class SuggestionsWidget : public TWidget {
public:
	SuggestionsWidget(QWidget *parent, const style::Menu &st);

	void showWithQuery(const QString &query);
	void handleKeyEvent(int key);

	rpl::producer<bool> toggleAnimated() const;
	rpl::producer<QString> triggered() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	class Row;

	std::vector<Row> getRowsByQuery() const;
	void resizeToRows();
	int countWidth(const Row &row);
	void setSelected(int selected);
	void setPressed(int pressed);
	void clearMouseSelection();
	void clearSelection();
	void updateSelectedItem();
	int itemTop(int index);
	void updateItem(int index);
	void updateSelection(QPoint globalPosition);
	void triggerSelectedRow();
	void triggerRow(const Row &row);

	not_null<const style::Menu*> _st;

	QString _query;
	std::vector<Row> _rows;

	int _rowHeight = 0;
	bool _mouseSelection = false;
	int _selected = -1;
	int _pressed = -1;

	rpl::event_stream<bool> _toggleAnimated;
	rpl::event_stream<QString> _triggered;

};

class SuggestionsController {
public:
	SuggestionsController(
		not_null<QWidget*> outer,
		not_null<QTextEdit*> field);

	void raise();
	void setReplaceCallback(Fn<void(
		int from,
		int till,
		const QString &replacement)> callback);

	static SuggestionsController *Init(
		not_null<QWidget*> outer,
		not_null<Ui::InputField*> field);

private:
	void handleCursorPositionChange();
	void handleTextChange();
	QString getEmojiQuery();
	void suggestionsUpdated(bool visible);
	void updateGeometry();
	void updateForceHidden();
	void replaceCurrent(const QString &replacement);
	bool fieldFilter(not_null<QEvent*> event);
	bool outerFilter(not_null<QEvent*> event);

	bool _shown = false;
	bool _forceHidden = false;
	int _queryStartPosition = 0;
	bool _ignoreCursorPositionChange = false;
	bool _textChangeAfterKeyPress = false;
	QPointer<QTextEdit> _field;
	Fn<void(
		int from,
		int till,
		const QString &replacement)> _replaceCallback;
	base::unique_qptr<InnerDropdown> _container;
	QPointer<SuggestionsWidget> _suggestions;
	base::unique_qptr<QObject> _fieldFilter;
	base::unique_qptr<QObject> _outerFilter;

	rpl::lifetime _lifetime;

};

} // namespace Emoji
} // namespace Ui
