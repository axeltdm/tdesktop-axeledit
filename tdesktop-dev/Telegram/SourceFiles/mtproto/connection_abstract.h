/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/dc_options.h"
#include "base/bytes.h"

namespace MTP {
namespace internal {

struct ConnectionOptions;

class AbstractConnection;

class ConnectionPointer {
public:
	ConnectionPointer();
	ConnectionPointer(std::nullptr_t);
	ConnectionPointer(ConnectionPointer &&other);
	ConnectionPointer &operator=(ConnectionPointer &&other);

	template <typename ConnectionType, typename ...Args>
	static ConnectionPointer New(Args &&...args) {
		return ConnectionPointer(new ConnectionType(
			std::forward<Args>(args)...
		));
	}

	AbstractConnection *get() const;
	void reset(AbstractConnection *value = nullptr);
	operator AbstractConnection*() const;
	AbstractConnection *operator->() const;
	AbstractConnection &operator*() const;
	explicit operator bool() const;

	~ConnectionPointer();

private:
	explicit ConnectionPointer(AbstractConnection *value);

	AbstractConnection *_value = nullptr;

};

class AbstractConnection : public QObject {
	Q_OBJECT

public:
	AbstractConnection(
		QThread *thread,
		const ProxyData &proxy);
	AbstractConnection(const AbstractConnection &other) = delete;
	AbstractConnection &operator=(const AbstractConnection &other) = delete;
	virtual ~AbstractConnection() = default;

	// virtual constructor
	static ConnectionPointer Create(
		not_null<Instance*> instance,
		DcOptions::Variants::Protocol protocol,
		QThread *thread,
		const ProxyData &proxy);

	virtual ConnectionPointer clone(const ProxyData &proxy) = 0;

	virtual TimeMs pingTime() const = 0;
	virtual TimeMs fullConnectTimeout() const = 0;
	virtual void sendData(mtpBuffer &&buffer) = 0;
	virtual void disconnectFromServer() = 0;
	virtual void connectToServer(
		const QString &ip,
		int port,
		const bytes::vector &protocolSecret,
		int16 protocolDcId) = 0;
	virtual bool isConnected() const = 0;
	virtual bool usingHttpWait() {
		return false;
	}
	virtual bool needHttpWait() {
		return false;
	}
	virtual bool requiresExtendedPadding() const {
		return false;
	}

	virtual int32 debugState() const = 0;

	virtual QString transport() const = 0;
	virtual QString tag() const = 0;

	void setSentEncrypted() {
		_sentEncrypted = true;
	}

	using BuffersQueue = std::deque<mtpBuffer>;
	BuffersQueue &received() {
		return _receivedQueue;
	}

	template <typename Request>
	mtpBuffer prepareNotSecurePacket(const Request &request) const;
	mtpBuffer prepareSecurePacket(
		uint64 keyId,
		MTPint128 msgKey,
		uint32 size) const;

	gsl::span<const mtpPrime> parseNotSecureResponse(
		const mtpBuffer &buffer) const;

	// Used to emit error(...) with no real code from the server.
	static constexpr auto kErrorCodeOther = -499;

signals:
	void receivedData();
	void receivedSome(); // to stop restart timer

	void error(qint32 errorCodebool);

	void connected();
	void disconnected();

protected:
	BuffersQueue _receivedQueue; // list of received packets, not processed yet
	bool _sentEncrypted = false;
	int _pingTime = 0;
	ProxyData _proxy;

	// first we always send fake MTPReq_pq to see if connection works at all
	// we send them simultaneously through TCP/HTTP/IPv4/IPv6 to choose the working one
	mtpBuffer preparePQFake(const MTPint128 &nonce) const;
	MTPResPQ readPQFakeReply(const mtpBuffer &buffer) const;

};

template <typename Request>
mtpBuffer AbstractConnection::prepareNotSecurePacket(const Request &request) const {
	const auto intsSize = request.innerLength() >> 2;
	const auto intsPadding = requiresExtendedPadding()
		? uint32(rand_value<uchar>() & 0x3F)
		: 0;

	auto result = mtpBuffer();
	constexpr auto kTcpPrefixInts = 2;
	constexpr auto kAuthKeyIdInts = 2;
	constexpr auto kMessageIdInts = 2;
	constexpr auto kMessageLengthInts = 1;
	constexpr auto kPrefixInts = kTcpPrefixInts
		+ kAuthKeyIdInts
		+ kMessageIdInts
		+ kMessageLengthInts;
	constexpr auto kTcpPostfixInts = 4;

	result.reserve(kPrefixInts + intsSize + intsPadding + kTcpPostfixInts);
	result.resize(kPrefixInts);

	const auto messageId = &result[kTcpPrefixInts + kAuthKeyIdInts];
	*reinterpret_cast<mtpMsgId*>(messageId) = msgid();

	request.write(result);

	const auto messageLength = messageId + kMessageIdInts;
	*messageLength = (result.size() - kPrefixInts + intsPadding) << 2;

	if (intsPadding > 0) {
		result.resize(result.size() + intsPadding);
		memset_rand(
			result.data() + result.size() - intsPadding,
			intsPadding * sizeof(mtpPrime));
	}

	return result;
}

} // namespace internal
} // namespace MTP
