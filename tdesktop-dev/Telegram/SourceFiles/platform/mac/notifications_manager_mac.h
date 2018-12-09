/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_notifications_manager.h"
#include "base/weak_ptr.h"

namespace Platform {
namespace Notifications {

bool SkipAudio();
bool SkipToast();

class Manager : public Window::Notifications::NativeManager, public base::has_weak_ptr {
public:
	Manager(Window::Notifications::System *system);
	~Manager();

protected:
	void doShowNativeNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, const QString &msg, bool hideNameAndPhoto, bool hideReplyButton) override;
	void doClearAllFast() override;
	void doClearFromHistory(History *history) override;

private:
	class Private;
	const std::unique_ptr<Private> _private;

};

} // namespace Notifications
} // namespace Platform
