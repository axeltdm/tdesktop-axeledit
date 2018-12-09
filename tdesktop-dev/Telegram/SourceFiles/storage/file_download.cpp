/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/file_download.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "messenger.h"
#include "storage/localstorage.h"
#include "platform/platform_file_utilities.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "core/crash_reports.h"
#include "base/bytes.h"
#include "base/openssl_help.h"

namespace Storage {

Downloader::Downloader() {
}

void Downloader::clearPriorities() {
	++_priority;
}

void Downloader::requestedAmountIncrement(MTP::DcId dcId, int index, int amount) {
	Expects(index >= 0 && index < MTP::kDownloadSessionsCount);

	auto it = _requestedBytesAmount.find(dcId);
	if (it == _requestedBytesAmount.cend()) {
		it = _requestedBytesAmount.emplace(dcId, RequestedInDc { { 0 } }).first;
	}
	it->second[index] += amount;
	if (it->second[index]) {
		Messenger::Instance().killDownloadSessionsStop(dcId);
	} else {
		Messenger::Instance().killDownloadSessionsStart(dcId);
	}
}

int Downloader::chooseDcIndexForRequest(MTP::DcId dcId) const {
	auto result = 0;
	auto it = _requestedBytesAmount.find(dcId);
	if (it != _requestedBytesAmount.cend()) {
		for (auto i = 1; i != MTP::kDownloadSessionsCount; ++i) {
			if (it->second[i] < it->second[result]) {
				result = i;
			}
		}
	}
	return result;
}

Downloader::~Downloader() = default;

} // namespace Storage

namespace {

constexpr auto kDownloadPhotoPartSize = 64 * 1024; // 64kb for photo
constexpr auto kDownloadDocumentPartSize = 128 * 1024; // 128kb for document
constexpr auto kMaxFileQueries = 16; // max 16 file parts downloaded at the same time
constexpr auto kMaxWebFileQueries = 8; // max 8 http[s] files downloaded at the same time
constexpr auto kDownloadCdnPartSize = 128 * 1024; // 128kb for cdn requests

} // namespace

struct FileLoaderQueue {
	FileLoaderQueue(int queriesLimit) : queriesLimit(queriesLimit) {
	}
	int queriesCount = 0;
	int queriesLimit = 0;
	FileLoader *start = nullptr;
	FileLoader *end = nullptr;
};

namespace {

using LoaderQueues = QMap<int32, FileLoaderQueue>;
LoaderQueues queues;

FileLoaderQueue _webQueue(kMaxWebFileQueries);

QThread *_webLoadThread = nullptr;
WebLoadManager *_webLoadManager = nullptr;
WebLoadManager *webLoadManager() {
	return (_webLoadManager && _webLoadManager != FinishedWebLoadManager) ? _webLoadManager : nullptr;
}
WebLoadMainManager *_webLoadMainManager = nullptr;

} // namespace

