/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "facades.h"

#include "info/info_memento.h"
#include "core/click_handler_types.h"
#include "media/media_clip_reader.h"
#include "window/window_controller.h"
#include "history/history_item_components.h"
#include "observer_peer.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "messenger.h"
#include "auth_session.h"
#include "boxes/confirm_box.h"
#include "window/layer_widget.h"
#include "lang/lang_keys.h"
#include "base/observer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_media.h"
#include "styles/style_history.h"
#include "data/data_session.h"

namespace App {
namespace internal {

void CallDelayed(int duration, FnMut<void()> &&lambda) {
	Messenger::Instance().callDelayed(duration, std::move(lambda));
}

} // namespace internal

void sendBotCommand(PeerData *peer, UserData *bot, const QString &cmd, MsgId replyTo) {
	if (auto m = main()) {
		m->sendBotCommand(peer, bot, cmd, replyTo);
	}
}

void hideSingleUseKeyboard(const HistoryItem *msg) {
	if (auto m = main()) {
		m->hideSingleUseKeyboard(msg->history()->peer, msg->id);
	}
}

bool insertBotCommand(const QString &cmd) {
	if (auto m = main()) {
		return m->insertBotCommand(cmd);
	}
	return false;
}

void activateBotCommand(
		not_null<const HistoryItem*> msg,
		int row,
		int column) {
	const HistoryMessageMarkupButton *button = nullptr;
	if (auto markup = msg->Get<HistoryMessageReplyMarkup>()) {
		if (row < markup->rows.size()) {
			auto &buttonRow = markup->rows[row];
			if (column < buttonRow.size()) {
				button = &buttonRow[column];
			}
		}
	}
	if (!button) return;

	using ButtonType = HistoryMessageMarkupButton::Type;
	switch (button->type) {
	case ButtonType::Default: {
		// Copy string before passing it to the sending method
		// because the original button can be destroyed inside.
		MsgId replyTo = (msg->id > 0) ? msg->id : 0;
		sendBotCommand(msg->history()->peer, msg->fromOriginal()->asUser(), QString(button->text), replyTo);
	} break;

	case ButtonType::Callback:
	case ButtonType::Game: {
		if (auto m = main()) {
			m->app_sendBotCallback(button, msg, row, column);
		}
	} break;

	case ButtonType::Buy: {
		Ui::show(Box<InformBox>(lang(lng_payments_not_supported)));
	} break;

	case ButtonType::Url: {
		auto url = QString::fromUtf8(button->data);
		auto skipConfirmation = false;
		if (auto bot = msg->getMessageBot()) {
			if (bot->isVerified()) {
				skipConfirmation = true;
			}
		}
		if (skipConfirmation) {
			UrlClickHandler::Open(url);
		} else {
			HiddenUrlClickHandler::Open(url);
		}
	} break;

	case ButtonType::RequestLocation: {
		hideSingleUseKeyboard(msg);
		Ui::show(Box<InformBox>(lang(lng_bot_share_location_unavailable)));
	} break;

	case ButtonType::RequestPhone: {
		hideSingleUseKeyboard(msg);
		const auto msgId = msg->id;
		const auto history = msg->history();
		Ui::show(Box<ConfirmBox>(lang(lng_bot_share_phone), lang(lng_bot_share_phone_confirm), [=] {
			Ui::showPeerHistory(history, ShowAtTheEndMsgId);
			auto options = ApiWrap::SendOptions(history);
			options.replyTo = msgId;
			Auth().api().shareContact(Auth().user(), options);
		}));
	} break;

	case ButtonType::SwitchInlineSame:
	case ButtonType::SwitchInline: {
		if (auto m = App::main()) {
			if (auto bot = msg->getMessageBot()) {
				auto tryFastSwitch = [bot, &button, msgId = msg->id]() -> bool {
					auto samePeer = (button->type == ButtonType::SwitchInlineSame);
					if (samePeer) {
						Notify::switchInlineBotButtonReceived(QString::fromUtf8(button->data), bot, msgId);
						return true;
					} else if (bot->botInfo && bot->botInfo->inlineReturnPeerId) {
						if (Notify::switchInlineBotButtonReceived(QString::fromUtf8(button->data))) {
							return true;
						}
					}
					return false;
				};
				if (!tryFastSwitch()) {
					m->inlineSwitchLayer('@' + bot->username + ' ' + QString::fromUtf8(button->data));
				}
			}
		}
	} break;
	}
}

void searchByHashtag(const QString &tag, PeerData *inPeer) {
	if (const auto m = main()) {
		Ui::hideSettingsAndLayer();
		Messenger::Instance().hideMediaView();
		if (inPeer && (!inPeer->isChannel() || inPeer->isMegagroup())) {
			inPeer = nullptr;
		}
		m->searchMessages(
			tag + ' ',
			(inPeer
				? App::history(inPeer).get()
				: Dialogs::Key()));
	}
}

void showSettings() {
	if (auto w = wnd()) {
		w->showSettings();
	}
}

void activateClickHandler(ClickHandlerPtr handler, ClickContext context) {
	crl::on_main(wnd(), [=] {
		handler->onClick(context);
	});
}

void activateClickHandler(ClickHandlerPtr handler, Qt::MouseButton button) {
	activateClickHandler(handler, ClickContext{ button });
}

} // namespace App

