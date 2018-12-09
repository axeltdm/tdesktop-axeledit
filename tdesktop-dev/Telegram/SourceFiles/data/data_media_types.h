/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;
class HistoryMedia;
class LocationCoords;
struct LocationData;

namespace base {
template <typename Enum>
class enum_mask;
} // namespace base

namespace Storage {
enum class SharedMediaType : signed char;
using SharedMediaTypesMask = base::enum_mask<SharedMediaType>;
} // namespace Storage

namespace HistoryView {
enum class Context : char;
class Element;
} // namespace HistoryView

namespace Data {

enum class CallFinishReason : char {
	Missed,
	Busy,
	Disconnected,
	Hangup,
};

struct SharedContact {
	UserId userId = 0;
	QString firstName;
	QString lastName;
	QString phoneNumber;

};

struct Call {
	using FinishReason = CallFinishReason;

	int duration = 0;
	FinishReason finishReason = FinishReason::Missed;

};

struct Invoice {
	MsgId receiptMsgId = 0;
	uint64 amount = 0;
	QString currency;
	QString title;
	QString description;
	PhotoData *photo = nullptr;
	bool isTest = false;

};

class Media {
public:
	Media(not_null<HistoryItem*> parent);
	virtual ~Media() = default;

	not_null<HistoryItem*> parent() const;

	virtual std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) = 0;

	virtual DocumentData *document() const;
	virtual PhotoData *photo() const;
	virtual WebPageData *webpage() const;
	virtual const SharedContact *sharedContact() const;
	virtual const Call *call() const;
	virtual GameData *game() const;
	virtual const Invoice *invoice() const;
	virtual LocationData *location() const;

	virtual bool uploading() const;
	virtual Storage::SharedMediaTypesMask sharedMediaTypes() const;
	virtual bool canBeGrouped() const;
	virtual bool hasReplyPreview() const;
	virtual Image *replyPreview() const;
	// Returns text with link-start and link-end commands for service-color highlighting.
	// Example: "[link1-start]You:[link1-end] [link1-start]Photo,[link1-end] caption text"
	virtual QString chatsListText() const;
	virtual QString notificationText() const = 0;
	virtual QString pinnedTextSubstring() const = 0;
	virtual TextWithEntities clipboardText() const = 0;
	virtual bool allowsForward() const;
	virtual bool allowsEdit() const;
	virtual bool allowsEditCaption() const;
	virtual bool allowsRevoke() const;
	virtual bool forwardedBecomesUnread() const;
	virtual QString errorTextForForward(
		not_null<ChannelData*> channel) const;

	[[nodiscard]] virtual bool consumeMessageText(
		const TextWithEntities &text);
	[[nodiscard]] virtual TextWithEntities consumedMessageText() const;

	// After sending an inline result we may want to completely recreate
	// the media (all media that was generated on client side, for example).
	virtual bool updateInlineResultMedia(const MTPMessageMedia &media) = 0;
	virtual bool updateSentMedia(const MTPMessageMedia &media) = 0;
	virtual std::unique_ptr<HistoryMedia> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) = 0;
	std::unique_ptr<HistoryMedia> createView(
		not_null<HistoryView::Element*> message);

private:
	const not_null<HistoryItem*> _parent;

};

class MediaPhoto : public Media {
public:
	MediaPhoto(
		not_null<HistoryItem*> parent,
		not_null<PhotoData*> photo);
	MediaPhoto(
		not_null<HistoryItem*> parent,
		not_null<PeerData*> chat,
		not_null<PhotoData*> photo);
	~MediaPhoto();

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	PhotoData *photo() const override;

	bool uploading() const override;
	Storage::SharedMediaTypesMask sharedMediaTypes() const override;
	bool canBeGrouped() const override;
	bool hasReplyPreview() const override;
	Image *replyPreview() const override;
	QString chatsListText() const override;
	QString notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextWithEntities clipboardText() const override;
	bool allowsEditCaption() const override;
	QString errorTextForForward(
		not_null<ChannelData*> channel) const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryMedia> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) override;