FileLoader::FileLoader(
	const QString &toFile,
	int32 size,
	LocationType locationType,
	LoadToCacheSetting toCache,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: _downloader(&Auth().downloader())
, _autoLoading(autoLoading)
, _cacheTag(cacheTag)
, _filename(toFile)
, _file(_filename)
, _toCache(toCache)
, _fromCloud(fromCloud)
, _size(size)
, _locationType(locationType) {
	Expects(!_filename.isEmpty() || (_size <= Storage::kMaxFileInMemory));
}

void FileLoader::finishWithBytes(const QByteArray &data) {
	_data = data;
	_localStatus = LocalStatus::Loaded;
	if (!_filename.isEmpty() && _toCache == LoadToCacheAsWell) {
		if (!_fileIsOpen) _fileIsOpen = _file.open(QIODevice::WriteOnly);
		if (!_fileIsOpen) {
			cancel(true);
			return;
		}
		if (_file.write(_data) != qint64(_data.size())) {
			cancel(true);
			return;
		}
	}

	_finished = true;
	if (_fileIsOpen) {
		_file.close();
		_fileIsOpen = false;
		Platform::File::PostprocessDownloaded(
			QFileInfo(_file).absoluteFilePath());
	}
	_downloader->taskFinished().notify();
}

QByteArray FileLoader::imageFormat(const QSize &shrinkBox) const {
	if (_imageFormat.isEmpty() && _locationType == UnknownFileLocation) {
		readImage(shrinkBox);
	}
	return _imageFormat;
}

QImage FileLoader::imageData(const QSize &shrinkBox) const {
	if (_imageData.isNull() && _locationType == UnknownFileLocation) {
		readImage(shrinkBox);
	}
	return _imageData;
}

void FileLoader::readImage(const QSize &shrinkBox) const {
	auto format = QByteArray();
	auto image = App::readImage(_data, &format, false);
	if (!image.isNull()) {
		if (!shrinkBox.isEmpty() && (image.width() > shrinkBox.width() || image.height() > shrinkBox.height())) {
			_imageData = image.scaled(shrinkBox, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		} else {
			_imageData = std::move(image);
		}
		_imageFormat = format;
	}
}

float64 FileLoader::currentProgress() const {
	if (_finished) return 1.;
	if (!fullSize()) return 0.;
	return snap(float64(currentOffset()) / fullSize(), 0., 1.);
}

int32 FileLoader::fullSize() const {
	return _size;
}

bool FileLoader::setFileName(const QString &fileName) {
	if (_toCache != LoadToCacheAsWell || !_filename.isEmpty()) {
		return fileName.isEmpty() || (fileName == _filename);
	}
	_filename = fileName;
	_file.setFileName(_filename);
	return true;
}

void FileLoader::permitLoadFromCloud() {
	_fromCloud = LoadFromCloudOrLocal;
}

void FileLoader::notifyAboutProgress() {
	const auto queue = _queue;
	emit progress(this);
	LoadNextFromQueue(queue);
}

void FileLoader::LoadNextFromQueue(not_null<FileLoaderQueue*> queue) {
	if (queue->queriesCount >= queue->queriesLimit) {
		return;
	}
	for (auto i = queue->start; i;) {
		if (i->loadPart()) {
			if (queue->queriesCount >= queue->queriesLimit) {
				return;
			}
		} else {
			i = i->_next;
		}
	}
}

void FileLoader::removeFromQueue() {
	if (!_inQueue) return;
	if (_next) {
		_next->_prev = _prev;
	}
	if (_prev) {
		_prev->_next = _next;
	}
	if (_queue->end == this) {
		_queue->end = _prev;
	}
	if (_queue->start == this) {
		_queue->start = _next;
	}
	_next = _prev = 0;
	_inQueue = false;
}

void FileLoader::pause() {
	removeFromQueue();
	_paused = true;
}

FileLoader::~FileLoader() {
	removeFromQueue();
}

void FileLoader::localLoaded(
		const StorageImageSaved &result,
		const QByteArray &imageFormat,
		const QImage &imageData) {
	_localLoading.kill();
	if (result.data.isEmpty()) {
		_localStatus = LocalStatus::NotFound;
		start(true);
		return;
	}
	if (!imageData.isNull()) {
		_imageFormat = imageFormat;
		_imageData = imageData;
	}
	finishWithBytes(result.data);
	notifyAboutProgress();
}

void FileLoader::start(bool loadFirst, bool prior) {
	if (_paused) {
		_paused = false;
	}
	if (_finished || tryLoadLocal()) {
		return;
	} else if (_fromCloud == LoadFromLocalOnly) {
		cancel();
		return;
	}

	if (!_filename.isEmpty() && _toCache == LoadToFileOnly && !_fileIsOpen) {
		_fileIsOpen = _file.open(QIODevice::WriteOnly);
		if (!_fileIsOpen) {
			return cancel(true);
		}
	}

	auto currentPriority = _downloader->currentPriority();
	FileLoader *before = 0, *after = 0;
	if (prior) {
		if (_inQueue && _priority == currentPriority) {
			if (loadFirst) {
				if (!_prev) return startLoading(loadFirst, prior);
				before = _queue->start;
			} else {
				if (!_next || _next->_priority < currentPriority) return startLoading(loadFirst, prior);
				after = _next;
				while (after->_next && after->_next->_priority == currentPriority) {
					after = after->_next;
				}
			}
		} else {
			_priority = currentPriority;
			if (loadFirst) {
				if (_inQueue && !_prev) return startLoading(loadFirst, prior);
				before = _queue->start;
			} else {
				if (_inQueue) {
					if (_next && _next->_priority == currentPriority) {
						after = _next;
					} else if (_prev && _prev->_priority < currentPriority) {
						before = _prev;
						while (before->_prev && before->_prev->_priority < currentPriority) {
							before = before->_prev;
						}
					} else {
						return startLoading(loadFirst, prior);
					}
				} else {
					if (_queue->start && _queue->start->_priority == currentPriority) {
						after = _queue->start;
					} else {
						before = _queue->start;
					}
				}
				if (after) {
					while (after->_next && after->_next->_priority == currentPriority) {
						after = after->_next;
					}
				}
			}
		}
	} else {
		if (loadFirst) {
			if (_inQueue && (!_prev || _prev->_priority == currentPriority)) return startLoading(loadFirst, prior);
			before = _prev;
			while (before->_prev && before->_prev->_priority != currentPriority) {
				before = before->_prev;
			}
		} else {
			if (_inQueue && !_next) return startLoading(loadFirst, prior);
			after = _queue->end;
		}
	}

	removeFromQueue();

	_inQueue = true;
	if (!_queue->start) {
		_queue->start = _queue->end = this;
	} else if (before) {
		if (before != _next) {
			_prev = before->_prev;
			_next = before;
			_next->_prev = this;
			if (_prev) {
				_prev->_next = this;
			}
			if (_queue->start->_prev) _queue->start = _queue->start->_prev;
		}
	} else if (after) {
		if (after != _prev) {
			_next = after->_next;
			_prev = after;
			after->_next = this;
			if (_next) {
				_next->_prev = this;
			}
			if (_queue->end->_next) _queue->end = _queue->end->_next;
		}
	} else {
		LOG(("Queue Error: _start && !before && !after"));
	}
	return startLoading(loadFirst, prior);
}

void FileLoader::loadLocal(const Storage::Cache::Key &key) {
	const auto readImage = (_locationType != AudioFileLocation);
	auto [first, second] = base::make_binary_guard();
	_localLoading = std::move(first);
	auto done = [=, guard = std::move(second)](
			QByteArray &&value,
			QImage &&image,
			QByteArray &&format) mutable {
		crl::on_main([
			=,
			value = std::move(value),
			image = std::move(image),
			format = std::move(format),
			guard = std::move(guard)
		]() mutable {
			if (!guard.alive()) {
				return;
			}
			localLoaded(
				StorageImageSaved(std::move(value)),
				format,
				std::move(image));
		});
	};
	Auth().data().cache().get(key, [=, callback = std::move(done)](
			QByteArray &&value) mutable {
		if (readImage) {
			crl::async([
				value = std::move(value),
				done = std::move(callback)
			]() mutable {
				auto format = QByteArray();
				auto image = App::readImage(value, &format, false);
				if (!image.isNull()) {
					done(
						std::move(value),
						std::move(image),
						std::move(format));
				} else {
					done(std::move(value), {}, {});
				}
			});
		} else {
			callback(std::move(value), {}, {});
		}
	});
}

bool FileLoader::tryLoadLocal() {
	if (_localStatus == LocalStatus::NotFound
		|| _localStatus == LocalStatus::Loaded) {
		return false;
	} else if (_localStatus == LocalStatus::Loading) {
		return true;
	}

	const auto weak = make_weak(this);
	if (const auto key = cacheKey()) {
		loadLocal(*key);
		emit progress(this);
	}
	if (!weak) {
		return false;
	} else if (_localStatus != LocalStatus::NotTried) {
		return _finished;
	} else if (_localLoading.alive()) {
		_localStatus = LocalStatus::Loading;
		return true;
	}
	_localStatus = LocalStatus::NotFound;
	return false;
}

void FileLoader::cancel() {
	cancel(false);
}

void FileLoader::cancel(bool fail) {
	bool started = currentOffset(true) > 0;
	cancelRequests();
	_cancelled = true;
	_finished = true;
	if (_fileIsOpen) {
		_file.close();
		_fileIsOpen = false;
		_file.remove();
	}
	_data = QByteArray();
	removeFromQueue();

	const auto queue = _queue;
	const auto weak = make_weak(this);
	if (fail) {
		emit failed(this, started);
	} else {
		emit progress(this);
	}
	if (weak) {
		_filename = QString();
		_file.setFileName(_filename);
	}
	LoadNextFromQueue(queue);
}

void FileLoader::startLoading(bool loadFirst, bool prior) {
	if ((_queue->queriesCount >= _queue->queriesLimit && (!loadFirst || !prior)) || _finished) {
		return;
	}
	loadPart();
}

mtpFileLoader::mtpFileLoader(
	not_null<StorageImageLocation*> location,
	Data::FileOrigin origin,
	int32 size,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: FileLoader(
	QString(),
	size,
	UnknownFileLocation,
	LoadToCacheAsWell,
	fromCloud,
	autoLoading,
	cacheTag)
, _dcId(location->dc())
, _location(location)
, _origin(origin) {
	auto shiftedDcId = MTP::downloadDcId(_dcId, 0);
	auto i = queues.find(shiftedDcId);
	if (i == queues.cend()) {
		i = queues.insert(shiftedDcId, FileLoaderQueue(kMaxFileQueries));
	}
	_queue = &i.value();
}

mtpFileLoader::mtpFileLoader(
	int32 dc,
	uint64 id,
	uint64 accessHash,
	const QByteArray &fileReference,
	Data::FileOrigin origin,
	LocationType type,
	const QString &to,
	int32 size,
	LoadToCacheSetting toCache,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: FileLoader(
	to,
	size,
	type,
	toCache,
	fromCloud,
	autoLoading,
	cacheTag)
, _dcId(dc)
, _id(id)
, _accessHash(accessHash)
, _fileReference(fileReference)
, _origin(origin) {
	auto shiftedDcId = MTP::downloadDcId(_dcId, 0);
	auto i = queues.find(shiftedDcId);
	if (i == queues.cend()) {
		i = queues.insert(shiftedDcId, FileLoaderQueue(kMaxFileQueries));
	}
	_queue = &i.value();
}

mtpFileLoader::mtpFileLoader(
	const WebFileLocation *location,
	int32 size,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: FileLoader(
	QString(),
	size,
	UnknownFileLocation,
	LoadToCacheAsWell,
	fromCloud,
	autoLoading,
	cacheTag)
, _dcId(location->dc())
, _urlLocation(location) {
	auto shiftedDcId = MTP::downloadDcId(_dcId, 0);
	auto i = queues.find(shiftedDcId);
	if (i == queues.cend()) {
		i = queues.insert(shiftedDcId, FileLoaderQueue(kMaxFileQueries));
	}
	_queue = &i.value();
}

mtpFileLoader::mtpFileLoader(
	const GeoPointLocation *location,
	int32 size,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: FileLoader(
	QString(),
	size,
	UnknownFileLocation,
	LoadToCacheAsWell,
	fromCloud,
	autoLoading,
	cacheTag)
, _dcId(Global::WebFileDcId())
, _geoLocation(location) {
	auto shiftedDcId = MTP::downloadDcId(_dcId, 0);
	auto i = queues.find(shiftedDcId);
	if (i == queues.cend()) {
		i = queues.insert(shiftedDcId, FileLoaderQueue(kMaxFileQueries));
	}
	_queue = &i.value();
}

int32 mtpFileLoader::currentOffset(bool includeSkipped) const {
	return (_fileIsOpen ? _file.size() : _data.size()) - (includeSkipped ? 0 : _skippedBytes);
}

Data::FileOrigin mtpFileLoader::fileOrigin() const {
	return _origin;
}

void mtpFileLoader::refreshFileReferenceFrom(
		const Data::UpdatedFileReferences &data,
		int requestId,
		const QByteArray &current) {
	const auto updated = [&] {
		if (_location) {
			const auto i = data.find(Data::SimpleFileLocationId(
				_location->volume(),
				_location->dc(),
				_location->local()));
			return (i == end(data)) ? QByteArray() : i->second;
		}
		const auto i = data.find(_id);
		return (i == end(data)) ? QByteArray() : i->second;
	}();
	if (updated.isEmpty() || updated == current) {
		cancel(true);
		return;
	}
	if (_location) {
		_location->refreshFileReference(updated);
	} else {
		_fileReference = updated;
	}
	const auto offset = finishSentRequestGetOffset(requestId);
	makeRequest(offset);
}

bool mtpFileLoader::loadPart() {
	if (_finished || _lastComplete || (!_sentRequests.empty() && !_size)) {
		return false;
	} else if (_size && _nextRequestOffset >= _size) {
		return false;
	}

	makeRequest(_nextRequestOffset);
	_nextRequestOffset += partSize();
	return true;
}

int mtpFileLoader::partSize() const {
	return kDownloadCdnPartSize;

	// Different part sizes are not supported for now :(
	// Because we start downloading with some part size
	// and then we get a cdn-redirect where we support only
	// fixed part size download for hash checking.
	//
	//if (_cdnDcId) {
	//	return kDownloadCdnPartSize;
	//} else if (_locationType == UnknownFileLocation) {
	//	return kDownloadPhotoPartSize;
	//}
	//return kDownloadDocumentPartSize;
}

mtpFileLoader::RequestData mtpFileLoader::prepareRequest(int offset) const {
	auto result = RequestData();
	result.dcId = _cdnDcId ? _cdnDcId : _dcId;
	result.dcIndex = _size ? _downloader->chooseDcIndexForRequest(result.dcId) : 0;
	result.offset = offset;
	return result;
}

void mtpFileLoader::makeRequest(int offset) {
	Expects(!_finished);

	auto requestData = prepareRequest(offset);
	auto send = [this, &requestData] {
		auto offset = requestData.offset;
		auto limit = partSize();
		auto shiftedDcId = MTP::downloadDcId(requestData.dcId, requestData.dcIndex);
		if (_cdnDcId) {
			Assert(requestData.dcId == _cdnDcId);
			return MTP::send(
				MTPupload_GetCdnFile(
					MTP_bytes(_cdnToken),
					MTP_int(offset),
					MTP_int(limit)),
				rpcDone(&mtpFileLoader::cdnPartLoaded),
				rpcFail(&mtpFileLoader::cdnPartFailed),
				shiftedDcId,
				50);
		} else if (_urlLocation) {
			Assert(requestData.dcId == _dcId);
			return MTP::send(
				MTPupload_GetWebFile(
					MTP_inputWebFileLocation(
						MTP_bytes(_urlLocation->url()),
						MTP_long(_urlLocation->accessHash())),
					MTP_int(offset),
					MTP_int(limit)),
				rpcDone(&mtpFileLoader::webPartLoaded),
				rpcFail(&mtpFileLoader::partFailed),
				shiftedDcId,
				50);
		} else if (_geoLocation) {
			Assert(requestData.dcId == _dcId);
			return MTP::send(
				MTPupload_GetWebFile(
					MTP_inputWebFileGeoPointLocation(
						MTP_inputGeoPoint(
							MTP_double(_geoLocation->lat),
							MTP_double(_geoLocation->lon)),
						MTP_long(_geoLocation->access),
						MTP_int(_geoLocation->width),
						MTP_int(_geoLocation->height),
						MTP_int(_geoLocation->zoom),
						MTP_int(_geoLocation->scale)),
					MTP_int(offset),
					MTP_int(limit)),
				rpcDone(&mtpFileLoader::webPartLoaded),
				rpcFail(&mtpFileLoader::partFailed),
				shiftedDcId,
				50);
		} else {
			Assert(requestData.dcId == _dcId);
			return MTP::send(
				MTPupload_GetFile(
					computeLocation(),
					MTP_int(offset),
					MTP_int(limit)),
				rpcDone(&mtpFileLoader::normalPartLoaded),
				rpcFail(&mtpFileLoader::partFailed),
				shiftedDcId,
				50);
		}
	};
	placeSentRequest(send(), requestData);
}

MTPInputFileLocation mtpFileLoader::computeLocation() const {
	if (_location) {
		return MTP_inputFileLocation(
			MTP_long(_location->volume()),
			MTP_int(_location->local()),
			MTP_long(_location->secret()),
			MTP_bytes(_location->fileReference()));
	} else if (_locationType == SecureFileLocation) {
		return MTP_inputSecureFileLocation(
			MTP_long(_id),
			MTP_long(_accessHash));
	}
	return MTP_inputDocumentFileLocation(
		MTP_long(_id),
		MTP_long(_accessHash),
		MTP_bytes(_fileReference));
}

void mtpFileLoader::requestMoreCdnFileHashes() {
	Expects(!_finished);

	if (_cdnHashesRequestId || _cdnUncheckedParts.empty()) {
		return;
	}

	auto offset = _cdnUncheckedParts.cbegin()->first;
	auto requestData = RequestData();
	requestData.dcId = _dcId;
	requestData.dcIndex = 0;
	requestData.offset = offset;
	auto shiftedDcId = MTP::downloadDcId(requestData.dcId, requestData.dcIndex);
	auto requestId = _cdnHashesRequestId = MTP::send(
		MTPupload_GetCdnFileHashes(
			MTP_bytes(_cdnToken),
			MTP_int(offset)),
		rpcDone(&mtpFileLoader::getCdnFileHashesDone),
		rpcFail(&mtpFileLoader::cdnPartFailed),
		shiftedDcId);
	placeSentRequest(requestId, requestData);
}

void mtpFileLoader::normalPartLoaded(
		const MTPupload_File &result,
		mtpRequestId requestId) {
	Expects(!_finished);
	Expects(result.type() == mtpc_upload_fileCdnRedirect || result.type() == mtpc_upload_file);

	auto offset = finishSentRequestGetOffset(requestId);
	if (result.type() == mtpc_upload_fileCdnRedirect) {
		return switchToCDN(offset, result.c_upload_fileCdnRedirect());
	}
	auto buffer = bytes::make_span(result.c_upload_file().vbytes.v);
	return partLoaded(offset, buffer);
}

void mtpFileLoader::webPartLoaded(
		const MTPupload_WebFile &result,
		mtpRequestId requestId) {
	Expects(result.type() == mtpc_upload_webFile);

	auto offset = finishSentRequestGetOffset(requestId);
	auto &webFile = result.c_upload_webFile();
	if (!_size) {
		_size = webFile.vsize.v;
	} else if (webFile.vsize.v != _size) {
		LOG(("MTP Error: Bad size provided by bot for webDocument: %1, real: %2").arg(_size).arg(webFile.vsize.v));
		return cancel(true);
	}
	auto buffer = bytes::make_span(webFile.vbytes.v);
	return partLoaded(offset, buffer);
}

void mtpFileLoader::cdnPartLoaded(const MTPupload_CdnFile &result, mtpRequestId requestId) {
	Expects(!_finished);

	auto offset = finishSentRequestGetOffset(requestId);
	if (result.type() == mtpc_upload_cdnFileReuploadNeeded) {
		auto requestData = RequestData();
		requestData.dcId = _dcId;
		requestData.dcIndex = 0;
		requestData.offset = offset;
		auto shiftedDcId = MTP::downloadDcId(requestData.dcId, requestData.dcIndex);
		auto requestId = MTP::send(MTPupload_ReuploadCdnFile(MTP_bytes(_cdnToken), result.c_upload_cdnFileReuploadNeeded().vrequest_token), rpcDone(&mtpFileLoader::reuploadDone), rpcFail(&mtpFileLoader::cdnPartFailed), shiftedDcId);
		placeSentRequest(requestId, requestData);
		return;
	}
	Expects(result.type() == mtpc_upload_cdnFile);

	auto key = bytes::make_span(_cdnEncryptionKey);
	auto iv = bytes::make_span(_cdnEncryptionIV);
	Expects(key.size() == MTP::CTRState::KeySize);
	Expects(iv.size() == MTP::CTRState::IvecSize);

	auto state = MTP::CTRState();
	auto ivec = bytes::make_span(state.ivec);
	std::copy(iv.begin(), iv.end(), ivec.begin());

	auto counterOffset = static_cast<uint32>(offset) >> 4;
	state.ivec[15] = static_cast<uchar>(counterOffset & 0xFF);
	state.ivec[14] = static_cast<uchar>((counterOffset >> 8) & 0xFF);
	state.ivec[13] = static_cast<uchar>((counterOffset >> 16) & 0xFF);
	state.ivec[12] = static_cast<uchar>((counterOffset >> 24) & 0xFF);

	auto decryptInPlace = result.c_upload_cdnFile().vbytes.v;
	auto buffer = bytes::make_detached_span(decryptInPlace);
	MTP::aesCtrEncrypt(buffer, key.data(), &state);

	switch (checkCdnFileHash(offset, buffer)) {
	case CheckCdnHashResult::NoHash: {
		_cdnUncheckedParts.emplace(offset, decryptInPlace);
		requestMoreCdnFileHashes();
	} return;

	case CheckCdnHashResult::Invalid: {
		LOG(("API Error: Wrong cdnFileHash for offset %1.").arg(offset));
		cancel(true);
	} return;

	case CheckCdnHashResult::Good: return partLoaded(offset, buffer);
	}
	Unexpected("Result of checkCdnFileHash()");
}

mtpFileLoader::CheckCdnHashResult mtpFileLoader::checkCdnFileHash(int offset, bytes::const_span buffer) {
	auto cdnFileHashIt = _cdnFileHashes.find(offset);
	if (cdnFileHashIt == _cdnFileHashes.cend()) {
		return CheckCdnHashResult::NoHash;
	}
	auto realHash = openssl::Sha256(buffer);
	if (bytes::compare(realHash, bytes::make_span(cdnFileHashIt->second.hash))) {
		return CheckCdnHashResult::Invalid;
	}
	return CheckCdnHashResult::Good;
}

void mtpFileLoader::reuploadDone(const MTPVector<MTPFileHash> &result, mtpRequestId requestId) {
	auto offset = finishSentRequestGetOffset(requestId);
	addCdnHashes(result.v);
	makeRequest(offset);
}

void mtpFileLoader::getCdnFileHashesDone(const MTPVector<MTPFileHash> &result, mtpRequestId requestId) {
	Expects(!_finished);
	Expects(_cdnHashesRequestId == requestId);

	_cdnHashesRequestId = 0;

	const auto offset = finishSentRequestGetOffset(requestId);
	addCdnHashes(result.v);
	auto someMoreChecked = false;
	for (auto i = _cdnUncheckedParts.begin(); i != _cdnUncheckedParts.cend();) {
		const auto uncheckedOffset = i->first;
		const auto uncheckedBytes = bytes::make_span(i->second);

		switch (checkCdnFileHash(uncheckedOffset, uncheckedBytes)) {
		case CheckCdnHashResult::NoHash: {
			++i;
		} break;

		case CheckCdnHashResult::Invalid: {
			LOG(("API Error: Wrong cdnFileHash for offset %1.").arg(offset));
			cancel(true);
			return;
		} break;

		case CheckCdnHashResult::Good: {
			someMoreChecked = true;
			const auto goodOffset = uncheckedOffset;
			const auto goodBytes = std::move(i->second);
			const auto weak = QPointer<mtpFileLoader>(this);
			i = _cdnUncheckedParts.erase(i);
			if (!feedPart(goodOffset, bytes::make_span(goodBytes))
				|| !weak) {
				return;
			} else if (_finished) {
				notifyAboutProgress();
				return;
			}
		} break;

		default: Unexpected("Result of checkCdnFileHash()");
		}
	}
	if (someMoreChecked) {
		const auto weak = make_weak(this);
		notifyAboutProgress();
		if (weak) {
			requestMoreCdnFileHashes();
		}
		return;
	}
	LOG(("API Error: Could not find cdnFileHash for offset %1 after getCdnFileHashes request.").arg(offset));
	cancel(true);
}

void mtpFileLoader::placeSentRequest(mtpRequestId requestId, const RequestData &requestData) {
	Expects(!_finished);

	_downloader->requestedAmountIncrement(requestData.dcId, requestData.dcIndex, partSize());
	++_queue->queriesCount;
	_sentRequests.emplace(requestId, requestData);
}

int mtpFileLoader::finishSentRequestGetOffset(mtpRequestId requestId) {
	auto it = _sentRequests.find(requestId);
	Assert(it != _sentRequests.cend());

	auto requestData = it->second;
	_downloader->requestedAmountIncrement(requestData.dcId, requestData.dcIndex, -partSize());

	--_queue->queriesCount;
	_sentRequests.erase(it);

	return requestData.offset;
}

bool mtpFileLoader::feedPart(int offset, bytes::const_span buffer) {
	Expects(!_finished);

	if (buffer.size()) {
		if (_fileIsOpen) {
			auto fsize = _file.size();
			if (offset < fsize) {
				_skippedBytes -= buffer.size();
			} else if (offset > fsize) {
				_skippedBytes += offset - fsize;
			}
			_file.seek(offset);
			if (_file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size()) != qint64(buffer.size())) {
				cancel(true);
				return false;
			}
		} else {
			_data.reserve(offset + buffer.size());
			if (offset > _data.size()) {
				_skippedBytes += offset - _data.size();
				_data.resize(offset);
			}
			if (offset == _data.size()) {
				_data.append(reinterpret_cast<const char*>(buffer.data()), buffer.size());
			} else {
				_skippedBytes -= buffer.size();
				if (int64(offset + buffer.size()) > _data.size()) {
					_data.resize(offset + buffer.size());
				}
				const auto dst = bytes::make_detached_span(_data).subspan(
					offset,
					buffer.size());
				bytes::copy(dst, buffer);
			}
		}
	}
	if (!buffer.size() || (buffer.size() % 1024)) { // bad next offset
		_lastComplete = true;
	}
	if (_sentRequests.empty()
		&& _cdnUncheckedParts.empty()
		&& (_lastComplete || (_size && _nextRequestOffset >= _size))) {
		if (!_filename.isEmpty() && (_toCache == LoadToCacheAsWell)) {
			if (!_fileIsOpen) {
				_fileIsOpen = _file.open(QIODevice::WriteOnly);
			}
			if (!_fileIsOpen || _file.write(_data) != qint64(_data.size())) {
				cancel(true);
				return false;
			}
		}
		_finished = true;
		if (_fileIsOpen) {
			_file.close();
			_fileIsOpen = false;
			Platform::File::PostprocessDownloaded(QFileInfo(_file).absoluteFilePath());
		}
		removeFromQueue();

		if (_localStatus == LocalStatus::NotFound) {
			if (_locationType != UnknownFileLocation
				&& !_filename.isEmpty()) {
				Local::writeFileLocation(
					mediaKey(_locationType, _dcId, _id),
					FileLocation(_filename));
			}
			if (_urlLocation
				|| _locationType == UnknownFileLocation
				|| _toCache == LoadToCacheAsWell) {
				if (const auto key = cacheKey()) {
					if (_data.size() <= Storage::kMaxFileInMemory) {
						Auth().data().cache().put(
							*key,
							Storage::Cache::Database::TaggedValue(
								base::duplicate(_data),
								_cacheTag));
					}
				}
			}
		}
	}
	if (_finished) {
		_downloader->taskFinished().notify();
	}
	return true;
}

void mtpFileLoader::partLoaded(int offset, bytes::const_span buffer) {
	if (feedPart(offset, buffer)) {
		notifyAboutProgress();
	}
}

bool mtpFileLoader::partFailed(
		const RPCError &error,
		mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) {
		return false;
	}
	if (error.code() == 400
		&& error.type().startsWith(qstr("FILE_REFERENCE_"))) {
		Auth().api().refreshFileReference(
			_origin,
			this,
			requestId,
			_location ? _location->fileReference() : _fileReference);
		return true;
	}
	cancel(true);
	return true;
}

bool mtpFileLoader::cdnPartFailed(
		const RPCError &error,
		mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) {
		return false;
	}

	if (requestId == _cdnHashesRequestId) {
		_cdnHashesRequestId = 0;
	}
	if (error.type() == qstr("FILE_TOKEN_INVALID")
		|| error.type() == qstr("REQUEST_TOKEN_INVALID")) {
		auto offset = finishSentRequestGetOffset(requestId);
		changeCDNParams(
			offset,
			0,
			QByteArray(),
			QByteArray(),
			QByteArray(),
			QVector<MTPFileHash>());
		return true;
	}
	return partFailed(error, requestId);
}

void mtpFileLoader::cancelRequests() {
	while (!_sentRequests.empty()) {
		auto requestId = _sentRequests.begin()->first;
		MTP::cancel(requestId);
		finishSentRequestGetOffset(requestId);
	}
}

void mtpFileLoader::switchToCDN(
		int offset,
		const MTPDupload_fileCdnRedirect &redirect) {
	changeCDNParams(
		offset,
		redirect.vdc_id.v,
		redirect.vfile_token.v,
		redirect.vencryption_key.v,
		redirect.vencryption_iv.v,
		redirect.vfile_hashes.v);
}

void mtpFileLoader::addCdnHashes(const QVector<MTPFileHash> &hashes) {
	for_const (auto &hash, hashes) {
		Assert(hash.type() == mtpc_fileHash);
		auto &data = hash.c_fileHash();
		_cdnFileHashes.emplace(
			data.voffset.v,
			CdnFileHash { data.vlimit.v, data.vhash.v });
	}
}

void mtpFileLoader::changeCDNParams(
		int offset,
		MTP::DcId dcId,
		const QByteArray &token,
		const QByteArray &encryptionKey,
		const QByteArray &encryptionIV,
		const QVector<MTPFileHash> &hashes) {
	if (dcId != 0
		&& (encryptionKey.size() != MTP::CTRState::KeySize
			|| encryptionIV.size() != MTP::CTRState::IvecSize)) {
		LOG(("Message Error: Wrong key (%1) / iv (%2) size in CDN params").arg(encryptionKey.size()).arg(encryptionIV.size()));
		cancel(true);
		return;
	}

	auto resendAllRequests = (_cdnDcId != dcId
		|| _cdnToken != token
		|| _cdnEncryptionKey != encryptionKey
		|| _cdnEncryptionIV != encryptionIV);
	_cdnDcId = dcId;
	_cdnToken = token;
	_cdnEncryptionKey = encryptionKey;
	_cdnEncryptionIV = encryptionIV;
	addCdnHashes(hashes);

	if (resendAllRequests && !_sentRequests.empty()) {
		auto resendOffsets = std::vector<int>();
		resendOffsets.reserve(_sentRequests.size());
		while (!_sentRequests.empty()) {
			auto requestId = _sentRequests.begin()->first;
			MTP::cancel(requestId);
			auto resendOffset = finishSentRequestGetOffset(requestId);
			resendOffsets.push_back(resendOffset);
		}
		for (auto resendOffset : resendOffsets) {
			makeRequest(resendOffset);
		}
	}
	makeRequest(offset);
}

std::optional<Storage::Cache::Key> mtpFileLoader::cacheKey() const {
	if (_urlLocation) {
		return Data::WebDocumentCacheKey(*_urlLocation);
	} else if (_geoLocation) {
		return Data::GeoPointCacheKey(*_geoLocation);
	} else if (_location) {
		return Data::StorageCacheKey(*_location);
	} else if (_toCache == LoadToCacheAsWell && _id != 0) {
		return Data::DocumentCacheKey(_dcId, _id);
	}
	return std::nullopt;
}

mtpFileLoader::~mtpFileLoader() {
	cancelRequests();
}

webFileLoader::webFileLoader(
	const QString &url,
	const QString &to,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: FileLoader(
	QString(),
	0,
	UnknownFileLocation,
	LoadToCacheAsWell,
	fromCloud,
	autoLoading,
	cacheTag)
, _url(url)
, _requestSent(false)
, _already(0) {
	_queue = &_webQueue;
}

bool webFileLoader::loadPart() {
	if (_finished || _requestSent || _webLoadManager == FinishedWebLoadManager) return false;
	if (!_webLoadManager) {
		_webLoadMainManager = new WebLoadMainManager();

		_webLoadThread = new QThread();
		_webLoadManager = new WebLoadManager(_webLoadThread);

		_webLoadThread->start();
	}

	_requestSent = true;
	_webLoadManager->append(this, _url);
	return false;
}

int32 webFileLoader::currentOffset(bool includeSkipped) const {
	return _already;
}

void webFileLoader::onProgress(qint64 already, qint64 size) {
	_size = size;
	_already = already;

	emit progress(this);
}

void webFileLoader::onFinished(const QByteArray &data) {
	if (_fileIsOpen) {
		if (_file.write(data.constData(), data.size()) != qint64(data.size())) {
			return cancel(true);
		}
	} else {
		_data = data;
	}
	if (!_filename.isEmpty() && (_toCache == LoadToCacheAsWell)) {
		if (!_fileIsOpen) _fileIsOpen = _file.open(QIODevice::WriteOnly);
		if (!_fileIsOpen) {
			return cancel(true);
		}
		if (_file.write(_data) != qint64(_data.size())) {
			return cancel(true);
		}
	}
	_finished = true;
	if (_fileIsOpen) {
		_file.close();
		_fileIsOpen = false;
		Platform::File::PostprocessDownloaded(QFileInfo(_file).absoluteFilePath());
	}
	removeFromQueue();

	if (_localStatus == LocalStatus::NotFound) {
		if (const auto key = cacheKey()) {
			if (_data.size() <= Storage::kMaxFileInMemory) {
				Auth().data().cache().put(
					*key,
					Storage::Cache::Database::TaggedValue(
						base::duplicate(_data),
						_cacheTag));
			}
		}
	}
	_downloader->taskFinished().notify();

	notifyAboutProgress();
}

void webFileLoader::onError() {
	cancel(true);
}

std::optional<Storage::Cache::Key> webFileLoader::cacheKey() const {
	return Data::UrlCacheKey(_url);
}

void webFileLoader::cancelRequests() {
	if (!webLoadManager()) return;
	webLoadManager()->stop(this);
}

webFileLoader::~webFileLoader() {
}

class webFileLoaderPrivate {
public:
	webFileLoaderPrivate(webFileLoader *loader, const QString &url)
		: _interface(loader)
		, _url(url)
		, _redirectsLeft(kMaxHttpRedirects) {
	}

	QNetworkReply *reply() {
		return _reply;
	}

	QNetworkReply *request(QNetworkAccessManager &manager, const QString &redirect) {
		if (!redirect.isEmpty()) _url = redirect;

		QNetworkRequest req(_url);
		QByteArray rangeHeaderValue = "bytes=" + QByteArray::number(_already) + "-";
		req.setRawHeader("Range", rangeHeaderValue);
		_reply = manager.get(req);
		return _reply;
	}

	bool oneMoreRedirect() {
		if (_redirectsLeft) {
			--_redirectsLeft;
			return true;
		}
		return false;
	}

	void setData(const QByteArray &data) {
		_data = data;
	}
	void addData(const QByteArray &data) {
		_data.append(data);
	}
	const QByteArray &data() {
		return _data;
	}
	void setProgress(qint64 already, qint64 size) {
		_already = already;
		_size = qMax(size, 0LL);
	}

	qint64 size() const {
		return _size;
	}
	qint64 already() const {
		return _already;
	}

private:
	static constexpr auto kMaxHttpRedirects = 5;

	webFileLoader *_interface = nullptr;
	QUrl _url;
	qint64 _already = 0;
	qint64 _size = 0;
	QNetworkReply *_reply = nullptr;
	int32 _redirectsLeft = kMaxHttpRedirects;
	QByteArray _data;

	friend class WebLoadManager;
};

void stopWebLoadManager() {
	if (webLoadManager()) {
		_webLoadThread->quit();
		DEBUG_LOG(("Waiting for webloadThread to finish"));
		_webLoadThread->wait();
		delete _webLoadManager;
		delete _webLoadMainManager;
		delete _webLoadThread;
		_webLoadThread = 0;
		_webLoadMainManager = 0;
		_webLoadManager = FinishedWebLoadManager;
	}
}

WebLoadManager::WebLoadManager(QThread *thread) {
	moveToThread(thread);
	_manager.moveToThread(thread);
	connect(thread, SIGNAL(started()), this, SLOT(process()));
	connect(thread, SIGNAL(finished()), this, SLOT(finish()));
	connect(this, SIGNAL(processDelayed()), this, SLOT(process()), Qt::QueuedConnection);

	connect(this, SIGNAL(progress(webFileLoader*,qint64,qint64)), _webLoadMainManager, SLOT(progress(webFileLoader*,qint64,qint64)));
	connect(this, SIGNAL(finished(webFileLoader*,QByteArray)), _webLoadMainManager, SLOT(finished(webFileLoader*,QByteArray)));
	connect(this, SIGNAL(error(webFileLoader*)), _webLoadMainManager, SLOT(error(webFileLoader*)));

	connect(&_manager, SIGNAL(authenticationRequired(QNetworkReply*,QAuthenticator*)), this, SLOT(onFailed(QNetworkReply*)));
#ifndef OS_MAC_OLD
	connect(&_manager, SIGNAL(sslErrors(QNetworkReply*,const QList<QSslError>&)), this, SLOT(onFailed(QNetworkReply*)));
#endif // OS_MAC_OLD
}

void WebLoadManager::append(webFileLoader *loader, const QString &url) {
	loader->_private = new webFileLoaderPrivate(loader, url);

	QMutexLocker lock(&_loaderPointersMutex);
	_loaderPointers.insert(loader, loader->_private);
	emit processDelayed();
}

void WebLoadManager::stop(webFileLoader *loader) {
	QMutexLocker lock(&_loaderPointersMutex);
	_loaderPointers.remove(loader);
	emit processDelayed();
}

bool WebLoadManager::carries(webFileLoader *loader) const {
	QMutexLocker lock(&_loaderPointersMutex);
	return _loaderPointers.contains(loader);
}

bool WebLoadManager::handleReplyResult(webFileLoaderPrivate *loader, WebReplyProcessResult result) {
	QMutexLocker lock(&_loaderPointersMutex);
	LoaderPointers::iterator it = _loaderPointers.find(loader->_interface);
	if (it != _loaderPointers.cend() && it.key()->_private != loader) {
		it = _loaderPointers.end(); // it is a new loader which was realloced in the same address
	}
	if (it == _loaderPointers.cend()) {
		return false;
	}

	if (result == WebReplyProcessProgress) {
		if (loader->size() > Storage::kMaxFileInMemory) {
			LOG(("API Error: too large file is loaded to cache: %1").arg(loader->size()));
			result = WebReplyProcessError;
		}
	}
	if (result == WebReplyProcessError) {
		if (it != _loaderPointers.cend()) {
			emit error(it.key());
		}
		return false;
	}
	if (loader->already() < loader->size() || !loader->size()) {
		emit progress(it.key(), loader->already(), loader->size());
		return true;
	}
	emit finished(it.key(), loader->data());
	return false;
}

void WebLoadManager::onFailed(QNetworkReply::NetworkError error) {
	onFailed(qobject_cast<QNetworkReply*>(QObject::sender()));
}

void WebLoadManager::onFailed(QNetworkReply *reply) {
	if (!reply) return;
	reply->deleteLater();

	Replies::iterator j = _replies.find(reply);
	if (j == _replies.cend()) { // handled already
		return;
	}
	webFileLoaderPrivate *loader = j.value();
	_replies.erase(j);

	LOG(("Network Error: Failed to request '%1', error %2 (%3)").arg(QString::fromLatin1(loader->_url.toEncoded())).arg(int(reply->error())).arg(reply->errorString()));

	if (!handleReplyResult(loader, WebReplyProcessError)) {
		_loaders.remove(loader);
		delete loader;
	}
}

void WebLoadManager::onProgress(qint64 already, qint64 size) {
	QNetworkReply *reply = qobject_cast<QNetworkReply*>(QObject::sender());
	if (!reply) return;

	Replies::iterator j = _replies.find(reply);
	if (j == _replies.cend()) { // handled already
		return;
	}
	webFileLoaderPrivate *loader = j.value();

	WebReplyProcessResult result = WebReplyProcessProgress;
	QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	int32 status = statusCode.isValid() ? statusCode.toInt() : 200;
	if (status != 200 && status != 206 && status != 416) {
		if (status == 301 || status == 302) {
			QString loc = reply->header(QNetworkRequest::LocationHeader).toString();
			if (!loc.isEmpty()) {
				if (loader->oneMoreRedirect()) {
					sendRequest(loader, loc);
					return;
				} else {
					LOG(("Network Error: Too many HTTP redirects in onFinished() for web file loader: %1").arg(loc));
					result = WebReplyProcessError;
				}
			}
		} else {
			LOG(("Network Error: Bad HTTP status received in WebLoadManager::onProgress(): %1").arg(statusCode.toInt()));
			result = WebReplyProcessError;
		}
	} else {
		loader->setProgress(already, size);
		QByteArray r = reply->readAll();
		if (!r.isEmpty()) {
			loader->addData(r);
		}
		if (size == 0) {
			LOG(("Network Error: Zero size received for HTTP download progress in WebLoadManager::onProgress(): %1 / %2").arg(already).arg(size));
			result = WebReplyProcessError;
		}
	}
	if (!handleReplyResult(loader, result)) {
		_replies.erase(j);
		_loaders.remove(loader);
		delete loader;

		reply->abort();
		reply->deleteLater();
	}
}

void WebLoadManager::onMeta() {
	QNetworkReply *reply = qobject_cast<QNetworkReply*>(QObject::sender());
	if (!reply) return;

	Replies::iterator j = _replies.find(reply);
	if (j == _replies.cend()) { // handled already
		return;
	}
	webFileLoaderPrivate *loader = j.value();

	typedef QList<QNetworkReply::RawHeaderPair> Pairs;
	Pairs pairs = reply->rawHeaderPairs();
	for (Pairs::iterator i = pairs.begin(), e = pairs.end(); i != e; ++i) {
		if (QString::fromUtf8(i->first).toLower() == "content-range") {
			QRegularExpressionMatch m = QRegularExpression(qsl("/(\\d+)([^\\d]|$)")).match(QString::fromUtf8(i->second));
			if (m.hasMatch()) {
				loader->setProgress(qMax(qint64(loader->data().size()), loader->already()), m.captured(1).toLongLong());
				if (!handleReplyResult(loader, WebReplyProcessProgress)) {
					_replies.erase(j);
					_loaders.remove(loader);
					delete loader;

					reply->abort();
					reply->deleteLater();
				}
			}
		}
	}
}

void WebLoadManager::process() {
	Loaders newLoaders;
	{
		QMutexLocker lock(&_loaderPointersMutex);
		for (LoaderPointers::iterator i = _loaderPointers.begin(), e = _loaderPointers.end(); i != e; ++i) {
			Loaders::iterator it = _loaders.find(i.value());
			if (i.value()) {
				if (it == _loaders.cend()) {
					_loaders.insert(i.value());
					newLoaders.insert(i.value());
				}
				i.value() = 0;
			}
		}
		for (auto i = _loaders.begin(), e = _loaders.end(); i != e;) {
			LoaderPointers::iterator it = _loaderPointers.find((*i)->_interface);
			if (it != _loaderPointers.cend() && it.key()->_private != (*i)) {
				it = _loaderPointers.end();
			}
			if (it == _loaderPointers.cend()) {
				if (QNetworkReply *reply = (*i)->reply()) {
					_replies.remove(reply);
					reply->abort();
					reply->deleteLater();
				}
				delete (*i);
				i = _loaders.erase(i);
			} else {
				++i;
			}
		}
	}
	for_const (webFileLoaderPrivate *loader, newLoaders) {
		if (_loaders.contains(loader)) {
			sendRequest(loader);
		}
	}
}

void WebLoadManager::sendRequest(webFileLoaderPrivate *loader, const QString &redirect) {
	Replies::iterator j = _replies.find(loader->reply());
	if (j != _replies.cend()) {
		QNetworkReply *r = j.key();
		_replies.erase(j);

		r->abort();
		r->deleteLater();
	}

	QNetworkReply *r = loader->request(_manager, redirect);

	// Those use QObject::sender, so don't just remove the receiver pointer!
	connect(r, SIGNAL(downloadProgress(qint64, qint64)), this, SLOT(onProgress(qint64, qint64)));
	connect(r, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onFailed(QNetworkReply::NetworkError)));
	connect(r, SIGNAL(metaDataChanged()), this, SLOT(onMeta()));

	_replies.insert(r, loader);
}

void WebLoadManager::finish() {
	clear();
}

void WebLoadManager::clear() {
	QMutexLocker lock(&_loaderPointersMutex);
	for (LoaderPointers::iterator i = _loaderPointers.begin(), e = _loaderPointers.end(); i != e; ++i) {
		if (i.value()) {
			i.key()->_private = 0;
		}
	}
	_loaderPointers.clear();

	for_const (webFileLoaderPrivate *loader, _loaders) {
		delete loader;
	}
	_loaders.clear();

	for (Replies::iterator i = _replies.begin(), e = _replies.end(); i != e; ++i) {
		delete i.key();
	}
	_replies.clear();
}

WebLoadManager::~WebLoadManager() {
	clear();
}

void WebLoadMainManager::progress(webFileLoader *loader, qint64 already, qint64 size) {
	if (webLoadManager() && webLoadManager()->carries(loader)) {
		loader->onProgress(already, size);
	}
}

void WebLoadMainManager::finished(webFileLoader *loader, QByteArray data) {
	if (webLoadManager() && webLoadManager()->carries(loader)) {
		loader->onFinished(data);
	}
}

void WebLoadMainManager::error(webFileLoader *loader) {
	if (webLoadManager() && webLoadManager()->carries(loader)) {
		loader->onError();
	}
}
