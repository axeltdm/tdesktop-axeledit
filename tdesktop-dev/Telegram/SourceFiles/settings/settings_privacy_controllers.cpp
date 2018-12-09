/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_privacy_controllers.h"

#include "settings/settings_common.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "observer_peer.h"
#include "mainwidget.h"
#include "auth_session.h"
#include "storage/localstorage.h"
#include "history/history.h"
#include "calls/calls_instance.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/vertical_layout.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/confirm_box.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

constexpr auto kBlockedPerPage = 40;

class BlockUserBoxController : public ChatsListBoxController {
public:
	void rowClicked(not_null<PeerListRow*> row) override;

	void setBlockUserCallback(Fn<void(not_null<UserData*> user)> callback) {
		_blockUserCallback = std::move(callback);
	}

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;
	void updateRowHook(not_null<Row*> row) override {
		updateIsBlocked(row, row->peer()->asUser());
		delegate()->peerListUpdateRow(row);
	}

private:
	void updateIsBlocked(not_null<PeerListRow*> row, UserData *user) const;

	Fn<void(not_null<UserData*> user)> _blockUserCallback;

};

void BlockUserBoxController::prepareViewHook() {
	delegate()->peerListSetTitle(langFactory(lng_blocked_list_add_title));
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::UserIsBlocked, [this](const Notify::PeerUpdate &update) {
		if (auto user = update.peer->asUser()) {
			if (auto row = delegate()->peerListFindRow(user->id)) {
				updateIsBlocked(row, user);
				delegate()->peerListUpdateRow(row);
			}
		}
	}));
}

void BlockUserBoxController::updateIsBlocked(not_null<PeerListRow*> row, UserData *user) const {
	auto blocked = user->isBlocked();
	row->setDisabledState(blocked ? PeerListRow::State::DisabledChecked : PeerListRow::State::Active);
	if (blocked) {
		row->setCustomStatus(lang(lng_blocked_list_already_blocked));
	} else {
		row->clearCustomStatus();
	}
}

void BlockUserBoxController::rowClicked(not_null<PeerListRow*> row) {
	_blockUserCallback(row->peer()->asUser());
}

std::unique_ptr<BlockUserBoxController::Row> BlockUserBoxController::createRow(not_null<History*> history) {
	if (history->peer->isSelf()) {
		return nullptr;
	}
	if (auto user = history->peer->asUser()) {
		auto row = std::make_unique<Row>(history);
		updateIsBlocked(row.get(), user);
		return row;
	}
	return nullptr;
}

} // namespace

void BlockedBoxController::prepare() {
	delegate()->peerListSetTitle(langFactory(lng_blocked_list_title));
	setDescriptionText(lang(lng_contacts_loading));
	delegate()->peerListRefreshRows();

	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::UserIsBlocked, [this](const Notify::PeerUpdate &update) {
		if (const auto user = update.peer->asUser()) {
			handleBlockedEvent(user);
		}
	}));

	loadMoreRows();
}

void BlockedBoxController::loadMoreRows() {
	if (_loadRequestId || _allLoaded) {
		return;
	}

	_loadRequestId = request(MTPcontacts_GetBlocked(MTP_int(_offset), MTP_int(kBlockedPerPage))).done([this](const MTPcontacts_Blocked &result) {
		_loadRequestId = 0;

		if (!_offset) {
			setDescriptionText(lang(lng_blocked_list_about));
		}

		auto handleContactsBlocked = [](auto &list) {
			App::feedUsers(list.vusers);
			return list.vblocked.v;
		};
		switch (result.type()) {
		case mtpc_contacts_blockedSlice: {
			receivedUsers(handleContactsBlocked(result.c_contacts_blockedSlice()));
		} break;
		case mtpc_contacts_blocked: {
			_allLoaded = true;
			receivedUsers(handleContactsBlocked(result.c_contacts_blocked()));
		} break;
		default: Unexpected("Bad type() in MTPcontacts_GetBlocked() result.");
		}
	}).fail([this](const RPCError &error) {
		_loadRequestId = 0;
	}).send();
}

void BlockedBoxController::rowClicked(not_null<PeerListRow*> row) {
	InvokeQueued(App::main(), [peerId = row->peer()->id] {
		Ui::showPeerHistory(peerId, ShowAtUnreadMsgId);
	});
}

void BlockedBoxController::rowActionClicked(not_null<PeerListRow*> row) {
	auto user = row->peer()->asUser();
	Expects(user != nullptr);

	Auth().api().unblockUser(user);
}

