/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/connection_tcp.h"

#include "base/bytes.h"
#include "base/openssl_help.h"
#include "base/qthelp_url.h"

extern "C" {
#include <openssl/aes.h>
} // extern "C"

namespace MTP {
namespace internal {
namespace {

constexpr auto kPacketSizeMax = int(0x01000000 * sizeof(mtpPrime));
constexpr auto kFullConnectionTimeout = 8 * TimeMs(1000);
constexpr auto kSmallBufferSize = 256 * 1024;
constexpr auto kMinPacketBuffer = 256;

using ErrorSignal = void(QTcpSocket::*)(QAbstractSocket::SocketError);
const auto QTcpSocket_error = ErrorSignal(&QAbstractSocket::error);

} // namespace

class TcpConnection::Protocol {
public:
	static std::unique_ptr<Protocol> Create(bytes::vector &&secret);

	virtual uint32 id() const = 0;
	virtual bool supportsArbitraryLength() const = 0;

	virtual bool requiresExtendedPadding() const = 0;
	virtual void prepareKey(bytes::span key, bytes::const_span source) = 0;
	virtual bytes::span finalizePacket(mtpBuffer &buffer) = 0;

	static constexpr auto kUnknownSize = -1;
	static constexpr auto kInvalidSize = -2;
	virtual int readPacketLength(bytes::const_span bytes) const = 0;
	virtual bytes::const_span readPacket(bytes::const_span bytes) const = 0;

	virtual ~Protocol() = default;

private:
	class Version0;
	class Version1;
	class VersionD;

};

class TcpConnection::Protocol::Version0 : public Protocol {
public:
	uint32 id() const override;
	bool supportsArbitraryLength() const override;

	bool requiresExtendedPadding() const override;
	void prepareKey(bytes::span key, bytes::const_span source) override;
	bytes::span finalizePacket(mtpBuffer &buffer) override;

	int readPacketLength(bytes::const_span bytes) const override;
	bytes::const_span readPacket(bytes::const_span bytes) const override;

};

uint32 TcpConnection::Protocol::Version0::id() const {
	return 0xEFEFEFEFU;
}

bool TcpConnection::Protocol::Version0::supportsArbitraryLength() const {
	return false;
}

bool TcpConnection::Protocol::Version0::requiresExtendedPadding() const {
	return false;
}

void TcpConnection::Protocol::Version0::prepareKey(
		bytes::span key,
		bytes::const_span source) {
	bytes::copy(key, source);
}

bytes::span TcpConnection::Protocol::Version0::finalizePacket(
		mtpBuffer &buffer) {
	Expects(buffer.size() > 2 && buffer.size() < 0x1000003U);

	const auto intsSize = uint32(buffer.size() - 2);
	const auto bytesSize = intsSize * sizeof(mtpPrime);
	const auto data = reinterpret_cast<uchar*>(&buffer[0]);
	const auto added = [&] {
		if (intsSize < 0x7F) {
			data[7] = uchar(intsSize);
			return 1;
		}
		data[4] = uchar(0x7F);
		data[5] = uchar(intsSize & 0xFF);
		data[6] = uchar((intsSize >> 8) & 0xFF);
		data[7] = uchar((intsSize >> 16) & 0xFF);
		return 4;
	}();
	return bytes::make_span(buffer).subspan(8 - added, added + bytesSize);
}

int TcpConnection::Protocol::Version0::readPacketLength(
		bytes::const_span bytes) const {
	if (bytes.empty()) {
		return kUnknownSize;
	}
	const auto first = static_cast<char>(bytes[0]);
	if (first == 0x7F) {
		if (bytes.size() < 4) {
			return kUnknownSize;
		}
		const auto ints = static_cast<uint32>(bytes[1])
			| (static_cast<uint32>(bytes[2]) << 8)
			| (static_cast<uint32>(bytes[3]) << 16);
		return (ints >= 0x7F) ? (int(ints << 2) + 4) : kInvalidSize;
	} else if (first > 0 && first < 0x7F) {
		const auto ints = uint32(first);
		return int(ints << 2) + 1;
	}
	return kInvalidSize;
}

bytes::const_span TcpConnection::Protocol::Version0::readPacket(
		bytes::const_span bytes) const {
	const auto size = readPacketLength(bytes);
	Assert(size != kUnknownSize
		&& size != kInvalidSize
		&& size <= bytes.size());
	const auto sizeLength = (static_cast<char>(bytes[0]) == 0x7F) ? 4 : 1;
	return bytes.subspan(sizeLength, size - sizeLength);
}

class TcpConnection::Protocol::Version1 : public Version0 {
public:
	explicit Version1(bytes::vector &&secret);

