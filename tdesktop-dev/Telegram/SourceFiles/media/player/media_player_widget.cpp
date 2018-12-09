/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_widget.h"

#include "data/data_document.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/effects/ripple_animation.h"
#include "lang/lang_keys.h"
#include "media/media_audio.h"
#include "media/view/media_clip_playback.h"
#include "media/player/media_player_button.h"
#include "media/player/media_player_instance.h"
#include "media/player/media_player_volume_controller.h"
#include "styles/style_media_player.h"
#include "styles/style_mediaview.h"
#include "history/history_item.h"
#include "storage/localstorage.h"
#include "layout.h"
#include "facades.h"

namespace Media {
namespace Player {

using ButtonState = PlayButtonLayout::State;

class Widget::PlayButton : public Ui::RippleButton {
public:
	PlayButton(QWidget *parent);

	void setState(PlayButtonLayout::State state) {
		_layout.setState(state);
	}
	void finishTransform() {
		_layout.finishTransform();
	}

protected:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	PlayButtonLayout _layout;

};

Widget::PlayButton::PlayButton(QWidget *parent) : Ui::RippleButton(parent, st::mediaPlayerButton.ripple)
, _layout(st::mediaPlayerButton, [this] { update(); }) {
	resize(st::mediaPlayerButtonSize);
	setCursor(style::cur_pointer);
}

void Widget::PlayButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	paintRipple(p, st::mediaPlayerButton.rippleAreaPosition.x(), st::mediaPlayerButton.rippleAreaPosition.y(), getms());
	p.translate(st::mediaPlayerButtonPosition.x(), st::mediaPlayerButtonPosition.y());
	_layout.paint(p, st::mediaPlayerActiveFg);
}

QImage Widget::PlayButton::prepareRippleMask() const {
	auto size = QSize(st::mediaPlayerButton.rippleAreaSize, st::mediaPlayerButton.rippleAreaSize);
	return Ui::RippleAnimation::ellipseMask(size);
}

QPoint Widget::PlayButton::prepareRippleStartPosition() const {
	return QPoint(mapFromGlobal(QCursor::pos()) - st::mediaPlayerButton.rippleAreaPosition);
}

Widget::Widget(QWidget *parent) : RpWidget(parent)
, _nameLabel(this, st::mediaPlayerName)
, _timeLabel(this, st::mediaPlayerTime)
, _playPause(this)
, _volumeToggle(this, st::mediaPlayerVolumeToggle)
, _repeatTrack(this, st::mediaPlayerRepeatButton)
, _playbackSpeed(this, st::mediaPlayerSpeedButton)
, _close(this, st::mediaPlayerClose)
, _shadow(this)
, _playbackSlider(this, st::mediaPlayerPlayback)
, _playback(std::make_unique<Clip::Playback>()) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	setMouseTracking(true);
	resize(width(), st::mediaPlayerHeight + st::lineWidth);

	_nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	_timeLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	_playback->setInLoadingStateChangedCallback([this](bool loading) {
		_playbackSlider->setDisabled(loading);
	});
	_playback->setValueChangedCallback([this](float64 value) {
		_playbackSlider->setValue(value);
	});
	_playbackSlider->setChangeProgressCallback([this](float64 value) {
		if (_type != AudioMsgId::Type::Song) {
			return; // Round video seek is not supported for now :(
		}
		_playback->setValue(value, false);
		handleSeekProgress(value);
	});
	_playbackSlider->setChangeFinishedCallback([this](float64 value) {
		if (_type != AudioMsgId::Type::Song) {
			return; // Round video seek is not supported for now :(
		}
		_playback->setValue(value, false);
		handleSeekFinished(value);
	});
	_playPause->setClickedCallback([this] {
		instance()->playPauseCancelClicked(_type);
	});

	updateVolumeToggleIcon();
	_volumeToggle->setClickedCallback([=] {
		Global::SetSongVolume((Global::SongVolume() > 0) ? 0. : Global::RememberedSongVolume());
		mixer()->setSongVolume(Global::SongVolume());
		Global::RefSongVolumeChanged().notify();
	});
	subscribe(Global::RefSongVolumeChanged(), [this] { updateVolumeToggleIcon(); });

	updateRepeatTrackIcon();
	_repeatTrack->setClickedCallback([=] {
		instance()->toggleRepeat(AudioMsgId::Type::Song);
	});