void BlockedBoxController::receivedUsers(const QVector<MTPContactBlocked> &result) {
	if (result.empty()) {
		_allLoaded = true;
	}

	for_const (auto &item, result) {
		++_offset;
		if (item.type() != mtpc_contactBlocked) {
			continue;
		}
		auto &contactBlocked = item.c_contactBlocked();
		auto userId = contactBlocked.vuser_id.v;
		if (auto user = App::userLoaded(userId)) {
			appendRow(user);
			user->setBlockStatus(UserData::BlockStatus::Blocked);
		}
	}
	delegate()->peerListRefreshRows();
}

void BlockedBoxController::handleBlockedEvent(not_null<UserData*> user) {
	if (user->isBlocked()) {
		if (prependRow(user)) {
			delegate()->peerListRefreshRows();
			delegate()->peerListScrollToTop();
		}
	} else if (auto row = delegate()->peerListFindRow(user->id)) {
		delegate()->peerListRemoveRow(row);
		delegate()->peerListRefreshRows();
	}
}

void BlockedBoxController::BlockNewUser() {
	auto controller = std::make_unique<BlockUserBoxController>();
	auto initBox = [controller = controller.get()](not_null<PeerListBox*> box) {
		controller->setBlockUserCallback([box](not_null<UserData*> user) {
			Auth().api().blockUser(user);
			box->closeBox();
		});
		box->addButton(langFactory(lng_cancel), [box] { box->closeBox(); });
	};
	Ui::show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)),
		LayerOption::KeepOther);
}

bool BlockedBoxController::appendRow(not_null<UserData*> user) {
	if (delegate()->peerListFindRow(user->id)) {
		return false;
	}
	delegate()->peerListAppendRow(createRow(user));
	return true;
}

bool BlockedBoxController::prependRow(not_null<UserData*> user) {
	if (delegate()->peerListFindRow(user->id)) {
		return false;
	}
	delegate()->peerListPrependRow(createRow(user));
	return true;
}

std::unique_ptr<PeerListRow> BlockedBoxController::createRow(
		not_null<UserData*> user) const {
	auto row = std::make_unique<PeerListRowWithLink>(user);
	row->setActionLink(lang(user->botInfo
		? lng_blocked_list_restart
		: lng_blocked_list_unblock));
	const auto status = [&] {
		if (!user->phone().isEmpty()) {
			return App::formatPhone(user->phone());
		} else if (!user->username.isEmpty()) {
			return '@' + user->username;
		} else if (user->botInfo) {
			return lang(lng_status_bot);
		}
		return lang(lng_blocked_list_unknown_phone);
	}();
	row->setCustomStatus(status);
	return std::move(row);
}

ApiWrap::Privacy::Key LastSeenPrivacyController::key() {
	return Key::LastSeen;
}

MTPInputPrivacyKey LastSeenPrivacyController::apiKey() {
	return MTP_inputPrivacyKeyStatusTimestamp();
}

QString LastSeenPrivacyController::title() {
	return lang(lng_edit_privacy_lastseen_title);
}

LangKey LastSeenPrivacyController::optionsTitleKey() {
	return lng_edit_privacy_lastseen_header;
}

rpl::producer<QString> LastSeenPrivacyController::warning() {
	return Lang::Viewer(lng_edit_privacy_lastseen_warning);
}

LangKey LastSeenPrivacyController::exceptionButtonTextKey(
		Exception exception) {
	switch (exception) {
	case Exception::Always:
		return lng_edit_privacy_lastseen_always_empty;
	case Exception::Never:
		return lng_edit_privacy_lastseen_never_empty;
	}
	Unexpected("Invalid exception value.");
}