	bool requiresExtendedPadding() const override;
	void prepareKey(bytes::span key, bytes::const_span source) override;

private:
	bytes::vector _secret;

};

TcpConnection::Protocol::Version1::Version1(bytes::vector &&secret)
: _secret(std::move(secret)) {
}

bool TcpConnection::Protocol::Version1::requiresExtendedPadding() const {
	return true;
}

void TcpConnection::Protocol::Version1::prepareKey(
		bytes::span key,
		bytes::const_span source) {
	const auto payload = bytes::concatenate(source, _secret);
	bytes::copy(key, openssl::Sha256(payload));
}

class TcpConnection::Protocol::VersionD : public Version1 {
public:
	using Version1::Version1;

	uint32 id() const override;
	bool supportsArbitraryLength() const override;

	bytes::span finalizePacket(mtpBuffer &buffer) override;

	int readPacketLength(bytes::const_span bytes) const override;
	bytes::const_span readPacket(bytes::const_span bytes) const override;

};

uint32 TcpConnection::Protocol::VersionD::id() const {
	return 0xDDDDDDDDU;
}

bool TcpConnection::Protocol::VersionD::supportsArbitraryLength() const {
	return true;
}

bytes::span TcpConnection::Protocol::VersionD::finalizePacket(
		mtpBuffer &buffer) {
	Expects(buffer.size() > 2 && buffer.size() < 0x1000003U);

	const auto intsSize = uint32(buffer.size() - 2);
	const auto padding = rand_value<uint32>() & 0x0F;
	const auto bytesSize = intsSize * sizeof(mtpPrime) + padding;
	buffer[1] = bytesSize;
	for (auto added = 0; added < padding; added += 4) {
		buffer.push_back(rand_value<mtpPrime>());
	}

	return bytes::make_span(buffer).subspan(4, 4 + bytesSize);
}

int TcpConnection::Protocol::VersionD::readPacketLength(
		bytes::const_span bytes) const {
	if (bytes.size() < 4) {
		return kUnknownSize;
	}
	const auto value = *reinterpret_cast<const uint32*>(bytes.data()) + 4;
	return (value >= 8 && value < kPacketSizeMax)
		? int(value)
		: kInvalidSize;
}

bytes::const_span TcpConnection::Protocol::VersionD::readPacket(
		bytes::const_span bytes) const {
	const auto size = readPacketLength(bytes);
	Assert(size != kUnknownSize
		&& size != kInvalidSize
		&& size <= bytes.size());
	const auto sizeLength = 4;
	return bytes.subspan(sizeLength, size - sizeLength);
}

auto TcpConnection::Protocol::Create(bytes::vector &&secret)
-> std::unique_ptr<Protocol> {
	if (secret.size() == 17 && static_cast<uchar>(secret[0]) == 0xDD) {
		return std::make_unique<VersionD>(
			bytes::make_vector(bytes::make_span(secret).subspan(1)));
	} else if (secret.size() == 16) {
		return std::make_unique<Version1>(std::move(secret));
	} else if (secret.empty()) {
		return std::make_unique<Version0>();
	}
	Unexpected("Secret bytes in TcpConnection::Protocol::Create.");
}

TcpConnection::TcpConnection(QThread *thread, const ProxyData &proxy)
: AbstractConnection(thread, proxy)
, _checkNonce(rand_value<MTPint128>()) {
	_socket.moveToThread(thread);
	_socket.setProxy(ToNetworkProxy(proxy));
	connect(
		&_socket,
		&QTcpSocket::connected,
		this,
		&TcpConnection::socketConnected);
	connect(
		&_socket,
		&QTcpSocket::disconnected,
		this,
		&TcpConnection::socketDisconnected);
	connect(
		&_socket,
		&QTcpSocket::readyRead,
		this,
		&TcpConnection::socketRead);
	connect(
		&_socket,
		QTcpSocket_error,
		this,
		&TcpConnection::socketError);
}

ConnectionPointer TcpConnection::clone(const ProxyData &proxy) {
	return ConnectionPointer::New<TcpConnection>(thread(), proxy);
}

void TcpConnection::ensureAvailableInBuffer(int amount) {
	auto &buffer = _usingLargeBuffer ? _largeBuffer : _smallBuffer;
	const auto full = bytes::make_span(buffer).subspan(
		_offsetBytes);
	if (full.size() >= amount) {
		return;
	}
	const auto read = full.subspan(0, _readBytes);
	if (amount <= _smallBuffer.size()) {
		if (_usingLargeBuffer) {
			bytes::copy(_smallBuffer, read);
			_usingLargeBuffer = false;
			_largeBuffer.clear();
		} else {
			bytes::move(_smallBuffer, read);
		}
	} else if (amount <= _largeBuffer.size()) {
		Assert(_usingLargeBuffer);
		bytes::move(_largeBuffer, read);
	} else {
		auto enough = bytes::vector(amount);
		bytes::copy(enough, read);
		_largeBuffer = std::move(enough);
		_usingLargeBuffer = true;
	}
	_offsetBytes = 0;
}

void TcpConnection::socketRead() {
	Expects(_leftBytes > 0 || !_usingLargeBuffer);

	if (_socket.state() != QAbstractSocket::ConnectedState) {
		LOG(("MTP error: "
			"socket not connected in socketRead(), state: %1"
			).arg(_socket.state()));
		emit error(kErrorCodeOther);
		return;
	}

	if (_smallBuffer.empty()) {
		_smallBuffer.resize(kSmallBufferSize);
	}
	do {
		const auto readLimit = (_leftBytes > 0)
			? _leftBytes
			: (kSmallBufferSize - _offsetBytes - _readBytes);
		Assert(readLimit > 0);

		auto &buffer = _usingLargeBuffer ? _largeBuffer : _smallBuffer;
		const auto full = bytes::make_span(buffer).subspan(_offsetBytes);
		const auto free = full.subspan(_readBytes);
		Assert(free.size() >= readLimit);

		const auto readCount = _socket.read(
			reinterpret_cast<char*>(free.data()),
			readLimit);
		if (readCount > 0) {
			const auto read = free.subspan(0, readCount);
			aesCtrEncrypt(read, _receiveKey, &_receiveState);
			TCP_LOG(("TCP Info: read %1 bytes").arg(readCount));

			_readBytes += readCount;
			if (_leftBytes > 0) {
				Assert(readCount <= _leftBytes);
				_leftBytes -= readCount;
				if (!_leftBytes) {
					socketPacket(full.subspan(0, _readBytes));
					_usingLargeBuffer = false;
					_largeBuffer.clear();
					_offsetBytes = _readBytes = 0;
				} else {
					TCP_LOG(("TCP Info: not enough %1 for packet! read %2"
						).arg(_leftBytes
						).arg(_readBytes));
					emit receivedSome();
				}
			} else {
				auto available = full.subspan(0, _readBytes);
				while (_readBytes > 0) {
					const auto packetSize = _protocol->readPacketLength(
						available);
					if (packetSize == Protocol::kUnknownSize) {
						// Not enough bytes yet.
						break;
					} else if (packetSize <= 0) {
						LOG(("TCP Error: bad packet size in 4 bytes: %1"
							).arg(packetSize));
						emit error(kErrorCodeOther);
						return;
					} else if (available.size() >= packetSize) {
						socketPacket(available.subspan(0, packetSize));
						available = available.subspan(packetSize);
						_offsetBytes += packetSize;
						_readBytes -= packetSize;

						// If we have too little space left in the buffer.
						ensureAvailableInBuffer(kMinPacketBuffer);
					} else {
						_leftBytes = packetSize - available.size();

						// If the next packet won't fit in the buffer.
						ensureAvailableInBuffer(packetSize);

						TCP_LOG(("TCP Info: not enough %1 for packet! "
							"full size %2 read %3"
							).arg(_leftBytes
							).arg(packetSize
							).arg(available.size()));
						emit receivedSome();
						break;
					}
				}
			}
		} else if (readCount < 0) {
			LOG(("TCP Error: socket read return %1").arg(readCount));
			emit error(kErrorCodeOther);
			return;
		} else {
			TCP_LOG(("TCP Info: no bytes read, but bytes available was true..."));
			break;
		}
	} while (_socket.state() == QAbstractSocket::ConnectedState && _socket.bytesAvailable());
}

mtpBuffer TcpConnection::parsePacket(bytes::const_span bytes) {
	const auto packet = _protocol->readPacket(bytes);
	TCP_LOG(("TCP Info: packet received, size = %1"
		).arg(packet.size()));
	const auto ints = gsl::make_span(
		reinterpret_cast<const mtpPrime*>(packet.data()),
		packet.size() / sizeof(mtpPrime));
	Assert(!ints.empty());
	if (ints.size() < 3) {
		// nop or error or new quickack, latter is not yet supported.
		if (ints[0] != 0) {
			LOG(("TCP Error: "
				"error packet received, endpoint: '%1:%2', "
				"protocolDcId: %3, code = %4"
				).arg(_address.isEmpty() ? ("prx_" + _proxy.host) : _address
				).arg(_address.isEmpty() ? _proxy.port : _port
				).arg(_protocolDcId
				).arg(ints[0]));
		}
		return mtpBuffer(1, ints[0]);
	}
	auto result = mtpBuffer(ints.size());
	memcpy(result.data(), ints.data(), ints.size() * sizeof(mtpPrime));
	return result;
}

void TcpConnection::handleError(QAbstractSocket::SocketError e, QTcpSocket &socket) {
	switch (e) {
	case QAbstractSocket::ConnectionRefusedError:
	LOG(("TCP Error: socket connection refused - %1").arg(socket.errorString()));
	break;

	case QAbstractSocket::RemoteHostClosedError:
	TCP_LOG(("TCP Info: remote host closed socket connection - %1").arg(socket.errorString()));
	break;

	case QAbstractSocket::HostNotFoundError:
	LOG(("TCP Error: host not found - %1").arg(socket.errorString()));
	break;

	case QAbstractSocket::SocketTimeoutError:
	LOG(("TCP Error: socket timeout - %1").arg(socket.errorString()));
	break;

	case QAbstractSocket::NetworkError:
	LOG(("TCP Error: network - %1").arg(socket.errorString()));
	break;

	case QAbstractSocket::ProxyAuthenticationRequiredError:
	case QAbstractSocket::ProxyConnectionRefusedError:
	case QAbstractSocket::ProxyConnectionClosedError:
	case QAbstractSocket::ProxyConnectionTimeoutError:
	case QAbstractSocket::ProxyNotFoundError:
	case QAbstractSocket::ProxyProtocolError:
	LOG(("TCP Error: proxy (%1) - %2").arg(e).arg(socket.errorString()));
	break;

	default:
	LOG(("TCP Error: other (%1) - %2").arg(e).arg(socket.errorString()));
	break;
	}

	TCP_LOG(("TCP Error %1, restarting! - %2").arg(e).arg(socket.errorString()));
}

void TcpConnection::socketConnected() {
	Expects(_status == Status::Waiting);

	auto buffer = preparePQFake(_checkNonce);

	DEBUG_LOG(("TCP Info: "
		"dc:%1 - Sending fake req_pq to '%2'"
		).arg(_protocolDcId
		).arg(_address + ':' + QString::number(_port)));

	_pingTime = getms();
	sendData(std::move(buffer));
}

void TcpConnection::socketDisconnected() {
	if (_status == Status::Waiting || _status == Status::Ready) {
		emit disconnected();
	}
}

bool TcpConnection::requiresExtendedPadding() const {
	Expects(_protocol != nullptr);

	return _protocol->requiresExtendedPadding();
}

void TcpConnection::sendData(mtpBuffer &&buffer) {
	Expects(buffer.size() > 2);

	if (_status != Status::Finished) {
		sendBuffer(std::move(buffer));
	}
}

void TcpConnection::writeConnectionStart() {
	Expects(_protocol != nullptr);

	// prepare random part
	auto nonceBytes = bytes::vector(64);
	const auto nonce = bytes::make_span(nonceBytes);

	const auto zero = reinterpret_cast<uchar*>(nonce.data());
	const auto first = reinterpret_cast<uint32*>(nonce.data());
	const auto second = first + 1;
	const auto reserved01 = 0x000000EFU;
	const auto reserved11 = 0x44414548U;
	const auto reserved12 = 0x54534F50U;
	const auto reserved13 = 0x20544547U;
	const auto reserved14 = 0xEEEEEEEEU;
	const auto reserved15 = 0xDDDDDDDDU;
	const auto reserved21 = 0x00000000U;
	do {
		bytes::set_random(nonce);
	} while (*zero == reserved01
		|| *first == reserved11
		|| *first == reserved12
		|| *first == reserved13
		|| *first == reserved14
		|| *first == reserved15
		|| *second == reserved21);

	// prepare encryption key/iv
	_protocol->prepareKey(
		bytes::make_span(_sendKey),
		nonce.subspan(8, CTRState::KeySize));
	bytes::copy(
		bytes::make_span(_sendState.ivec),
		nonce.subspan(8 + CTRState::KeySize, CTRState::IvecSize));

	// prepare decryption key/iv
	auto reversedBytes = bytes::vector(48);
	const auto reversed = bytes::make_span(reversedBytes);
	bytes::copy(reversed, nonce.subspan(8, reversed.size()));
	std::reverse(reversed.begin(), reversed.end());
	_protocol->prepareKey(
		bytes::make_span(_receiveKey),
		reversed.subspan(0, CTRState::KeySize));
	bytes::copy(
		bytes::make_span(_receiveState.ivec),
		reversed.subspan(CTRState::KeySize, CTRState::IvecSize));

	// write protocol and dc ids
	const auto protocol = reinterpret_cast<uint32*>(nonce.data() + 56);
	*protocol = _protocol->id();
	const auto dcId = reinterpret_cast<int16*>(nonce.data() + 60);
	*dcId = _protocolDcId;

	_socket.write(reinterpret_cast<const char*>(nonce.data()), 56);
	aesCtrEncrypt(nonce, _sendKey, &_sendState);
	_socket.write(reinterpret_cast<const char*>(nonce.subspan(56).data()), 8);
}

void TcpConnection::sendBuffer(mtpBuffer &&buffer) {
	if (!_connectionStarted) {
		writeConnectionStart();
		_connectionStarted = true;
	}

	// buffer: 2 available int-s + data + available int.
	const auto bytes = _protocol->finalizePacket(buffer);
	TCP_LOG(("TCP Info: write packet %1 bytes").arg(bytes.size()));
	aesCtrEncrypt(bytes, _sendKey, &_sendState);
	_socket.write(
		reinterpret_cast<const char*>(bytes.data()),
		bytes.size());
}


void TcpConnection::disconnectFromServer() {
	if (_status == Status::Finished) return;
	_status = Status::Finished;

	disconnect(&_socket, &QTcpSocket::connected, nullptr, nullptr);
	disconnect(&_socket, &QTcpSocket::disconnected, nullptr, nullptr);
	disconnect(&_socket, &QTcpSocket::readyRead, nullptr, nullptr);
	disconnect(&_socket, QTcpSocket_error, nullptr, nullptr);
	_socket.close();
}

void TcpConnection::connectToServer(
		const QString &address,
		int port,
		const bytes::vector &protocolSecret,
		int16 protocolDcId) {
	Expects(_address.isEmpty());
	Expects(_port == 0);
	Expects(_protocol == nullptr);
	Expects(_protocolDcId == 0);

	if (_proxy.type == ProxyData::Type::Mtproto) {
		_address = _proxy.host;
		_port = _proxy.port;
		_protocol = Protocol::Create(_proxy.secretFromMtprotoPassword());

		DEBUG_LOG(("TCP Info: "
			"dc:%1 - Connecting to proxy '%2'"
			).arg(protocolDcId
			).arg(_address + ':' + QString::number(_port)));
	} else {
		_address = address;
		_port = port;
		_protocol = Protocol::Create(base::duplicate(protocolSecret));

		DEBUG_LOG(("TCP Info: "
			"dc:%1 - Connecting to '%2'"
			).arg(protocolDcId
			).arg(_address + ':' + QString::number(_port)));
	}
	_protocolDcId = protocolDcId;

	_socket.connectToHost(_address, _port);
}

TimeMs TcpConnection::pingTime() const {
	return isConnected() ? _pingTime : TimeMs(0);
}

TimeMs TcpConnection::fullConnectTimeout() const {
	return kFullConnectionTimeout;
}

void TcpConnection::socketPacket(bytes::const_span bytes) {
	if (_status == Status::Finished) return;

	// old quickack?..
	const auto data = parsePacket(bytes);
	if (data.size() == 1) {
		if (data[0] != 0) {
			emit error(data[0]);
		} else {
			// nop
		}
	//} else if (data.size() == 2) {
		// new quickack?..
	} else if (_status == Status::Ready) {
		_receivedQueue.push_back(data);
		emit receivedData();
	} else if (_status == Status::Waiting) {
		try {
			const auto res_pq = readPQFakeReply(data);
			const auto &data = res_pq.c_resPQ();
			if (data.vnonce == _checkNonce) {
				DEBUG_LOG(("Connection Info: Valid pq response by TCP."));
				_status = Status::Ready;
				disconnect(
					&_socket,
					&QTcpSocket::connected,
					nullptr,
					nullptr);
				_pingTime = (getms() - _pingTime);
				emit connected();
			} else {
				DEBUG_LOG(("Connection Error: "
					"Wrong nonce received in TCP fake pq-responce"));
				emit error(kErrorCodeOther);
			}
		} catch (Exception &e) {
			DEBUG_LOG(("Connection Error: "
				"Exception in parsing TCP fake pq-responce, %1"
				).arg(e.what()));
			emit error(kErrorCodeOther);
		}
	}
}

bool TcpConnection::isConnected() const {
	return (_status == Status::Ready);
}

int32 TcpConnection::debugState() const {
	return _socket.state();
}

QString TcpConnection::transport() const {
	if (!isConnected()) {
		return QString();
	}
	auto result = qsl("TCP");
	if (qthelp::is_ipv6(_address)) {
		result += qsl("/IPv6");
	}
	return result;
}

QString TcpConnection::tag() const {
	auto result = qsl("TCP");
	if (qthelp::is_ipv6(_address)) {
		result += qsl("/IPv6");
	} else {
		result += qsl("/IPv4");
	}
	return result;
}

void TcpConnection::socketError(QAbstractSocket::SocketError e) {
	if (_status == Status::Finished) return;

	handleError(e, _socket);
	emit error(kErrorCodeOther);
}

TcpConnection::~TcpConnection() = default;

} // namespace internal
} // namespace MTP
