/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <gsl/gsl>
#include <QtCore/QVector>
#include <QtCore/QString>
#include <QtCore/QByteArray>
#include <rpl/details/callable.h>
#include "base/basic_types.h"
#include "base/match_method.h"
#include "base/flags.h"
#include "base/bytes.h"
#include "base/algorithm.h"
#include "base/assertion.h"

using mtpPrime = int32;
using mtpRequestId = int32;
using mtpMsgId = uint64;
using mtpPingId = uint64;

using mtpBuffer = QVector<mtpPrime>;
using mtpTypeId = uint32;

namespace MTP {

// type DcId represents actual data center id, while in most cases
// we use some shifted ids, like DcId() + X * DCShift
using DcId = int32;
using ShiftedDcId = int32;

constexpr auto kDcShift = ShiftedDcId(10000);
constexpr auto kConfigDcShift = 0x01;
constexpr auto kLogoutDcShift = 0x02;
constexpr auto kUpdaterDcShift = 0x03;
constexpr auto kExportDcShift = 0x04;
constexpr auto kExportMediaDcShift = 0x05;
constexpr auto kMaxMediaDcCount = 0x10;
constexpr auto kBaseDownloadDcShift = 0x10;
constexpr auto kBaseUploadDcShift = 0x20;
constexpr auto kDestroyKeyStartDcShift = 0x100;

constexpr DcId BareDcId(ShiftedDcId shiftedDcId) {
	return (shiftedDcId % kDcShift);
}

constexpr ShiftedDcId ShiftDcId(DcId dcId, int value) {
	return dcId + kDcShift * value;
}

constexpr int GetDcIdShift(ShiftedDcId shiftedDcId) {
	return shiftedDcId / kDcShift;
}

} // namespace MTP

class Exception : public std::exception {
public:
	explicit Exception(const QString &msg) noexcept;

	const char *what() const noexcept override;

private:
	QByteArray _msg;

};

class mtpErrorUnexpected : public Exception {
public:
	mtpErrorUnexpected(mtpTypeId typeId, const QString &type) noexcept;

};

class mtpErrorInsufficient : public Exception {
public:
	mtpErrorInsufficient() noexcept;

};

class mtpErrorBadTypeId : public Exception {
public:
	mtpErrorBadTypeId(mtpTypeId typeId, const QString &type) noexcept;

};

namespace MTP {
namespace internal {

class TypeData {
public:
	TypeData() = default;
	TypeData(const TypeData &other) = delete;
	TypeData(TypeData &&other) = delete;
	TypeData &operator=(const TypeData &other) = delete;
	TypeData &operator=(TypeData &&other) = delete;

	virtual ~TypeData() {
	}

private:
	void incrementCounter() const {
		_counter.ref();
	}
	bool decrementCounter() const {
		return _counter.deref();
	}
	friend class TypeDataOwner;

	mutable QAtomicInt _counter = { 1 };

};

class TypeDataOwner {
public:
	TypeDataOwner(TypeDataOwner &&other) : _data(base::take(other._data)) {
	}
	TypeDataOwner(const TypeDataOwner &other) : _data(other._data) {
		incrementCounter();
	}
	TypeDataOwner &operator=(TypeDataOwner &&other) {
		if (other._data != _data) {
			decrementCounter();
			_data = base::take(other._data);
		}
		return *this;
	}
	TypeDataOwner &operator=(const TypeDataOwner &other) {
		if (other._data != _data) {
			setData(other._data);
			incrementCounter();
		}
		return *this;
	}
	~TypeDataOwner() {
		decrementCounter();
	}

protected:
	TypeDataOwner() = default;
	TypeDataOwner(const TypeData *data) : _data(data) {
	}

	void setData(const TypeData *data) {
		decrementCounter();
		_data = data;
	}

	// Unsafe cast, type should be checked by the caller.
	template <typename DataType>
	const DataType &queryData() const {
		Expects(_data != nullptr);

		return static_cast<const DataType &>(*_data);
	}

private:
	void incrementCounter() {
		if (_data) {
			_data->incrementCounter();
		}
	}
	void decrementCounter() {
		if (_data && !_data->decrementCounter()) {
			delete base::take(_data);
		}
	}

	const TypeData * _data = nullptr;

};

} // namespace internal
} // namespace MTP

