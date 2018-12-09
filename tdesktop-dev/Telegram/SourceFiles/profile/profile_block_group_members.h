/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "profile/profile_block_peer_list.h"

namespace Ui {
class FlatLabel;
class LeftOutlineButton;
} // namespace Ui

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Profile {

class GroupMembersWidget : public PeerListWidget {
	Q_OBJECT

public:
	enum class TitleVisibility {
		Visible,
		Hidden,
	};
	GroupMembersWidget(QWidget *parent, PeerData *peer, TitleVisibility titleVisibility = TitleVisibility::Visible, const style::PeerListItem &st = st::profileMemberItem);

	int onlineCount() const {
		return _onlineCount;
	}

	~GroupMembersWidget();

protected:
	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

	void paintContents(Painter &p) override;

	Ui::PopupMenu *fillPeerMenu(PeerData *peer) override;

signals:
	void onlineCountUpdated(int onlineCount);

private slots:
	void onUpdateOnlineDisplay();

private:
	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);

	void editAdmin(not_null<UserData*> user);
	void restrictUser(not_null<UserData*> user);
	void removePeer(PeerData *selectedPeer);
	void refreshMembers();
	void fillChatMembers(ChatData *chat);
	void fillMegagroupMembers(ChannelData *megagroup);
	void sortMembers();
	void updateOnlineCount();
	void checkSelfAdmin(ChatData *chat);
	void refreshLimitReached();

	void preloadMore();

	bool limitReachedHook(const ClickHandlerPtr &handler, Qt::MouseButton button);

	void refreshUserOnline(UserData *user);

	int getListTop() const override;

	struct Member : public Item {
		explicit Member(UserData *user) : Item(user) {
		}
		UserData *user() const {
			return static_cast<UserData*>(peer);
		}
		TimeId onlineTextTill = 0;
		TimeId onlineTill = 0;
		TimeId onlineForSort = 0;
	};
	Member *getMember(Item *item) {
		return static_cast<Member*>(item);
	}

	void updateItemStatusText(Item *item);
	Member *computeMember(UserData *user);
	Member *addUser(ChatData *chat, UserData *user);
	Member *addUser(ChannelData *megagroup, UserData *user);
	void setItemFlags(Item *item, ChatData *chat);
	void setItemFlags(Item *item, ChannelData *megagroup);
	bool addUsersToEnd(ChannelData *megagroup);

	object_ptr<Ui::FlatLabel> _limitReachedInfo = { nullptr };

	QMap<UserData*, Member*> _membersByUser;
	bool _sortByOnline = false;
	TimeId _now = 0;

	int _onlineCount = 0;
	TimeId _updateOnlineAt = 0;
	QTimer _updateOnlineTimer;

};

} // namespace Profile