namespace Ui {
namespace internal {

void showBox(
		object_ptr<BoxContent> content,
		LayerOptions options,
		anim::type animated) {
	if (auto w = App::wnd()) {
		w->ui_showBox(std::move(content), options, animated);
	}
}

} // namespace internal

void showMediaPreview(
		Data::FileOrigin origin,
		not_null<DocumentData*> document) {
	if (auto w = App::wnd()) {
		w->ui_showMediaPreview(origin, document);
	}
}

void showMediaPreview(Data::FileOrigin origin, not_null<PhotoData*> photo) {
	if (auto w = App::wnd()) {
		w->ui_showMediaPreview(origin, photo);
	}
}

void hideLayer(anim::type animated) {
	if (auto w = App::wnd()) {
		w->ui_showBox(
			{ nullptr },
			LayerOption::CloseOther,
			animated);
	}
}

void hideSettingsAndLayer(anim::type animated) {
	if (auto w = App::wnd()) {
		w->ui_hideSettingsAndLayer(animated);
	}
}

bool isLayerShown() {
	if (auto w = App::wnd()) return w->ui_isLayerShown();
	return false;
}

void showPeerProfile(const PeerId &peer) {
	if (auto window = App::wnd()) {
		if (auto controller = window->controller()) {
			controller->showPeerInfo(peer);
		}
	}
}

void showPeerProfile(not_null<const History*> history) {
	showPeerProfile(history->peer->id);
}

void showPeerHistory(
		const PeerId &peer,
		MsgId msgId) {
	auto ms = getms();
	if (auto m = App::main()) {
		m->ui_showPeerHistory(
			peer,
			Window::SectionShow::Way::ClearStack,
			msgId);
	}
}

void showPeerHistoryAtItem(not_null<const HistoryItem*> item) {
	showPeerHistory(item->history()->peer->id, item->id);
}

void showPeerHistory(not_null<const History*> history, MsgId msgId) {
	showPeerHistory(history->peer->id, msgId);
}

PeerData *getPeerForMouseAction() {
	return Messenger::Instance().ui_getPeerForMouseAction();
}

bool skipPaintEvent(QWidget *widget, QPaintEvent *event) {
	if (auto w = App::wnd()) {
		if (w->contentOverlapped(widget, event)) {
			return true;
		}
	}
	return false;
}

} // namespace Ui