enum {
	// core types
	mtpc_int = 0xa8509bda,
	mtpc_long = 0x22076cba,
	mtpc_int128 = 0x4bb5362b,
	mtpc_int256 = 0x929c32f,
	mtpc_double = 0x2210c154,
	mtpc_string = 0xb5286e24,

	mtpc_vector = 0x1cb5c415,

	// layers
	mtpc_invokeWithLayer1 = 0x53835315,
	mtpc_invokeWithLayer2 = 0x289dd1f6,
	mtpc_invokeWithLayer3 = 0xb7475268,
	mtpc_invokeWithLayer4 = 0xdea0d430,
	mtpc_invokeWithLayer5 = 0x417a57ae,
	mtpc_invokeWithLayer6 = 0x3a64d54d,
	mtpc_invokeWithLayer7 = 0xa5be56d3,
	mtpc_invokeWithLayer8 = 0xe9abd9fd,
	mtpc_invokeWithLayer9 = 0x76715a63,
	mtpc_invokeWithLayer10 = 0x39620c41,
	mtpc_invokeWithLayer11 = 0xa6b88fdf,
	mtpc_invokeWithLayer12 = 0xdda60d3c,
	mtpc_invokeWithLayer13 = 0x427c8ea2,
	mtpc_invokeWithLayer14 = 0x2b9b08fa,
	mtpc_invokeWithLayer15 = 0xb4418b64,
	mtpc_invokeWithLayer16 = 0xcf5f0987,
	mtpc_invokeWithLayer17 = 0x50858a19,
	mtpc_invokeWithLayer18 = 0x1c900537,

	// manually parsed
	mtpc_rpc_result = 0xf35c6d01,
	mtpc_msg_container = 0x73f1f8dc,
//	mtpc_msg_copy = 0xe06046b2,
	mtpc_gzip_packed = 0x3072cfa1
};
static const mtpTypeId mtpc_bytes = mtpc_string;
static const mtpTypeId mtpc_flags = mtpc_int;
static const mtpTypeId mtpc_core_message = -1; // undefined type, but is used
static const mtpTypeId mtpLayers[] = {
	mtpTypeId(mtpc_invokeWithLayer1),
	mtpTypeId(mtpc_invokeWithLayer2),
	mtpTypeId(mtpc_invokeWithLayer3),
	mtpTypeId(mtpc_invokeWithLayer4),
	mtpTypeId(mtpc_invokeWithLayer5),
	mtpTypeId(mtpc_invokeWithLayer6),
	mtpTypeId(mtpc_invokeWithLayer7),
	mtpTypeId(mtpc_invokeWithLayer8),
	mtpTypeId(mtpc_invokeWithLayer9),
	mtpTypeId(mtpc_invokeWithLayer10),
	mtpTypeId(mtpc_invokeWithLayer11),
	mtpTypeId(mtpc_invokeWithLayer12),
	mtpTypeId(mtpc_invokeWithLayer13),
	mtpTypeId(mtpc_invokeWithLayer14),
	mtpTypeId(mtpc_invokeWithLayer15),
	mtpTypeId(mtpc_invokeWithLayer16),
	mtpTypeId(mtpc_invokeWithLayer17),
	mtpTypeId(mtpc_invokeWithLayer18),
};
static const uint32 mtpLayerMaxSingle = sizeof(mtpLayers) / sizeof(mtpLayers[0]);

template <typename bareT>
class MTPBoxed : public bareT {
public:
	using bareT::bareT;
	MTPBoxed() = default;
	MTPBoxed(const MTPBoxed<bareT> &v) = default;
	MTPBoxed<bareT> &operator=(const MTPBoxed<bareT> &v) = default;
	MTPBoxed(const bareT &v) : bareT(v) {
	}
	MTPBoxed<bareT> &operator=(const bareT &v) {
		*((bareT*)this) = v;
		return *this;
	}

	uint32 innerLength() const {
		return sizeof(mtpTypeId) + bareT::innerLength();
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) {
		if (from + 1 > end) throw mtpErrorInsufficient();
		cons = (mtpTypeId)*(from++);
		bareT::read(from, end, cons);
	}
	void write(mtpBuffer &to) const {
        to.push_back(bareT::type());
		bareT::write(to);
	}

	using Unboxed = bareT;

};
template <typename T>
class MTPBoxed<MTPBoxed<T> > {
	typename T::CantMakeBoxedBoxedType v;
};