private:
	not_null<PhotoData*> _photo;
	PeerData *_chat = nullptr;

};

class MediaFile : public Media {
public:
	MediaFile(
		not_null<HistoryItem*> parent,
		not_null<DocumentData*> document);
	~MediaFile();

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	DocumentData *document() const override;

	bool uploading() const override;
	Storage::SharedMediaTypesMask sharedMediaTypes() const override;
	bool canBeGrouped() const override;
	bool hasReplyPreview() const override;
	Image *replyPreview() const override;
	QString chatsListText() const override;
	QString notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextWithEntities clipboardText() const override;
	bool allowsEditCaption() const override;
	bool forwardedBecomesUnread() const override;
	QString errorTextForForward(
		not_null<ChannelData*> channel) const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryMedia> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) override;

private:
	not_null<DocumentData*> _document;
	QString _emoji;

};

class MediaContact : public Media {
public:
	MediaContact(
		not_null<HistoryItem*> parent,
		UserId userId,
		const QString &firstName,
		const QString &lastName,
		const QString &phoneNumber);
	~MediaContact();

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	const SharedContact *sharedContact() const override;
	QString notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextWithEntities clipboardText() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryMedia> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) override;

private:
	SharedContact _contact;

};

class MediaLocation : public Media {
public:
	MediaLocation(
		not_null<HistoryItem*> parent,
		const LocationCoords &coords);
	MediaLocation(
		not_null<HistoryItem*> parent,
		const LocationCoords &coords,
		const QString &title,
		const QString &description);

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	LocationData *location() const override;
	QString chatsListText() const override;
	QString notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextWithEntities clipboardText() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryMedia> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) override;

private:
	not_null<LocationData*> _location;
	QString _title;
	QString _description;

};

class MediaCall : public Media {
public:
	MediaCall(
		not_null<HistoryItem*> parent,
		const MTPDmessageActionPhoneCall &call);

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	const Call *call() const override;
	QString notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextWithEntities clipboardText() const override;
	bool allowsForward() const override;
	bool allowsRevoke() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryMedia> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) override;

	static QString Text(
		not_null<HistoryItem*> item,
		CallFinishReason reason);

private:
	Call _call;

};

class MediaWebPage : public Media {
public:
	MediaWebPage(
		not_null<HistoryItem*> parent,
		not_null<WebPageData*> page);
	~MediaWebPage();

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	DocumentData *document() const override;
	PhotoData *photo() const override;
	WebPageData *webpage() const override;

	bool hasReplyPreview() const override;
	Image *replyPreview() const override;
	QString chatsListText() const override;
	QString notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextWithEntities clipboardText() const override;
	bool allowsEdit() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryMedia> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) override;

private:
	not_null<WebPageData*> _page;

};

class MediaGame : public Media {
public:
	MediaGame(
		not_null<HistoryItem*> parent,
		not_null<GameData*> game);

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	GameData *game() const override;

	bool hasReplyPreview() const override;
	Image *replyPreview() const override;
	QString notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextWithEntities clipboardText() const override;
	QString errorTextForForward(
		not_null<ChannelData*> channel) const override;

	bool consumeMessageText(const TextWithEntities &text) override;
	TextWithEntities consumedMessageText() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryMedia> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) override;

private:
	not_null<GameData*> _game;
	TextWithEntities _consumedText;

};

class MediaInvoice : public Media {
public:
	MediaInvoice(
		not_null<HistoryItem*> parent,
		const MTPDmessageMediaInvoice &data);
	MediaInvoice(
		not_null<HistoryItem*> parent,
		const Invoice &data);

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	const Invoice *invoice() const override;

	bool hasReplyPreview() const override;
	Image *replyPreview() const override;
	QString notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextWithEntities clipboardText() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryMedia> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) override;

private:
	Invoice _invoice;

};

TextWithEntities WithCaptionClipboardText(
	const QString &attachType,
	TextWithEntities &&caption);

} // namespace Data