	updatePlaybackSpeedIcon();
	_playbackSpeed->setClickedCallback([=] {
		const auto doubled = !Global::VoiceMsgPlaybackDoubled();
		Global::SetVoiceMsgPlaybackDoubled(doubled);
		mixer()->setVoicePlaybackDoubled(doubled);
		updatePlaybackSpeedIcon();
		Local::writeUserSettings();
	});

	subscribe(instance()->repeatChangedNotifier(), [this](AudioMsgId::Type type) {
		if (type == _type) {
			updateRepeatTrackIcon();
		}
	});
	subscribe(instance()->updatedNotifier(), [this](const TrackState &state) {
		handleSongUpdate(state);
	});
	subscribe(instance()->trackChangedNotifier(), [this](AudioMsgId::Type type) {
		if (type == _type) {
			handleSongChange();
		}
	});
	subscribe(instance()->tracksFinishedNotifier(), [this](AudioMsgId::Type type) {
		if (type == AudioMsgId::Type::Voice) {
			_voiceIsActive = false;
			auto currentSong = instance()->current(AudioMsgId::Type::Song);
			auto songState = mixer()->currentState(AudioMsgId::Type::Song);
			if (currentSong == songState.id && !IsStoppedOrStopping(songState.state)) {
				setType(AudioMsgId::Type::Song);
			}
		}
	});
	setType(AudioMsgId::Type::Song);
	_playPause->finishTransform();
}

void Widget::updateVolumeToggleIcon() {
	auto icon = []() -> const style::icon * {
		auto volume = Global::SongVolume();
		if (volume > 0) {
			if (volume < 1 / 3.) {
				return &st::mediaPlayerVolumeIcon1;
			} else if (volume < 2 / 3.) {
				return &st::mediaPlayerVolumeIcon2;
			}
			return &st::mediaPlayerVolumeIcon3;
		}
		return nullptr;
	};
	_volumeToggle->setIconOverride(icon());
}

void Widget::setCloseCallback(Fn<void()> callback) {
	_closeCallback = std::move(callback);
	_close->setClickedCallback([this] { stopAndClose(); });
}

void Widget::stopAndClose() {
	_voiceIsActive = false;
	if (_type == AudioMsgId::Type::Voice) {
		auto songData = instance()->current(AudioMsgId::Type::Song);
		auto songState = mixer()->currentState(AudioMsgId::Type::Song);
		if (songData == songState.id && !IsStoppedOrStopping(songState.state)) {
			instance()->stop(AudioMsgId::Type::Voice);
			return;
		}
	}
	if (_closeCallback) {
		_closeCallback();
	}
}

void Widget::setShadowGeometryToLeft(int x, int y, int w, int h) {
	_shadow->setGeometryToLeft(x, y, w, h);
}

void Widget::showShadow() {
	_shadow->show();
	_playbackSlider->setVisible(_type == AudioMsgId::Type::Song);
}

void Widget::hideShadow() {
	_shadow->hide();
	_playbackSlider->hide();
}

QPoint Widget::getPositionForVolumeWidget() const {
	auto x = _volumeToggle->x();
	x += (_volumeToggle->width() - st::mediaPlayerVolumeSize.width()) / 2;
	if (rtl()) x = width() - x - st::mediaPlayerVolumeSize.width();
	return QPoint(x, height());
}

void Widget::volumeWidgetCreated(VolumeWidget *widget) {
	_volumeToggle->installEventFilter(widget);
}

Widget::~Widget() = default;

void Widget::handleSeekProgress(float64 progress) {
	if (!_lastDurationMs) return;

	auto positionMs = snap(static_cast<TimeMs>(progress * _lastDurationMs), 0LL, _lastDurationMs);
	if (_seekPositionMs != positionMs) {
		_seekPositionMs = positionMs;
		updateTimeLabel();

		instance()->startSeeking(_type);
	}
}

void Widget::handleSeekFinished(float64 progress) {
	if (!_lastDurationMs) return;

	auto positionMs = snap(static_cast<TimeMs>(progress * _lastDurationMs), 0LL, _lastDurationMs);
	_seekPositionMs = -1;

	auto state = mixer()->currentState(_type);
	if (state.id && state.length && state.frequency) {
		mixer()->seek(_type, qRound(progress * state.length * 1000. / state.frequency));
	}

	instance()->stopSeeking(_type);
}