namespace MTP {
namespace details {

struct SecureRequestCreateTag {
};

} // namespace details

template <typename T>
struct is_boxed : std::false_type {
};

template <typename T>
struct is_boxed<MTPBoxed<T>> : std::true_type {
};

template <typename T>
constexpr bool is_boxed_v = is_boxed<T>::value;

class SecureRequestData;
class SecureRequest {
public:
	SecureRequest() = default;

	static constexpr auto kSaltInts = 2;
	static constexpr auto kSessionIdInts = 2;
	static constexpr auto kMessageIdInts = 2;
	static constexpr auto kSeqNoPosition = kSaltInts
		+ kSessionIdInts
		+ kMessageIdInts;
	static constexpr auto kSeqNoInts = 1;
	static constexpr auto kMessageLengthPosition = kSeqNoPosition
		+ kSeqNoInts;
	static constexpr auto kMessageLengthInts = 1;
	static constexpr auto kMessageBodyPosition = kMessageLengthPosition
		+ kMessageLengthInts;

	static SecureRequest Prepare(uint32 size, uint32 reserveSize = 0);

	template <
		typename Request,
		typename = std::enable_if_t<is_boxed_v<Request>>>
	static SecureRequest Serialize(const Request &request);

	// For template MTP requests and MTPBoxed instanciation.
	uint32 innerLength() const;
	void write(mtpBuffer &to) const;

	SecureRequestData *operator->() const;
	SecureRequestData &operator*() const;
	explicit operator bool() const;

	void addPadding(bool extended);
	uint32 messageSize() const;

	// "request-like" wrap for msgIds vector
	bool isSentContainer() const;
	bool isStateRequest() const;
	bool needAck() const;

	using ResponseType = void; // don't know real response type =(

private:
	explicit SecureRequest(const details::SecureRequestCreateTag &);

	std::shared_ptr<SecureRequestData> _data;

};

class SecureRequestData : public mtpBuffer {
public:
	explicit SecureRequestData(const details::SecureRequestCreateTag &) {
	}

	// in toSend: = 0 - must send in container, > 0 - can send without container
	// in haveSent: = 0 - container with msgIds, > 0 - when was sent
	int64 msDate = 0;

	mtpRequestId requestId = 0;
	SecureRequest after;
	bool needsLayer = false;

};

template <typename Request, typename>
SecureRequest SecureRequest::Serialize(const Request &request) {
	const auto requestSize = request.innerLength() >> 2;
	auto serialized = Prepare(requestSize);
	request.write(*serialized);
	return serialized;
}

} // namespace MTP

class MTPint {
public:
	int32 v = 0;

	MTPint() = default;

	uint32 innerLength() const {
		return sizeof(int32);
	}
	mtpTypeId type() const {
		return mtpc_int;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_int) {
		if (from + 1 > end) throw mtpErrorInsufficient();
		if (cons != mtpc_int) throw mtpErrorUnexpected(cons, "MTPint");
		v = (int32)*(from++);
	}
	void write(mtpBuffer &to) const {
		to.push_back((mtpPrime)v);
	}

private:
	explicit MTPint(int32 val) : v(val) {
	}

	friend MTPint MTP_int(int32 v);
};
inline MTPint MTP_int(int32 v) {
	return MTPint(v);
}
using MTPInt = MTPBoxed<MTPint>;

namespace internal {

struct ZeroFlagsHelper {
};

} // namespace internal

template <typename Flags>
class MTPflags {
public:
	Flags v = 0;
	static_assert(sizeof(Flags) == sizeof(int32), "MTPflags are allowed only wrapping int32 flag types!");

	MTPflags() = default;
	MTPflags(internal::ZeroFlagsHelper helper) {
	}

	uint32 innerLength() const {
		return sizeof(Flags);
	}
	mtpTypeId type() const {
		return mtpc_flags;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_flags) {
		if (from + 1 > end) throw mtpErrorInsufficient();
		if (cons != mtpc_flags) throw mtpErrorUnexpected(cons, "MTPflags");
		v = Flags::from_raw(static_cast<typename Flags::Type>(*(from++)));
	}
	void write(mtpBuffer &to) const {
		to.push_back(static_cast<mtpPrime>(v.value()));
	}

private:
	explicit MTPflags(Flags val) : v(val) {
	}

