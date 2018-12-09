/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/dcenter.h"

#include "mtproto/facade.h"
#include "mtproto/auth_key.h"
#include "mtproto/dc_options.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/special_config_request.h"
#include "storage/localstorage.h"

namespace MTP {
namespace internal {
namespace {

constexpr auto kEnumerateDcTimeout = 8000; // 8 seconds timeout for help_getConfig to work (then move to other dc)
constexpr auto kSpecialRequestTimeoutMs = 6000; // 4 seconds timeout for it to work in a specially requested dc.

} // namespace

Dcenter::Dcenter(not_null<Instance*> instance, DcId dcId, AuthKeyPtr &&key)
: _instance(instance)
, _id(dcId)
, _key(std::move(key)) {
	connect(this, SIGNAL(authKeyCreated()), this, SLOT(authKeyWrite()), Qt::QueuedConnection);
}

void Dcenter::authKeyWrite() {
	DEBUG_LOG(("AuthKey Info: MTProtoDC::authKeyWrite() slot, dc %1").arg(_id));
	if (_key) {
		Local::writeMtpData();
	}
}

void Dcenter::setKey(AuthKeyPtr &&key) {
	DEBUG_LOG(("AuthKey Info: MTProtoDC::setKey(%1), emitting authKeyCreated, dc %2").arg(key ? key->keyId() : 0).arg(_id));
	_key = std::move(key);
	_connectionInited = false;
	_instance->setKeyForWrite(_id, _key);
	emit authKeyCreated();
}

QReadWriteLock *Dcenter::keyMutex() const {
	return &keyLock;
}

const AuthKeyPtr &Dcenter::getKey() const {
	return _key;
}

void Dcenter::destroyKey() {
	setKey(AuthKeyPtr());
}

} // namespace internal
} // namespace MTP