void Widget::resizeEvent(QResizeEvent *e) {
	auto right = st::mediaPlayerCloseRight;
	_close->moveToRight(right, st::mediaPlayerPlayTop); right += _close->width();
	if (hasPlaybackSpeedControl()) {
		_playbackSpeed->moveToRight(right, st::mediaPlayerPlayTop); right += _playbackSpeed->width();
	}
	_repeatTrack->moveToRight(right, st::mediaPlayerPlayTop); right += _repeatTrack->width();
	_volumeToggle->moveToRight(right, st::mediaPlayerPlayTop); right += _volumeToggle->width();

	updatePlayPrevNextPositions();

	_playbackSlider->setGeometry(0, height() - st::mediaPlayerPlayback.fullWidth, width(), st::mediaPlayerPlayback.fullWidth);
}

void Widget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto fill = e->rect().intersected(QRect(0, 0, width(), st::mediaPlayerHeight));
	if (!fill.isEmpty()) {
		p.fillRect(fill, st::mediaPlayerBg);
	}
}

void Widget::leaveEventHook(QEvent *e) {
	updateOverLabelsState(false);
}

void Widget::mouseMoveEvent(QMouseEvent *e) {
	updateOverLabelsState(e->pos());
}

void Widget::mousePressEvent(QMouseEvent *e) {
	_labelsDown = _labelsOver;
}

void Widget::mouseReleaseEvent(QMouseEvent *e) {
	if (auto downLabels = base::take(_labelsDown)) {
		if (_labelsOver == downLabels) {
			if (_type == AudioMsgId::Type::Voice) {
				auto current = instance()->current(_type);
				if (auto item = App::histItemById(current.contextId())) {
					Ui::showPeerHistoryAtItem(item);
				}
			}
		}
	}
}

void Widget::updateOverLabelsState(QPoint pos) {
	auto left = getLabelsLeft();
	auto right = getLabelsRight();
	auto labels = myrtlrect(left, 0, width() - right - left, height() - st::mediaPlayerPlayback.fullWidth);
	auto over = labels.contains(pos);
	updateOverLabelsState(over);
}

void Widget::updateOverLabelsState(bool over) {
	_labelsOver = over;
	auto pressShowsItem = _labelsOver && (_type == AudioMsgId::Type::Voice);
	setCursor(pressShowsItem ? style::cur_pointer : style::cur_default);
	auto showPlaylist = over && (_type == AudioMsgId::Type::Song);
	instance()->playerWidgetOver().notify(showPlaylist, true);
}

void Widget::updatePlayPrevNextPositions() {
	auto left = st::mediaPlayerPlayLeft;
	auto top = st::mediaPlayerPlayTop;
	if (_previousTrack) {
		_previousTrack->moveToLeft(left, top); left += _previousTrack->width() + st::mediaPlayerPlaySkip;
		_playPause->moveToLeft(left, top); left += _playPause->width() + st::mediaPlayerPlaySkip;
		_nextTrack->moveToLeft(left, top);
	} else {
		_playPause->moveToLeft(left, top);
	}
	updateLabelsGeometry();
}

int Widget::getLabelsLeft() const {
	auto result = st::mediaPlayerPlayLeft + _playPause->width();
	if (_previousTrack) {
		result += _previousTrack->width() + st::mediaPlayerPlaySkip + _nextTrack->width() + st::mediaPlayerPlaySkip;
	}
	result += st::mediaPlayerPadding;
	return result;
}

int Widget::getLabelsRight() const {
	auto result = st::mediaPlayerCloseRight + _close->width();
	if (_type == AudioMsgId::Type::Song) {
		result += _repeatTrack->width() + _volumeToggle->width();
	} else if (hasPlaybackSpeedControl()) {
		result += _playbackSpeed->width();
	}
	result += st::mediaPlayerPadding;
	return result;
}

void Widget::updateLabelsGeometry() {
	auto left = getLabelsLeft();
	auto right = getLabelsRight();

	auto widthForName = width() - left - right;
	widthForName -= _timeLabel->width() + 2 * st::normalFont->spacew;
	_nameLabel->resizeToWidth(widthForName);

	_nameLabel->moveToLeft(left, st::mediaPlayerNameTop - st::mediaPlayerName.style.font->ascent);
	_timeLabel->moveToRight(right, st::mediaPlayerNameTop - st::mediaPlayerTime.font->ascent);
}