QString LastSeenPrivacyController::exceptionBoxTitle(Exception exception) {
	switch (exception) {
	case Exception::Always: return lang(lng_edit_privacy_lastseen_always_title);
	case Exception::Never: return lang(lng_edit_privacy_lastseen_never_title);
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> LastSeenPrivacyController::exceptionsDescription() {
	return Lang::Viewer(lng_edit_privacy_lastseen_exceptions);
}

void LastSeenPrivacyController::confirmSave(bool someAreDisallowed, FnMut<void()> saveCallback) {
	if (someAreDisallowed && !Auth().settings().lastSeenWarningSeen()) {
		auto weakBox = std::make_shared<QPointer<ConfirmBox>>();
		auto callback = [weakBox, saveCallback = std::move(saveCallback)]() mutable {
			if (auto box = *weakBox) {
				box->closeBox();
			}
			saveCallback();
			Auth().settings().setLastSeenWarningSeen(true);
			Local::writeUserSettings();
		};
		auto box = Box<ConfirmBox>(lang(lng_edit_privacy_lastseen_warning), lang(lng_continue), lang(lng_cancel), std::move(callback));
		*weakBox = Ui::show(std::move(box), LayerOption::KeepOther);
	} else {
		saveCallback();
	}
}

ApiWrap::Privacy::Key GroupsInvitePrivacyController::key() {
	return Key::Invites;
}

MTPInputPrivacyKey GroupsInvitePrivacyController::apiKey() {
	return MTP_inputPrivacyKeyChatInvite();
}

QString GroupsInvitePrivacyController::title() {
	return lang(lng_edit_privacy_groups_title);
}

bool GroupsInvitePrivacyController::hasOption(Option option) {
	return (option != Option::Nobody);
}

LangKey GroupsInvitePrivacyController::optionsTitleKey() {
	return lng_edit_privacy_groups_header;
}

LangKey GroupsInvitePrivacyController::exceptionButtonTextKey(
		Exception exception) {
	switch (exception) {
	case Exception::Always: return lng_edit_privacy_groups_always_empty;
	case Exception::Never: return lng_edit_privacy_groups_never_empty;
	}
	Unexpected("Invalid exception value.");
}

QString GroupsInvitePrivacyController::exceptionBoxTitle(Exception exception) {
	switch (exception) {
	case Exception::Always: return lang(lng_edit_privacy_groups_always_title);
	case Exception::Never: return lang(lng_edit_privacy_groups_never_title);
	}
	Unexpected("Invalid exception value.");
}

auto GroupsInvitePrivacyController::exceptionsDescription()
-> rpl::producer<QString> {
	return Lang::Viewer(lng_edit_privacy_groups_exceptions);
}

ApiWrap::Privacy::Key CallsPrivacyController::key() {
	return Key::Calls;
}

MTPInputPrivacyKey CallsPrivacyController::apiKey() {
	return MTP_inputPrivacyKeyPhoneCall();
}

QString CallsPrivacyController::title() {
	return lang(lng_edit_privacy_calls_title);
}

LangKey CallsPrivacyController::optionsTitleKey() {
	return lng_edit_privacy_calls_header;
}

LangKey CallsPrivacyController::exceptionButtonTextKey(
		Exception exception) {
	switch (exception) {
	case Exception::Always: return lng_edit_privacy_calls_always_empty;
	case Exception::Never: return lng_edit_privacy_calls_never_empty;
	}
	Unexpected("Invalid exception value.");
}

QString CallsPrivacyController::exceptionBoxTitle(Exception exception) {
	switch (exception) {
	case Exception::Always: return lang(lng_edit_privacy_calls_always_title);
	case Exception::Never: return lang(lng_edit_privacy_calls_never_title);
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> CallsPrivacyController::exceptionsDescription() {
	return Lang::Viewer(lng_edit_privacy_calls_exceptions);
}

ApiWrap::Privacy::Key CallsPeer2PeerPrivacyController::key() {
	return Key::CallsPeer2Peer;
}

MTPInputPrivacyKey CallsPeer2PeerPrivacyController::apiKey() {
	return MTP_inputPrivacyKeyPhoneP2P();
}

QString CallsPeer2PeerPrivacyController::title() {
	return lang(lng_edit_privacy_calls_p2p_title);
}

LangKey CallsPeer2PeerPrivacyController::optionsTitleKey() {
	return lng_edit_privacy_calls_p2p_header;
}

LangKey CallsPeer2PeerPrivacyController::optionLabelKey(
		EditPrivacyBox::Option option) {
	switch (option) {
		case Option::Everyone: return lng_edit_privacy_calls_p2p_everyone;
		case Option::Contacts: return lng_edit_privacy_calls_p2p_contacts;
		case Option::Nobody: return lng_edit_privacy_calls_p2p_nobody;
	}
	Unexpected("Option value in optionsLabelKey.");
}

rpl::producer<QString> CallsPeer2PeerPrivacyController::warning() {
	return Lang::Viewer(lng_settings_peer_to_peer_about);
}

LangKey CallsPeer2PeerPrivacyController::exceptionButtonTextKey(
		Exception exception) {
	switch (exception) {
	case Exception::Always: return lng_edit_privacy_calls_p2p_always_empty;
	case Exception::Never: return lng_edit_privacy_calls_p2p_never_empty;
	}
	Unexpected("Invalid exception value.");
}

QString CallsPeer2PeerPrivacyController::exceptionBoxTitle(Exception exception) {
	switch (exception) {
	case Exception::Always: return lang(lng_edit_privacy_calls_p2p_always_title);
	case Exception::Never: return lang(lng_edit_privacy_calls_p2p_never_title);
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> CallsPeer2PeerPrivacyController::exceptionsDescription() {
	return Lang::Viewer(lng_edit_privacy_calls_p2p_exceptions);
}

} // namespace Settings
