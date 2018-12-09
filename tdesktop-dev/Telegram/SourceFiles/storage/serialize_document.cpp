/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/serialize_document.h"

#include "storage/serialize_common.h"
#include "chat_helpers/stickers.h"
#include "data/data_session.h"
#include "ui/image/image.h"
#include "auth_session.h"

namespace {

enum StickerSetType {
	StickerSetTypeEmpty = 0,
	StickerSetTypeID = 1,
	StickerSetTypeShortName = 2,
};

} // namespace

namespace Serialize {

void Document::writeToStream(QDataStream &stream, DocumentData *document) {
	const auto version = 0;
	stream << quint64(document->id) << quint64(document->_access) << qint32(document->date);
	stream << document->_fileReference << qint32(version);
	stream << document->filename() << document->mimeString() << qint32(document->_dc) << qint32(document->size);
	stream << qint32(document->dimensions.width()) << qint32(document->dimensions.height());
	stream << qint32(document->type);
	if (auto sticker = document->sticker()) {
		stream << document->sticker()->alt;
		switch (document->sticker()->set.type()) {
		case mtpc_inputStickerSetID: {
			stream << qint32(StickerSetTypeID);
		} break;
		case mtpc_inputStickerSetShortName: {
			stream << qint32(StickerSetTypeShortName);
		} break;
		case mtpc_inputStickerSetEmpty:
		default: {
			stream << qint32(StickerSetTypeEmpty);
		} break;
		}
		writeStorageImageLocation(stream, document->sticker()->loc);
	} else {
		stream << qint32(document->duration());
		writeStorageImageLocation(stream, document->thumb->location());
	}
}

DocumentData *Document::readFromStreamHelper(int streamAppVersion, QDataStream &stream, const StickerSetInfo *info) {
	quint64 id, access;
	QString name, mime;
	qint32 date, dc, size, width, height, type, version;
	QByteArray fileReference;
	stream >> id >> access >> date;
	if (streamAppVersion >= 9061) {
		if (streamAppVersion >= 1003013) {
			stream >> fileReference;
		}
		stream >> version;
	} else {
		version = 0;
	}
	stream >> name >> mime >> dc >> size;
	stream >> width >> height;
	stream >> type;

	QVector<MTPDocumentAttribute> attributes;
	if (!name.isEmpty()) {
		attributes.push_back(MTP_documentAttributeFilename(MTP_string(name)));
	}

	qint32 duration = -1;
	StorageImageLocation thumb;
	if (type == StickerDocument) {
		QString alt;
		qint32 typeOfSet;
		stream >> alt >> typeOfSet;

		thumb = readStorageImageLocation(streamAppVersion, stream);

		if (typeOfSet == StickerSetTypeEmpty) {
			attributes.push_back(MTP_documentAttributeSticker(MTP_flags(0), MTP_string(alt), MTP_inputStickerSetEmpty(), MTPMaskCoords()));
		} else if (info) {
			if (info->setId == Stickers::DefaultSetId
				|| info->setId == Stickers::CloudRecentSetId
				|| info->setId == Stickers::FavedSetId
				|| info->setId == Stickers::CustomSetId) {
				typeOfSet = StickerSetTypeEmpty;
			}

			switch (typeOfSet) {
			case StickerSetTypeID: {
				attributes.push_back(MTP_documentAttributeSticker(MTP_flags(0), MTP_string(alt), MTP_inputStickerSetID(MTP_long(info->setId), MTP_long(info->accessHash)), MTPMaskCoords()));
			} break;
			case StickerSetTypeShortName: {
				attributes.push_back(MTP_documentAttributeSticker(MTP_flags(0), MTP_string(alt), MTP_inputStickerSetShortName(MTP_string(info->shortName)), MTPMaskCoords()));
			} break;
			case StickerSetTypeEmpty:
			default: {
				attributes.push_back(MTP_documentAttributeSticker(MTP_flags(0), MTP_string(alt), MTP_inputStickerSetEmpty(), MTPMaskCoords()));
			} break;
			}
		}
	} else {
		stream >> duration;
		if (type == AnimatedDocument) {
			attributes.push_back(MTP_documentAttributeAnimated());
		}
		thumb = readStorageImageLocation(streamAppVersion, stream);
	}
	if (width > 0 && height > 0) {
		if (duration >= 0) {
			auto flags = MTPDdocumentAttributeVideo::Flags(0);
			if (type == RoundVideoDocument) {
				flags |= MTPDdocumentAttributeVideo::Flag::f_round_message;
			}
			attributes.push_back(MTP_documentAttributeVideo(MTP_flags(flags), MTP_int(duration), MTP_int(width), MTP_int(height)));
		} else {
			attributes.push_back(MTP_documentAttributeImageSize(MTP_int(width), MTP_int(height)));
		}
	}

	if (!dc && !access) {
		return nullptr;
	}
	return Auth().data().document(
		id,
		access,
		fileReference,
		date,
		attributes,
		mime,
		thumb.isNull() ? ImagePtr() : Images::Create(thumb),
		dc,
		size,
		thumb);
}

DocumentData *Document::readStickerFromStream(int streamAppVersion, QDataStream &stream, const StickerSetInfo &info) {
	return readFromStreamHelper(streamAppVersion, stream, &info);
}

DocumentData *Document::readFromStream(int streamAppVersion, QDataStream &stream) {
	return readFromStreamHelper(streamAppVersion, stream, nullptr);
}

int Document::sizeInStream(DocumentData *document) {
	int result = 0;

	// id + access + date + version
	result += sizeof(quint64) + sizeof(quint64) + sizeof(qint32) + sizeof(qint32);
	// + namelen + name + mimelen + mime + dc + size
	result += stringSize(document->filename()) + stringSize(document->mimeString()) + sizeof(qint32) + sizeof(qint32);
	// + width + height
	result += sizeof(qint32) + sizeof(qint32);
	// + type
	result += sizeof(qint32);

	if (auto sticker = document->sticker()) { // type == StickerDocument
		// + altlen + alt + type-of-set
		result += stringSize(sticker->alt) + sizeof(qint32);
		// + sticker loc
		result += Serialize::storageImageLocationSize(document->sticker()->loc);
	} else {
		// + duration
		result += sizeof(qint32);
		// + thumb loc
		result += Serialize::storageImageLocationSize(document->thumb->location());
	}

	return result;
}

} // namespace Serialize