void Widget::updateRepeatTrackIcon() {
	auto repeating = instance()->repeatEnabled(AudioMsgId::Type::Song);
	_repeatTrack->setIconOverride(repeating ? nullptr : &st::mediaPlayerRepeatDisabledIcon, repeating ? nullptr : &st::mediaPlayerRepeatDisabledIconOver);
	_repeatTrack->setRippleColorOverride(repeating ? nullptr : &st::mediaPlayerRepeatDisabledRippleBg);
}

void Widget::updatePlaybackSpeedIcon() {
	const auto doubled = Global::VoiceMsgPlaybackDoubled();
	const auto isDefaultSpeed = !doubled;
	_playbackSpeed->setIconOverride(
		isDefaultSpeed ? &st::mediaPlayerSpeedDisabledIcon : nullptr,
		isDefaultSpeed ? &st::mediaPlayerSpeedDisabledIconOver : nullptr);
	_playbackSpeed->setRippleColorOverride(
		isDefaultSpeed ? &st::mediaPlayerSpeedDisabledRippleBg : nullptr);
}

void Widget::checkForTypeChange() {
	auto hasActiveType = [](AudioMsgId::Type type) {
		auto current = instance()->current(type);
		auto state = mixer()->currentState(type);
		return (current == state.id && !IsStoppedOrStopping(state.state));
	};
	if (hasActiveType(AudioMsgId::Type::Voice)) {
		_voiceIsActive = true;
		setType(AudioMsgId::Type::Voice);
	} else if (!_voiceIsActive && hasActiveType(AudioMsgId::Type::Song)) {
		setType(AudioMsgId::Type::Song);
	}
}

bool Widget::hasPlaybackSpeedControl() const {
#ifndef TDESKTOP_DISABLE_OPENAL_EFFECTS
	return (_type == AudioMsgId::Type::Voice);
#else // TDESKTOP_DISABLE_OPENAL_EFFECTS
	return false;
#endif // TDESKTOP_DISABLE_OPENAL_EFFECTS
}

void Widget::setType(AudioMsgId::Type type) {
	if (_type != type) {
		_type = type;
		_repeatTrack->setVisible(_type == AudioMsgId::Type::Song);
		_volumeToggle->setVisible(_type == AudioMsgId::Type::Song);
		_playbackSpeed->setVisible(hasPlaybackSpeedControl());
		if (!_shadow->isHidden()) {
			_playbackSlider->setVisible(_type == AudioMsgId::Type::Song);
		}
		updateLabelsGeometry();
		handleSongChange();
		handleSongUpdate(mixer()->currentState(_type));
		updateOverLabelsState(_labelsOver);
		_playlistChangesLifetime = instance()->playlistChanges(
			_type
		) | rpl::start_with_next([=] {
			handlePlaylistUpdate();
		});
		// maybe the type change causes a change of the button layout
		QResizeEvent event = { size(), size() };
		resizeEvent(&event);
	}
}

void Widget::handleSongUpdate(const TrackState &state) {
	checkForTypeChange();
	if (state.id.type() != _type || !state.id.audio()) {
		return;
	}

	if (state.id.audio()->loading()) {
		_playback->updateLoadingState(state.id.audio()->progress());
	} else {
		_playback->updateState(state);
	}

	auto stopped = IsStoppedOrStopping(state.state);
	auto showPause = !stopped && (state.state == State::Playing || state.state == State::Resuming || state.state == State::Starting);
	if (instance()->isSeeking(_type)) {
		showPause = true;
	}
	auto buttonState = [audio = state.id.audio(), showPause] {
		if (audio->loading()) {
			return ButtonState::Cancel;
		} else if (showPause) {
			return ButtonState::Pause;
		}
		return ButtonState::Play;
	};
	_playPause->setState(buttonState());

	updateTimeText(state);
}

void Widget::updateTimeText(const TrackState &state) {
	QString time;
	qint64 position = 0, length = 0, display = 0;
	const auto frequency = state.frequency;
	const auto document = state.id.audio();
	if (!IsStoppedOrStopping(state.state)) {
		display = position = state.position;
		length = state.length;
	} else if (state.length) {
		display = state.length;
	} else if (const auto song = document->song()) {
		display = (song->duration * frequency);
	}

	_lastDurationMs = (state.length * 1000LL) / frequency;

	if (document->loading()) {
		_time = QString::number(qRound(document->progress() * 100)) + '%';
		_playbackSlider->setDisabled(true);
	} else {
		display = display / frequency;
		_time = formatDurationText(display);
		_playbackSlider->setDisabled(false);
	}
	if (_seekPositionMs < 0) {
		updateTimeLabel();
	}
}