namespace Notify {

void userIsBotChanged(UserData *user) {
	if (MainWidget *m = App::main()) m->notify_userIsBotChanged(user);
}

void botCommandsChanged(UserData *user) {
	if (MainWidget *m = App::main()) {
		m->notify_botCommandsChanged(user);
	}
	peerUpdatedDelayed(user, PeerUpdate::Flag::BotCommandsChanged);
}

void inlineBotRequesting(bool requesting) {
	if (MainWidget *m = App::main()) m->notify_inlineBotRequesting(requesting);
}

void replyMarkupUpdated(const HistoryItem *item) {
	if (MainWidget *m = App::main()) {
		m->notify_replyMarkupUpdated(item);
	}
}

void inlineKeyboardMoved(const HistoryItem *item, int oldKeyboardTop, int newKeyboardTop) {
	if (MainWidget *m = App::main()) {
		m->notify_inlineKeyboardMoved(item, oldKeyboardTop, newKeyboardTop);
	}
}

bool switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot, MsgId samePeerReplyTo) {
	if (auto main = App::main()) {
		return main->notify_switchInlineBotButtonReceived(query, samePeerBot, samePeerReplyTo);
	}
	return false;
}

void migrateUpdated(PeerData *peer) {
	if (MainWidget *m = App::main()) m->notify_migrateUpdated(peer);
}

void historyMuteUpdated(History *history) {
	if (MainWidget *m = App::main()) m->notify_historyMuteUpdated(history);
}

void unreadCounterUpdated() {
	Global::RefHandleUnreadCounterUpdate().call();
}

} // namespace Notify

#define DefineReadOnlyVar(Namespace, Type, Name) const Type &Name() { \
	AssertCustom(Namespace##Data != nullptr, #Namespace "Data != nullptr in " #Namespace "::" #Name); \
	return Namespace##Data->Name; \
}
#define DefineRefVar(Namespace, Type, Name) DefineReadOnlyVar(Namespace, Type, Name) \
Type &Ref##Name() { \
	AssertCustom(Namespace##Data != nullptr, #Namespace "Data != nullptr in " #Namespace "::Ref" #Name); \
	return Namespace##Data->Name; \
}
#define DefineVar(Namespace, Type, Name) DefineRefVar(Namespace, Type, Name) \
void Set##Name(const Type &Name) { \
	AssertCustom(Namespace##Data != nullptr, #Namespace "Data != nullptr in " #Namespace "::Set" #Name); \
	Namespace##Data->Name = Name; \
}

namespace Sandbox {
namespace internal {

struct Data {
	QByteArray LastCrashDump;
	ProxyData PreLaunchProxy;
};

} // namespace internal
} // namespace Sandbox

std::unique_ptr<Sandbox::internal::Data> SandboxData;
uint64 SandboxUserTag = 0;