	template <typename T>
	friend MTPflags<base::flags<T>> MTP_flags(base::flags<T> v);

	template <typename T, typename>
	friend MTPflags<base::flags<T>> MTP_flags(T v);

};

template <typename T>
inline MTPflags<base::flags<T>> MTP_flags(base::flags<T> v) {
	return MTPflags<base::flags<T>>(v);
}

template <typename T, typename = std::enable_if_t<!std::is_same<T, int>::value>>
inline MTPflags<base::flags<T>> MTP_flags(T v) {
	return MTPflags<base::flags<T>>(v);
}

inline internal::ZeroFlagsHelper MTP_flags(void(internal::ZeroFlagsHelper::*)()) {
	return internal::ZeroFlagsHelper();
}

template <typename Flags>
using MTPFlags = MTPBoxed<MTPflags<Flags>>;

inline bool operator==(const MTPint &a, const MTPint &b) {
	return a.v == b.v;
}
inline bool operator!=(const MTPint &a, const MTPint &b) {
	return a.v != b.v;
}

class MTPlong {
public:
	uint64 v = 0;

	MTPlong() = default;

	uint32 innerLength() const {
		return sizeof(uint64);
	}
	mtpTypeId type() const {
		return mtpc_long;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_long) {
		if (from + 2 > end) throw mtpErrorInsufficient();
		if (cons != mtpc_long) throw mtpErrorUnexpected(cons, "MTPlong");
		v = (uint64)(((uint32*)from)[0]) | ((uint64)(((uint32*)from)[1]) << 32);
		from += 2;
	}
	void write(mtpBuffer &to) const {
		to.push_back((mtpPrime)(v & 0xFFFFFFFFL));
		to.push_back((mtpPrime)(v >> 32));
	}

private:
	explicit MTPlong(uint64 val) : v(val) {
	}

	friend MTPlong MTP_long(uint64 v);
};
inline MTPlong MTP_long(uint64 v) {
	return MTPlong(v);
}
using MTPLong = MTPBoxed<MTPlong>;

inline bool operator==(const MTPlong &a, const MTPlong &b) {
	return a.v == b.v;
}
inline bool operator!=(const MTPlong &a, const MTPlong &b) {
	return a.v != b.v;
}

class MTPint128 {
public:
	uint64 l = 0;
	uint64 h = 0;

	MTPint128() = default;

	uint32 innerLength() const {
		return sizeof(uint64) + sizeof(uint64);
	}
	mtpTypeId type() const {
		return mtpc_int128;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_int128) {
		if (from + 4 > end) throw mtpErrorInsufficient();
		if (cons != mtpc_int128) throw mtpErrorUnexpected(cons, "MTPint128");
		l = (uint64)(((uint32*)from)[0]) | ((uint64)(((uint32*)from)[1]) << 32);
		h = (uint64)(((uint32*)from)[2]) | ((uint64)(((uint32*)from)[3]) << 32);
		from += 4;
	}
	void write(mtpBuffer &to) const {
		to.push_back((mtpPrime)(l & 0xFFFFFFFFL));
		to.push_back((mtpPrime)(l >> 32));
		to.push_back((mtpPrime)(h & 0xFFFFFFFFL));
		to.push_back((mtpPrime)(h >> 32));
	}

private:
	explicit MTPint128(uint64 low, uint64 high) : l(low), h(high) {
	}

	friend MTPint128 MTP_int128(uint64 l, uint64 h);
};
inline MTPint128 MTP_int128(uint64 l, uint64 h) {
	return MTPint128(l, h);
}
using MTPInt128 = MTPBoxed<MTPint128>;

inline bool operator==(const MTPint128 &a, const MTPint128 &b) {
	return a.l == b.l && a.h == b.h;
}
inline bool operator!=(const MTPint128 &a, const MTPint128 &b) {
	return a.l != b.l || a.h != b.h;
}

class MTPint256 {
public:
	MTPint128 l;
	MTPint128 h;

	MTPint256() = default;

	uint32 innerLength() const {
		return l.innerLength() + h.innerLength();
	}
	mtpTypeId type() const {
		return mtpc_int256;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_int256) {
		if (cons != mtpc_int256) throw mtpErrorUnexpected(cons, "MTPint256");
		l.read(from, end);
		h.read(from, end);
	}
	void write(mtpBuffer &to) const {
		l.write(to);
		h.write(to);
	}

private:
	explicit MTPint256(MTPint128 low, MTPint128 high) : l(low), h(high) {
	}