void Widget::updateTimeLabel() {
	auto timeLabelWidth = _timeLabel->width();
	if (_seekPositionMs >= 0) {
		auto playAlready = _seekPositionMs / 1000LL;
		_timeLabel->setText(formatDurationText(playAlready));
	} else {
		_timeLabel->setText(_time);
	}
	if (timeLabelWidth != _timeLabel->width()) {
		updateLabelsGeometry();
	}
}

void Widget::handleSongChange() {
	const auto current = instance()->current(_type);
	const auto document = current.audio();
	if (!current || !document) {
		return;
	}

	TextWithEntities textWithEntities;
	if (document->isVoiceMessage() || document->isVideoMessage()) {
		if (const auto item = App::histItemById(current.contextId())) {
			const auto name = App::peerName(item->fromOriginal());
			const auto date = [item] {
				const auto parsed = ItemDateTime(item);
				const auto date = parsed.date();
				const auto time = parsed.time().toString(cTimeFormat());
				const auto today = QDateTime::currentDateTime().date();
				if (date == today) {
					return lng_player_message_today(lt_time, time);
				} else if (date.addDays(1) == today) {
					return lng_player_message_yesterday(lt_time, time);
				}
				return lng_player_message_date(
					lt_date,
					langDayOfMonthFull(date),
					lt_time,
					time);
			};

			textWithEntities.text = name + ' ' + date();
			textWithEntities.entities.append(EntityInText(
				EntityInTextBold,
				0,
				name.size(),
				QString()));
		} else {
			textWithEntities.text = lang(lng_media_audio);
		}
	} else {
		const auto song = document->song();
		if (!song || song->performer.isEmpty()) {
			textWithEntities.text = (!song || song->title.isEmpty())
				? (document->filename().isEmpty()
					? qsl("Unknown Track")
					: document->filename())
				: song->title;
		} else {
			auto title = song->title.isEmpty()
				? qsl("Unknown Track")
				: TextUtilities::Clean(song->title);
			auto dash = QString::fromUtf8(" \xe2\x80\x93 ");
			textWithEntities.text = song->performer + dash + title;
			textWithEntities.entities.append({ EntityInTextBold, 0, song->performer.size(), QString() });
		}
	}
	_nameLabel->setMarkedText(textWithEntities);

	handlePlaylistUpdate();
}

void Widget::handlePlaylistUpdate() {
	const auto previousEnabled = instance()->previousAvailable(_type);
	const auto nextEnabled = instance()->nextAvailable(_type);
	if (!previousEnabled && !nextEnabled) {
		destroyPrevNextButtons();
	} else {
		createPrevNextButtons();
		_previousTrack->setIconOverride(previousEnabled ? nullptr : &st::mediaPlayerPreviousDisabledIcon);
		_previousTrack->setRippleColorOverride(previousEnabled ? nullptr : &st::mediaPlayerBg);
		_previousTrack->setCursor(previousEnabled ? style::cur_pointer : style::cur_default);
		_nextTrack->setIconOverride(nextEnabled ? nullptr : &st::mediaPlayerNextDisabledIcon);
		_nextTrack->setRippleColorOverride(nextEnabled ? nullptr : &st::mediaPlayerBg);
		_nextTrack->setCursor(nextEnabled ? style::cur_pointer : style::cur_default);
	}
}

void Widget::createPrevNextButtons() {
	if (!_previousTrack) {
		_previousTrack.create(this, st::mediaPlayerPreviousButton);
		_previousTrack->show();
		_previousTrack->setClickedCallback([=]() {
			instance()->previous();
		});
		_nextTrack.create(this, st::mediaPlayerNextButton);
		_nextTrack->show();
		_nextTrack->setClickedCallback([=]() {
			instance()->next();
		});
		updatePlayPrevNextPositions();
	}
}

void Widget::destroyPrevNextButtons() {
	if (_previousTrack) {
		_previousTrack.destroy();
		_nextTrack.destroy();
		updatePlayPrevNextPositions();
	}
}

} // namespace Player
} // namespace Media