namespace Sandbox {

bool MoveLegacyAlphaFolder(const QString &folder, const QString &file) {
	const auto was = cExeDir() + folder;
	const auto now = cExeDir() + qsl("TelegramForcePortable");
	if (QDir(was).exists() && !QDir(now).exists()) {
		const auto oldFile = was + "/tdata/" + file;
		const auto newFile = was + "/tdata/alpha";
		if (QFile(oldFile).exists() && !QFile(newFile).exists()) {
			if (!QFile(oldFile).copy(newFile)) {
				LOG(("FATAL: Could not copy '%1' to '%2'"
					).arg(oldFile
					).arg(newFile));
				return false;
			}
		}
		if (!QDir().rename(was, now)) {
			LOG(("FATAL: Could not rename '%1' to '%2'"
				).arg(was
				).arg(now));
			return false;
		}
	}
	return true;
}

bool MoveLegacyAlphaFolder() {
	if (!MoveLegacyAlphaFolder(qsl("TelegramAlpha_data"), qsl("alpha"))
		|| !MoveLegacyAlphaFolder(qsl("TelegramBeta_data"), qsl("beta"))) {
		return false;
	}
	return true;
}

bool CheckPortableVersionDir() {
	if (!MoveLegacyAlphaFolder()) {
		return false;
	}

	const auto portable = cExeDir() + qsl("TelegramForcePortable");
	QFile key(portable + qsl("/tdata/alpha"));
	if (cAlphaVersion()) {
		Assert(*AlphaPrivateKey != 0);

		cForceWorkingDir(portable + '/');
		QDir().mkpath(cWorkingDir() + qstr("tdata"));
		cSetAlphaPrivateKey(QByteArray(AlphaPrivateKey));
		if (!key.open(QIODevice::WriteOnly)) {
			LOG(("FATAL: Could not open '%1' for writing private key!"
				).arg(key.fileName()));
			return false;
		}
		QDataStream dataStream(&key);
		dataStream.setVersion(QDataStream::Qt_5_3);
		dataStream << quint64(cRealAlphaVersion()) << cAlphaPrivateKey();
		return true;
	}
	if (!QDir(portable).exists()) {
		return true;
	}
	cForceWorkingDir(portable + '/');
	if (!key.exists()) {
		return true;
	}

	if (!key.open(QIODevice::ReadOnly)) {
		LOG(("FATAL: could not open '%1' for reading private key. "
			"Delete it or reinstall private alpha version."
			).arg(key.fileName()));
		return false;
	}
	QDataStream dataStream(&key);
	dataStream.setVersion(QDataStream::Qt_5_3);

	quint64 v;
	QByteArray k;
	dataStream >> v >> k;
	if (dataStream.status() != QDataStream::Ok || k.isEmpty()) {
		LOG(("FATAL: '%1' is corrupted. "
			"Delete it or reinstall private alpha version."
			).arg(key.fileName()));
		return false;
	}
	cSetAlphaVersion(AppVersion * 1000ULL);
	cSetAlphaPrivateKey(k);
	cSetRealAlphaVersion(v);
	return true;
}

QString InstallBetaVersionsSettingPath() {
	return cWorkingDir() + qsl("tdata/devversion");
}

void WriteInstallBetaVersionsSetting() {
	QFile f(InstallBetaVersionsSettingPath());
	if (f.open(QIODevice::WriteOnly)) {
		f.write(cInstallBetaVersion() ? "1" : "0");
	}
}

QString DebugModeSettingPath() {
	return cWorkingDir() + qsl("tdata/withdebug");
}

void WriteDebugModeSetting() {
	QFile f(DebugModeSettingPath());
	if (f.open(QIODevice::WriteOnly)) {
		f.write(Logs::DebugEnabled() ? "1" : "0");
	}
}

void ComputeTestMode() {
	if (QFile(cWorkingDir() + qsl("tdata/withtestmode")).exists()) {
		cSetTestMode(true);
	}
}

void ComputeDebugMode() {
	Logs::SetDebugEnabled(cAlphaVersion() != 0);
	const auto debugModeSettingPath = DebugModeSettingPath();
	if (QFile(debugModeSettingPath).exists()) {
		QFile f(debugModeSettingPath);
		if (f.open(QIODevice::ReadOnly)) {
			Logs::SetDebugEnabled(f.read(1) != "0");
		}
	}
}

void ComputeInstallBetaVersions() {
	const auto installBetaSettingPath = InstallBetaVersionsSettingPath();
	if (cAlphaVersion()) {
		cSetInstallBetaVersion(false);
	} else if (QFile(installBetaSettingPath).exists()) {
		QFile f(installBetaSettingPath);
		if (f.open(QIODevice::ReadOnly)) {
			cSetInstallBetaVersion(f.read(1) != "0");
		}
	} else if (AppBetaVersion) {
		WriteInstallBetaVersionsSetting();
	}
}

void ComputeUserTag() {
	SandboxUserTag = 0;
	QFile usertag(cWorkingDir() + qsl("tdata/usertag"));
	if (usertag.open(QIODevice::ReadOnly)) {
		if (usertag.read(reinterpret_cast<char*>(&SandboxUserTag), sizeof(uint64)) != sizeof(uint64)) {
			SandboxUserTag = 0;
		}
		usertag.close();
	}
	if (!SandboxUserTag) {
		do {
			memsetrnd_bad(SandboxUserTag);
		} while (!SandboxUserTag);

		if (usertag.open(QIODevice::WriteOnly)) {
			usertag.write(reinterpret_cast<char*>(&SandboxUserTag), sizeof(uint64));
			usertag.close();
		}
	}
}

void WorkingDirReady() {
	srand((int32)time(NULL));

	ComputeTestMode();
	ComputeDebugMode();
	ComputeInstallBetaVersions();
	ComputeUserTag();
}

void start() {
	SandboxData = std::make_unique<internal::Data>();
}

bool started() {
	return (SandboxData != nullptr);
}

void finish() {
	SandboxData.reset();
}

uint64 UserTag() {
	return SandboxUserTag;
}

DefineVar(Sandbox, QByteArray, LastCrashDump);
DefineVar(Sandbox, ProxyData, PreLaunchProxy);

} // namespace Sandbox