	friend MTPint256 MTP_int256(const MTPint128 &l, const MTPint128 &h);
};
inline MTPint256 MTP_int256(const MTPint128 &l, const MTPint128 &h) {
	return MTPint256(l, h);
}
using MTPInt256 = MTPBoxed<MTPint256>;

inline bool operator==(const MTPint256 &a, const MTPint256 &b) {
	return a.l == b.l && a.h == b.h;
}
inline bool operator!=(const MTPint256 &a, const MTPint256 &b) {
	return a.l != b.l || a.h != b.h;
}

class MTPdouble {
public:
	float64 v = 0.;

	MTPdouble() = default;

	uint32 innerLength() const {
		return sizeof(float64);
	}
	mtpTypeId type() const {
		return mtpc_double;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_double) {
		if (from + 2 > end) throw mtpErrorInsufficient();
		if (cons != mtpc_double) throw mtpErrorUnexpected(cons, "MTPdouble");
		*(uint64*)(&v) = (uint64)(((uint32*)from)[0]) | ((uint64)(((uint32*)from)[1]) << 32);
		from += 2;
	}
	void write(mtpBuffer &to) const {
		uint64 iv = *(uint64*)(&v);
		to.push_back((mtpPrime)(iv & 0xFFFFFFFFL));
		to.push_back((mtpPrime)(iv >> 32));
	}

private:
	explicit MTPdouble(float64 val) : v(val) {
	}

	friend MTPdouble MTP_double(float64 v);
};
inline MTPdouble MTP_double(float64 v) {
	return MTPdouble(v);
}
using MTPDouble = MTPBoxed<MTPdouble>;

inline bool operator==(const MTPdouble &a, const MTPdouble &b) {
	return a.v == b.v;
}
inline bool operator!=(const MTPdouble &a, const MTPdouble &b) {
	return a.v != b.v;
}

class MTPstring;
using MTPbytes = MTPstring;

class MTPstring {
public:
	MTPstring() = default;

	uint32 innerLength() const;
	mtpTypeId type() const {
		return mtpc_string;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_string);
	void write(mtpBuffer &to) const;

	QByteArray v;

private:
	explicit MTPstring(QByteArray &&data) : v(std::move(data)) {
	}

	friend MTPstring MTP_string(const std::string &v);
	friend MTPstring MTP_string(const QString &v);
	friend MTPstring MTP_string(const char *v);

	friend MTPbytes MTP_bytes(const QByteArray &v);
	friend MTPbytes MTP_bytes(QByteArray &&v);

};
using MTPString = MTPBoxed<MTPstring>;
using MTPBytes = MTPBoxed<MTPbytes>;

inline MTPstring MTP_string(const std::string &v) {
	return MTPstring(QByteArray(v.data(), v.size()));
}
inline MTPstring MTP_string(const QString &v) {
	return MTPstring(v.toUtf8());
}
inline MTPstring MTP_string(const char *v) {
	return MTPstring(QByteArray(v, strlen(v)));
}
MTPstring MTP_string(const QByteArray &v) = delete;

inline MTPbytes MTP_bytes(const QByteArray &v) {
	return MTPbytes(QByteArray(v));
}
inline MTPbytes MTP_bytes(QByteArray &&v) {
	return MTPbytes(std::move(v));
}
inline MTPbytes MTP_bytes(bytes::const_span buffer) {
	return MTP_bytes(QByteArray(
		reinterpret_cast<const char*>(buffer.data()),
		buffer.size()));
}
inline MTPbytes MTP_bytes(const bytes::vector &buffer) {
	return MTP_bytes(bytes::make_span(buffer));
}

inline bool operator==(const MTPstring &a, const MTPstring &b) {
	return a.v == b.v;
}
inline bool operator!=(const MTPstring &a, const MTPstring &b) {
	return a.v != b.v;
}

inline QString qs(const MTPstring &v) {
	return QString::fromUtf8(v.v);
}

inline QByteArray qba(const MTPstring &v) {
	return v.v;
}

template <typename T>
class MTPvector {
public:
	MTPvector() = default;

