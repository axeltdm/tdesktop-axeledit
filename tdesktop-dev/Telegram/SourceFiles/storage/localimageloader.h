/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/variant.h"

enum class CompressConfirm {
	Auto,
	Yes,
	No,
	None,
};

enum class SendMediaType {
	Photo,
	Audio,
	File,
	Secure,
};

struct SendMediaPrepare {
	SendMediaPrepare(const QString &file, const PeerId &peer, SendMediaType type, MsgId replyTo) : id(rand_value<PhotoId>()), file(file), peer(peer), type(type), replyTo(replyTo) {
	}
	SendMediaPrepare(const QImage &img, const PeerId &peer, SendMediaType type, MsgId replyTo) : id(rand_value<PhotoId>()), img(img), peer(peer), type(type), replyTo(replyTo) {
	}
	SendMediaPrepare(const QByteArray &data, const PeerId &peer, SendMediaType type, MsgId replyTo) : id(rand_value<PhotoId>()), data(data), peer(peer), type(type), replyTo(replyTo) {
	}
	SendMediaPrepare(const QByteArray &data, int duration, const PeerId &peer, SendMediaType type, MsgId replyTo) : id(rand_value<PhotoId>()), data(data), peer(peer), type(type), duration(duration), replyTo(replyTo) {
	}
	PhotoId id;
	QString file;
	QImage img;
	QByteArray data;
	PeerId peer;
	SendMediaType type;
	int duration = 0;
	MsgId replyTo;

};
using SendMediaPrepareList = QList<SendMediaPrepare>;

using UploadFileParts =  QMap<int, QByteArray>;
struct SendMediaReady {
	SendMediaReady() = default; // temp
	SendMediaReady(SendMediaType type, const QString &file, const QString &filename, int32 filesize, const QByteArray &data, const uint64 &id, const uint64 &thumbId, const QString &thumbExt, const PeerId &peer, const MTPPhoto &photo, const PreparedPhotoThumbs &photoThumbs, const MTPDocument &document, const QByteArray &jpeg, MsgId replyTo)
		: replyTo(replyTo)
		, type(type)
		, file(file)
		, filename(filename)
		, filesize(filesize)
		, data(data)
		, thumbExt(thumbExt)
		, id(id)
		, thumbId(thumbId)
		, peer(peer)
		, photo(photo)
		, document(document)
		, photoThumbs(photoThumbs) {
		if (!jpeg.isEmpty()) {
			int32 size = jpeg.size();
			for (int32 i = 0, part = 0; i < size; i += UploadPartSize, ++part) {
				parts.insert(part, jpeg.mid(i, UploadPartSize));
			}
			jpeg_md5.resize(32);
			hashMd5Hex(jpeg.constData(), jpeg.size(), jpeg_md5.data());
		}
	}
	MsgId replyTo;
	SendMediaType type;
	QString file, filename;
	int32 filesize;
	QByteArray data;
	QString thumbExt;
	uint64 id, thumbId; // id always file-id of media, thumbId is file-id of thumb ( == id for photos)
	PeerId peer;

	MTPPhoto photo;
	MTPDocument document;
	PreparedPhotoThumbs photoThumbs;
	UploadFileParts parts;
	QByteArray jpeg_md5;

	QString caption;

};

SendMediaReady PreparePeerPhoto(PeerId peerId, QImage &&image);

using TaskId = void*; // no interface, just id

class Task {
public:
	virtual void process() = 0; // is executed in a separate thread
	virtual void finish() = 0; // is executed in the same as TaskQueue thread
	virtual ~Task() = default;

	TaskId id() const {
		return static_cast<TaskId>(const_cast<Task*>(this));
	}

};

class TaskQueueWorker;
class TaskQueue : public QObject {
	Q_OBJECT

public:
	explicit TaskQueue(TimeMs stopTimeoutMs = 0); // <= 0 - never stop worker

	TaskId addTask(std::unique_ptr<Task> &&task);
	void addTasks(std::vector<std::unique_ptr<Task>> &&tasks);
	void cancelTask(TaskId id); // this task finish() won't be called

	~TaskQueue();

signals:
	void taskAdded();

public slots:
	void onTaskProcessed();
	void stop();

private:
	friend class TaskQueueWorker;

	void wakeThread();

	std::deque<std::unique_ptr<Task>> _tasksToProcess;
	std::deque<std::unique_ptr<Task>> _tasksToFinish;
	TaskId _taskInProcessId = TaskId();
	QMutex _tasksToProcessMutex, _tasksToFinishMutex;
	QThread *_thread = nullptr;
	TaskQueueWorker *_worker = nullptr;
	QTimer *_stopTimer = nullptr;

};

class TaskQueueWorker : public QObject {
	Q_OBJECT

public:
	TaskQueueWorker(TaskQueue *queue) : _queue(queue) {
	}

signals:
	void taskProcessed();

public slots:
	void onTaskAdded();

private:
	TaskQueue *_queue;
	bool _inTaskAdded = false;

};