namespace Global {
namespace internal {

struct Data {
	SingleQueuedInvokation HandleUnreadCounterUpdate = { [] { Messenger::Instance().call_handleUnreadCounterUpdate(); } };
	SingleQueuedInvokation HandleDelayedPeerUpdates = { [] { Messenger::Instance().call_handleDelayedPeerUpdates(); } };
	SingleQueuedInvokation HandleObservables = { [] { Messenger::Instance().call_handleObservables(); } };

	Adaptive::WindowLayout AdaptiveWindowLayout = Adaptive::WindowLayout::Normal;
	Adaptive::ChatLayout AdaptiveChatLayout = Adaptive::ChatLayout::Normal;
	bool AdaptiveForWide = true;
	base::Observable<void> AdaptiveChanged;

	bool DialogsModeEnabled = false;
	Dialogs::Mode DialogsMode = Dialogs::Mode::All;
	bool ModerateModeEnabled = false;

	bool ScreenIsLocked = false;

	int32 DebugLoggingFlags = 0;

	float64 RememberedSongVolume = kDefaultVolume;
	float64 SongVolume = kDefaultVolume;
	base::Observable<void> SongVolumeChanged;
	float64 VideoVolume = kDefaultVolume;
	base::Observable<void> VideoVolumeChanged;

	// config
	int32 ChatSizeMax = 200;
	int32 MegagroupSizeMax = 10000;
	int32 ForwardedCountMax = 100;
	int32 OnlineUpdatePeriod = 120000;
	int32 OfflineBlurTimeout = 5000;
	int32 OfflineIdleTimeout = 30000;
	int32 OnlineFocusTimeout = 1000;
	int32 OnlineCloudTimeout = 300000;
	int32 NotifyCloudDelay = 30000;
	int32 NotifyDefaultDelay = 1500;
	int32 PushChatPeriod = 60000;
	int32 PushChatLimit = 2;
	int32 SavedGifsLimit = 200;
	int32 EditTimeLimit = 172800;
	int32 RevokeTimeLimit = 172800;
	int32 RevokePrivateTimeLimit = 172800;
	bool RevokePrivateInbox = false;
	int32 StickersRecentLimit = 30;
	int32 StickersFavedLimit = 5;
	int32 PinnedDialogsCountMax = 5;
	QString InternalLinksDomain = qsl("https://t.me/");
	int32 ChannelsReadMediaPeriod = 86400 * 7;
	int32 CallReceiveTimeoutMs = 20000;
	int32 CallRingTimeoutMs = 90000;
	int32 CallConnectTimeoutMs = 30000;
	int32 CallPacketTimeoutMs = 10000;
	int32 WebFileDcId = cTestMode() ? 2 : 4;
	QString TxtDomainString = cTestMode()
		? qsl("testapv2.stel.com")
		: qsl("apv2.stel.com");
	bool PhoneCallsEnabled = true;
	bool BlockedMode = false;
	int32 CaptionLengthMax = 1024;
	base::Observable<void> PhoneCallsEnabledChanged;

	HiddenPinnedMessagesMap HiddenPinnedMessages;

	Stickers::Sets StickerSets;
	Stickers::Order StickerSetsOrder;
	TimeMs LastStickersUpdate = 0;
	TimeMs LastRecentStickersUpdate = 0;
	TimeMs LastFavedStickersUpdate = 0;
	Stickers::Order FeaturedStickerSetsOrder;
	int FeaturedStickerSetsUnreadCount = 0;
	base::Observable<void> FeaturedStickerSetsUnreadCountChanged;
	TimeMs LastFeaturedStickersUpdate = 0;
	Stickers::Order ArchivedStickerSetsOrder;