	uint32 innerLength() const {
		auto result = uint32(sizeof(uint32));
		for (const auto &item : v) {
			result += item.innerLength();
		}
		return result;
	}
	mtpTypeId type() const {
		return mtpc_vector;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_vector) {
		if (from + 1 > end) throw mtpErrorInsufficient();
		if (cons != mtpc_vector) throw mtpErrorUnexpected(cons, "MTPvector");
		auto count = static_cast<uint32>(*(from++));

		auto vector = QVector<T>(count, T());
		for (auto &item : vector) {
			item.read(from, end);
		}
		v = std::move(vector);
	}
	void write(mtpBuffer &to) const {
		to.push_back(v.size());
		for (const auto &item : v) {
			item.write(to);
		}
	}

	QVector<T> v;

private:
	explicit MTPvector(QVector<T> &&data) : v(std::move(data)) {
	}

	template <typename U>
	friend MTPvector<U> MTP_vector(uint32 count);
	template <typename U>
	friend MTPvector<U> MTP_vector(uint32 count, const U &value);
	template <typename U>
	friend MTPvector<U> MTP_vector(const QVector<U> &v);
	template <typename U>
	friend MTPvector<U> MTP_vector(QVector<U> &&v);

};
template <typename T>
inline MTPvector<T> MTP_vector(uint32 count) {
	return MTPvector<T>(QVector<T>(count));
}
template <typename T>
inline MTPvector<T> MTP_vector(uint32 count, const T &value) {
	return MTPvector<T>(QVector<T>(count, value));
}
template <typename T>
inline MTPvector<T> MTP_vector(const QVector<T> &v) {
	return MTPvector<T>(QVector<T>(v));
}
template <typename T>
inline MTPvector<T> MTP_vector(QVector<T> &&v) {
	return MTPvector<T>(std::move(v));
}
template <typename T>
using MTPVector = MTPBoxed<MTPvector<T>>;

template <typename T>
inline bool operator==(const MTPvector<T> &a, const MTPvector<T> &b) {
	return a.c_vector().v == b.c_vector().v;
}
template <typename T>
inline bool operator!=(const MTPvector<T> &a, const MTPvector<T> &b) {
	return a.c_vector().v != b.c_vector().v;
}

// Human-readable text serialization

struct MTPStringLogger {
	static constexpr auto kBufferSize = 1024 * 1024; // 1 mb start size

	MTPStringLogger()
	: p(new char[kBufferSize])
	, alloced(kBufferSize) {
	}
	~MTPStringLogger() {
		delete[] p;
	}

	MTPStringLogger &add(const QString &data) {
		auto d = data.toUtf8();
		return add(d.constData(), d.size());
	}

	MTPStringLogger &add(const char *data, int32 len = -1) {
		if (len < 0) len = strlen(data);
		if (!len) return (*this);

		ensureLength(len);
		memcpy(p + size, data, len);
		size += len;
		return (*this);
	}

	MTPStringLogger &addSpaces(int32 level) {
		int32 len = level * 2;
		if (!len) return (*this);

		ensureLength(len);
		for (char *ptr = p + size, *end = ptr + len; ptr != end; ++ptr) {
			*ptr = ' ';
		}
		size += len;
		return (*this);
	}

	void ensureLength(int32 add) {
		if (size + add <= alloced) return;

		int32 newsize = size + add;
		if (newsize % kBufferSize) {
			newsize += kBufferSize - (newsize % kBufferSize);
		}
		char *b = new char[newsize];
		memcpy(b, p, size);
		alloced = newsize;
		delete[] p;
		p = b;
	}

	char *p = nullptr;
	int size = 0;
	int alloced = 0;

};

void mtpTextSerializeType(MTPStringLogger &to, const mtpPrime *&from, const mtpPrime *end, mtpPrime cons = 0, uint32 level = 0, mtpPrime vcons = 0);

void mtpTextSerializeCore(MTPStringLogger &to, const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons, uint32 level, mtpPrime vcons = 0);

inline QString mtpTextSerialize(const mtpPrime *&from, const mtpPrime *end) {
	MTPStringLogger to;
	try {
		mtpTextSerializeType(to, from, end, mtpc_core_message);
	} catch (Exception &e) {
		to.add("[ERROR] (").add(e.what()).add(")");
	}
	return QString::fromUtf8(to.p, to.size);
}