struct SendingAlbum {
	struct Item {
		explicit Item(TaskId taskId) : taskId(taskId) {
		}
		TaskId taskId;
		FullMsgId msgId;
		std::optional<MTPInputSingleMedia> media;
	};

	SendingAlbum();

	uint64 groupId = 0;
	std::vector<Item> items;
	bool silent = false;

};

struct FileLoadTo {
	FileLoadTo(const PeerId &peer, bool silent, MsgId replyTo)
		: peer(peer)
		, silent(silent)
		, replyTo(replyTo) {
	}
	PeerId peer;
	bool silent;
	MsgId replyTo;
};

struct FileLoadResult {
	FileLoadResult(
		TaskId taskId,
		uint64 id,
		const FileLoadTo &to,
		const TextWithTags &caption,
		std::shared_ptr<SendingAlbum> album);

	TaskId taskId;
	uint64 id;
	FileLoadTo to;
	std::shared_ptr<SendingAlbum> album;
	SendMediaType type = SendMediaType::File;
	QString filepath;
	QByteArray content;

	QString filename;
	QString filemime;
	int32 filesize = 0;
	UploadFileParts fileparts;
	QByteArray filemd5;
	int32 partssize;

	uint64 thumbId = 0; // id is always file-id of media, thumbId is file-id of thumb ( == id for photos)
	QString thumbname;
	UploadFileParts thumbparts;
	QByteArray thumbmd5;
	QImage thumb;

	QImage goodThumbnail;
	QByteArray goodThumbnailBytes;

	MTPPhoto photo;
	MTPDocument document;

	PreparedPhotoThumbs photoThumbs;
	TextWithTags caption;

	void setFileData(const QByteArray &filedata) {
		if (filedata.isEmpty()) {
			partssize = 0;
		} else {
			partssize = filedata.size();
			for (int32 i = 0, part = 0; i < partssize; i += UploadPartSize, ++part) {
				fileparts.insert(part, filedata.mid(i, UploadPartSize));
			}
			filemd5.resize(32);
			hashMd5Hex(filedata.constData(), filedata.size(), filemd5.data());
		}
	}
	void setThumbData(const QByteArray &thumbdata) {
		if (!thumbdata.isEmpty()) {
			int32 size = thumbdata.size();
			for (int32 i = 0, part = 0; i < size; i += UploadPartSize, ++part) {
				thumbparts.insert(part, thumbdata.mid(i, UploadPartSize));
			}
			thumbmd5.resize(32);
			hashMd5Hex(thumbdata.constData(), thumbdata.size(), thumbmd5.data());
		}
	}
};

struct FileMediaInformation {
	struct Image {
		QImage data;
		bool animated = false;
	};
	struct Song {
		int duration = -1;
		QString title;
		QString performer;
		QImage cover;
	};
	struct Video {
		bool isGifv = false;
		bool supportsStreaming = false;
		int duration = -1;
		QImage thumbnail;
	};

	QString filemime;
	base::variant<Image, Song, Video> media;
};

class FileLoadTask final : public Task {
public:
	static std::unique_ptr<FileMediaInformation> ReadMediaInformation(
		const QString &filepath,
		const QByteArray &content,
		const QString &filemime);
	static bool FillImageInformation(
		QImage &&image,
		bool animated,
		std::unique_ptr<FileMediaInformation> &result);

	FileLoadTask(
		const QString &filepath,
		const QByteArray &content,
		std::unique_ptr<FileMediaInformation> information,
		SendMediaType type,
		const FileLoadTo &to,
		const TextWithTags &caption,
		std::shared_ptr<SendingAlbum> album = nullptr);
	FileLoadTask(
		const QByteArray &voice,
		int32 duration,
		const VoiceWaveform &waveform,
		const FileLoadTo &to,
		const TextWithTags &caption);

	uint64 fileid() const {
		return _id;
	}

	void process();
	void finish();

private:
	static bool CheckForSong(
		const QString &filepath,
		const QByteArray &content,
		std::unique_ptr<FileMediaInformation> &result);
	static bool CheckForVideo(
		const QString &filepath,
		const QByteArray &content,
		std::unique_ptr<FileMediaInformation> &result);
	static bool CheckForImage(
		const QString &filepath,
		const QByteArray &content,
		std::unique_ptr<FileMediaInformation> &result);

	template <typename Mimes, typename Extensions>
	static bool CheckMimeOrExtensions(const QString &filepath, const QString &filemime, Mimes &mimes, Extensions &extensions);

	std::unique_ptr<FileMediaInformation> readMediaInformation(const QString &filemime) const {
		return ReadMediaInformation(_filepath, _content, filemime);
	}
	void removeFromAlbum();

	uint64 _id;
	FileLoadTo _to;
	const std::shared_ptr<SendingAlbum> _album;
	QString _filepath;
	QByteArray _content;
	std::unique_ptr<FileMediaInformation> _information;
	int32 _duration = 0;
	VoiceWaveform _waveform;
	SendMediaType _type;
	TextWithTags _caption;

	std::shared_ptr<FileLoadResult> _result;

};