	CircleMasksMap CircleMasks;

	bool AskDownloadPath = false;
	QString DownloadPath;
	QByteArray DownloadPathBookmark;
	base::Observable<void> DownloadPathChanged;

	bool ReplaceEmoji = true;
	bool SuggestEmoji = true;
	bool SuggestStickersByEmoji = true;
	base::Observable<void> ReplaceEmojiChanged;
	bool VoiceMsgPlaybackDoubled = false;
	bool SoundNotify = true;
	bool DesktopNotify = true;
	bool RestoreSoundNotifyFromTray = false;
	DBINotifyView NotifyView = dbinvShowPreview;
	bool NativeNotifications = false;
	int NotificationsCount = 3;
	Notify::ScreenCorner NotificationsCorner = Notify::ScreenCorner::BottomRight;
	bool NotificationsDemoIsShown = false;

	bool TryIPv6 = (cPlatform() == dbipWindows) ? false : true;
	std::vector<ProxyData> ProxiesList;
	ProxyData SelectedProxy;
	ProxyData::Settings ProxySettings = ProxyData::Settings::System;
	bool UseProxyForCalls = false;
	base::Observable<void> ConnectionTypeChanged;

	int AutoLock = 3600;
	bool LocalPasscode = false;
	base::Observable<void> LocalPasscodeChanged;

	base::Variable<DBIWorkMode> WorkMode = { dbiwmWindowAndTray };

	base::Observable<void> UnreadCounterUpdate;
	base::Observable<void> PeerChooseCancel;

};

} // namespace internal
} // namespace Global

Global::internal::Data *GlobalData = nullptr;

namespace Global {

bool started() {
	return GlobalData != nullptr;
}

void start() {
	GlobalData = new internal::Data();
}

void finish() {
	delete GlobalData;
	GlobalData = nullptr;
}

DefineRefVar(Global, SingleQueuedInvokation, HandleUnreadCounterUpdate);
DefineRefVar(Global, SingleQueuedInvokation, HandleDelayedPeerUpdates);
DefineRefVar(Global, SingleQueuedInvokation, HandleObservables);

DefineVar(Global, Adaptive::WindowLayout, AdaptiveWindowLayout);
DefineVar(Global, Adaptive::ChatLayout, AdaptiveChatLayout);
DefineVar(Global, bool, AdaptiveForWide);
DefineRefVar(Global, base::Observable<void>, AdaptiveChanged);

DefineVar(Global, bool, DialogsModeEnabled);
DefineVar(Global, Dialogs::Mode, DialogsMode);
DefineVar(Global, bool, ModerateModeEnabled);

DefineVar(Global, bool, ScreenIsLocked);

DefineVar(Global, int32, DebugLoggingFlags);

DefineVar(Global, float64, RememberedSongVolume);
DefineVar(Global, float64, SongVolume);
DefineRefVar(Global, base::Observable<void>, SongVolumeChanged);
DefineVar(Global, float64, VideoVolume);
DefineRefVar(Global, base::Observable<void>, VideoVolumeChanged);

// config
DefineVar(Global, int32, ChatSizeMax);
DefineVar(Global, int32, MegagroupSizeMax);
DefineVar(Global, int32, ForwardedCountMax);
DefineVar(Global, int32, OnlineUpdatePeriod);
DefineVar(Global, int32, OfflineBlurTimeout);
DefineVar(Global, int32, OfflineIdleTimeout);
DefineVar(Global, int32, OnlineFocusTimeout);
DefineVar(Global, int32, OnlineCloudTimeout);
DefineVar(Global, int32, NotifyCloudDelay);
DefineVar(Global, int32, NotifyDefaultDelay);
DefineVar(Global, int32, PushChatPeriod);
DefineVar(Global, int32, PushChatLimit);
DefineVar(Global, int32, SavedGifsLimit);
DefineVar(Global, int32, EditTimeLimit);
DefineVar(Global, int32, RevokeTimeLimit);
DefineVar(Global, int32, RevokePrivateTimeLimit);
DefineVar(Global, bool, RevokePrivateInbox);
DefineVar(Global, int32, StickersRecentLimit);
DefineVar(Global, int32, StickersFavedLimit);
DefineVar(Global, int32, PinnedDialogsCountMax);
DefineVar(Global, QString, InternalLinksDomain);
DefineVar(Global, int32, ChannelsReadMediaPeriod);
DefineVar(Global, int32, CallReceiveTimeoutMs);
DefineVar(Global, int32, CallRingTimeoutMs);
DefineVar(Global, int32, CallConnectTimeoutMs);
DefineVar(Global, int32, CallPacketTimeoutMs);
DefineVar(Global, int32, WebFileDcId);
DefineVar(Global, QString, TxtDomainString);
DefineVar(Global, bool, PhoneCallsEnabled);
DefineVar(Global, bool, BlockedMode);
DefineVar(Global, int32, CaptionLengthMax);
DefineRefVar(Global, base::Observable<void>, PhoneCallsEnabledChanged);

DefineVar(Global, HiddenPinnedMessagesMap, HiddenPinnedMessages);

DefineVar(Global, Stickers::Sets, StickerSets);
DefineVar(Global, Stickers::Order, StickerSetsOrder);
DefineVar(Global, TimeMs, LastStickersUpdate);
DefineVar(Global, TimeMs, LastRecentStickersUpdate);
DefineVar(Global, TimeMs, LastFavedStickersUpdate);
DefineVar(Global, Stickers::Order, FeaturedStickerSetsOrder);
DefineVar(Global, int, FeaturedStickerSetsUnreadCount);
DefineRefVar(Global, base::Observable<void>, FeaturedStickerSetsUnreadCountChanged);
DefineVar(Global, TimeMs, LastFeaturedStickersUpdate);
DefineVar(Global, Stickers::Order, ArchivedStickerSetsOrder);

DefineRefVar(Global, CircleMasksMap, CircleMasks);

DefineVar(Global, bool, AskDownloadPath);
DefineVar(Global, QString, DownloadPath);
DefineVar(Global, QByteArray, DownloadPathBookmark);
DefineRefVar(Global, base::Observable<void>, DownloadPathChanged);

DefineVar(Global, bool, ReplaceEmoji);
DefineVar(Global, bool, SuggestEmoji);
DefineVar(Global, bool, SuggestStickersByEmoji);
DefineRefVar(Global, base::Observable<void>, ReplaceEmojiChanged);
DefineVar(Global, bool, VoiceMsgPlaybackDoubled);
DefineVar(Global, bool, SoundNotify);
DefineVar(Global, bool, DesktopNotify);
DefineVar(Global, bool, RestoreSoundNotifyFromTray);
DefineVar(Global, DBINotifyView, NotifyView);
DefineVar(Global, bool, NativeNotifications);
DefineVar(Global, int, NotificationsCount);
DefineVar(Global, Notify::ScreenCorner, NotificationsCorner);
DefineVar(Global, bool, NotificationsDemoIsShown);

DefineVar(Global, bool, TryIPv6);
DefineVar(Global, std::vector<ProxyData>, ProxiesList);
DefineVar(Global, ProxyData, SelectedProxy);
DefineVar(Global, ProxyData::Settings, ProxySettings);
DefineVar(Global, bool, UseProxyForCalls);
DefineRefVar(Global, base::Observable<void>, ConnectionTypeChanged);

DefineVar(Global, int, AutoLock);
DefineVar(Global, bool, LocalPasscode);
DefineRefVar(Global, base::Observable<void>, LocalPasscodeChanged);

DefineRefVar(Global, base::Variable<DBIWorkMode>, WorkMode);

DefineRefVar(Global, base::Observable<void>, UnreadCounterUpdate);
DefineRefVar(Global, base::Observable<void>, PeerChooseCancel);

rpl::producer<bool> ReplaceEmojiValue() {
	return rpl::single(
		Global::ReplaceEmoji()
	) | rpl::then(base::ObservableViewer(
		Global::RefReplaceEmojiChanged()
	) | rpl::map([] {
		return Global::ReplaceEmoji();
	}));
}

} // namespace Global
