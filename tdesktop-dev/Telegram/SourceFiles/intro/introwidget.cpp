/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/introwidget.h"

#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "lang/lang_file_parser.h"
#include "intro/introstart.h"
#include "intro/introphone.h"
#include "intro/introcode.h"
#include "intro/introsignup.h"
#include "intro/intropwdcheck.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "mainwindow.h"
#include "messenger.h"
#include "application.h"
#include "boxes/confirm_box.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/effects/slide_animation.h"
#include "core/update_checker.h"
#include "window/window_slide_animation.h"
#include "window/window_connecting_widget.h"
#include "window/window_lock_widgets.h"
#include "styles/style_boxes.h"
#include "styles/style_intro.h"
#include "styles/style_window.h"
#include "window/themes/window_theme.h"
#include "lang/lang_cloud_manager.h"
#include "auth_session.h"

namespace Intro {
namespace {

constexpr str_const kDefaultCountry = "US";

void PrepareSupportMode() {
	anim::SetDisabled(true);
	Local::writeSettings();

	Global::SetDesktopNotify(false);
	Global::SetSoundNotify(false);
	cSetAutoDownloadAudio(dbiadNoPrivate | dbiadNoGroups);
	cSetAutoDownloadGif(dbiadNoPrivate | dbiadNoGroups);
	cSetAutoDownloadPhoto(dbiadNoPrivate | dbiadNoGroups);
	cSetAutoPlayGif(false);
	Local::writeUserSettings();
}

} // namespace

Widget::Widget(QWidget *parent) : RpWidget(parent)
, _back(this, object_ptr<Ui::IconButton>(this, st::introBackButton))
, _settings(
	this,
	object_ptr<Ui::RoundButton>(
		this,
		langFactory(lng_menu_settings),
		st::defaultBoxButton))
, _next(this, Fn<QString()>(), st::introNextButton) {
	auto country = Platform::SystemCountry();
	if (country.isEmpty()) {
		country = str_const_toString(kDefaultCountry);
	}
	getData()->country = country;

	_back->entity()->setClickedCallback([this] { historyMove(Direction::Back); });
	_back->hide(anim::type::instant);

	_next->setClickedCallback([this] { getStep()->submit(); });

	_settings->entity()->setClickedCallback([] { App::wnd()->showSettings(); });

	getNearestDC();
	setupConnectingWidget();

	appendStep(new StartWidget(this, getData()));
	fixOrder();

	subscribe(Lang::CurrentCloudManager().firstLanguageSuggestion(), [this] { createLanguageLink(); });
	createLanguageLink();
	if (_changeLanguage) _changeLanguage->finishAnimating();

	subscribe(Lang::Current().updated(), [this] { refreshLang(); });

	show();
	showControls();
	getStep()->showFast();

	cSetPasswordRecovered(false);

	if (!Core::UpdaterDisabled()) {
		Core::UpdateChecker checker;
		checker.isLatest() | rpl::start_with_next([=] {
			onCheckUpdateStatus();
		}, lifetime());
		checker.failed() | rpl::start_with_next([=] {
			onCheckUpdateStatus();
		}, lifetime());
		checker.ready() | rpl::start_with_next([=] {
			onCheckUpdateStatus();
		}, lifetime());
		checker.start();
		onCheckUpdateStatus();
	}
}

void Widget::setupConnectingWidget() {
	_connecting = Window::ConnectingWidget::CreateDefaultWidget(
		this,
		rpl::single(true));
}

void Widget::refreshLang() {
	_changeLanguage.destroy();
	createLanguageLink();
	InvokeQueued(this, [this] { updateControlsGeometry(); });
}

void Widget::createLanguageLink() {
	if (_changeLanguage) return;

	auto createLink = [this](const QString &text, const QString &languageId) {
		_changeLanguage.create(
			this,
			object_ptr<Ui::LinkButton>(this, text));
		_changeLanguage->hide(anim::type::instant);
		_changeLanguage->entity()->setClickedCallback([=] {
			Lang::CurrentCloudManager().switchToLanguage(languageId);
		});
		_changeLanguage->toggle(
			!_resetAccount && !_terms,
			anim::type::normal);
		updateControlsGeometry();
	};

	const auto currentId = Lang::LanguageIdOrDefault(Lang::Current().id());
	const auto defaultId = Lang::DefaultLanguageId();
	const auto suggested = Lang::CurrentCloudManager().suggestedLanguage();
	if (currentId != defaultId) {
		createLink(Lang::GetOriginalValue(lng_switch_to_this), defaultId);
	} else if (!suggested.isEmpty() && suggested != currentId) {
		request(MTPlangpack_GetStrings(
			MTP_string(Lang::CloudLangPackName()),
			MTP_string(suggested),
			MTP_vector<MTPstring>(1, MTP_string("lng_switch_to_this"))
		)).done([=](const MTPVector<MTPLangPackString> &result) {
			auto strings = Lang::Instance::ParseStrings(result);
			auto it = strings.find(lng_switch_to_this);
			if (it != strings.end()) {
				createLink(it->second, suggested);
			}
		}).send();
	}
}

void Widget::onCheckUpdateStatus() {
	Expects(!Core::UpdaterDisabled());

	if (Core::UpdateChecker().state() == Core::UpdateChecker::State::Ready) {
		if (_update) return;
		_update.create(
			this,
			object_ptr<Ui::RoundButton>(
				this,
				langFactory(lng_menu_update),
				st::defaultBoxButton));
		if (!_a_show.animating()) {
			_update->setVisible(true);
		}
		const auto stepHasCover = getStep()->hasCover();
		_update->toggle(!stepHasCover, anim::type::instant);
		_update->entity()->setClickedCallback([] {
			Core::checkReadyUpdate();
			App::restart();
		});
	} else {
		if (!_update) return;
		_update.destroy();
	}
	updateControlsGeometry();
}

void Widget::setInnerFocus() {
	if (getStep()->animating()) {
		setFocus();
	} else {
		getStep()->setInnerFocus();
	}
}

void Widget::historyMove(Direction direction) {
	if (getStep()->animating()) return;

	Assert(_stepHistory.size() > 1);

	auto wasStep = getStep((direction == Direction::Back) ? 0 : 1);
	if (direction == Direction::Back) {
		_stepHistory.pop_back();
		wasStep->cancelled();
	} else if (direction == Direction::Replace) {
		_stepHistory.removeAt(_stepHistory.size() - 2);
	}

	if (_resetAccount) {
		hideAndDestroy(std::exchange(_resetAccount, { nullptr }));
	}
	if (_terms) {
		hideAndDestroy(std::exchange(_terms, { nullptr }));
	}

	getStep()->finishInit();
	getStep()->prepareShowAnimated(wasStep);
	if (wasStep->hasCover() != getStep()->hasCover()) {
		_nextTopFrom = wasStep->contentTop() + st::introStepHeight;
		_controlsTopFrom = wasStep->hasCover() ? st::introCoverHeight : 0;
		_coverShownAnimation.start([this] { updateControlsGeometry(); }, 0., 1., st::introCoverDuration, wasStep->hasCover() ? anim::linear : anim::easeOutCirc);
	}

	if (direction == Direction::Forward || direction == Direction::Replace) {
		wasStep->finished();
	}
	if (direction == Direction::Back || direction == Direction::Replace) {
		delete base::take(wasStep);
	}
	_back->toggle(getStep()->hasBack(), anim::type::normal);

	auto stepHasCover = getStep()->hasCover();
	_settings->toggle(!stepHasCover, anim::type::normal);
	if (_update) {
		_update->toggle(!stepHasCover, anim::type::normal);
	}
	_next->setText([this] { return getStep()->nextButtonText(); });
	if (_resetAccount) _resetAccount->show(anim::type::normal);
	if (_terms) _terms->show(anim::type::normal);
	if (_changeLanguage) {
		_changeLanguage->toggle(
			!_resetAccount && !_terms,
			anim::type::normal);
	}
	getStep()->showAnimated(direction);
	fixOrder();
}

void Widget::hideAndDestroy(object_ptr<Ui::FadeWrap<Ui::RpWidget>> widget) {
	const auto weak = make_weak(widget.data());
	widget->hide(anim::type::normal);
	widget->shownValue(
	) | rpl::start_with_next([=](bool shown) {
		if (!shown && weak) {
			weak->deleteLater();
		}
	}, widget->lifetime());
}

void Widget::fixOrder() {
	_next->raise();
	if (_update) _update->raise();
	_settings->raise();
	_back->raise();
	_connecting->raise();
}

void Widget::moveToStep(Step *step, Direction direction) {
	appendStep(step);
	_back->raise();
	_settings->raise();
	if (_update) {
		_update->raise();
	}
	_connecting->raise();

	historyMove(direction);
}

void Widget::appendStep(Step *step) {
	_stepHistory.push_back(step);
	step->setGeometry(calculateStepRect());
	step->setGoCallback([=](Step *step, Direction direction) {
		if (direction == Direction::Back) {
			historyMove(direction);
		} else {
			moveToStep(step, direction);
		}
	});
	step->setShowResetCallback([=] {
		showResetButton();
	});
	step->setShowTermsCallback([=]() {
		showTerms();
	});
	step->setAcceptTermsCallback([=](Fn<void()> callback) {
		acceptTerms(callback);
	});
}

void Widget::showResetButton() {
	if (!_resetAccount) {
		auto entity = object_ptr<Ui::RoundButton>(this, langFactory(lng_signin_reset_account), st::introResetButton);
		_resetAccount.create(this, std::move(entity));
		_resetAccount->hide(anim::type::instant);
		_resetAccount->entity()->setClickedCallback([this] { resetAccount(); });
		updateControlsGeometry();
	}
	_resetAccount->show(anim::type::normal);
	if (_changeLanguage) {
		_changeLanguage->hide(anim::type::normal);
	}
}

void Widget::showTerms() {
	if (getData()->termsLock.text.text.isEmpty()) {
		_terms.destroy();
	} else if (!_terms) {
		auto entity = object_ptr<Ui::FlatLabel>(
			this,
			lng_terms_signup(
				lt_link,
				textcmdLink(1, lang(lng_terms_signup_link))),
			Ui::FlatLabel::InitType::Rich,
			st::introTermsLabel);
		_terms.create(this, std::move(entity));
		_terms->entity()->setLink(
			1,
			std::make_shared<LambdaClickHandler>([=] {
				showTerms(nullptr);
			}));
		updateControlsGeometry();
		_terms->hide(anim::type::instant);
	}
	if (_changeLanguage) {
		_changeLanguage->toggle(
			!_terms && !_resetAccount,
			anim::type::normal);
	}
}

void Widget::acceptTerms(Fn<void()> callback) {
	showTerms(callback);
}

void Widget::resetAccount() {
	if (_resetRequest) return;

	Ui::show(Box<ConfirmBox>(lang(lng_signin_sure_reset), lang(lng_signin_reset), st::attentionBoxButton, crl::guard(this, [this] {
		if (_resetRequest) return;
		_resetRequest = request(MTPaccount_DeleteAccount(MTP_string("Forgot password"))).done([this](const MTPBool &result) {
			_resetRequest = 0;

			Ui::hideLayer();
			moveToStep(new SignupWidget(this, getData()), Direction::Replace);
		}).fail([this](const RPCError &error) {
			_resetRequest = 0;

			auto type = error.type();
			if (type.startsWith(qstr("2FA_CONFIRM_WAIT_"))) {
				auto seconds = type.mid(qstr("2FA_CONFIRM_WAIT_").size()).toInt();
				auto days = (seconds + 59) / 86400;
				auto hours = ((seconds + 59) % 86400) / 3600;
				auto minutes = ((seconds + 59) % 3600) / 60;
				auto when = lng_signin_reset_minutes(lt_count, minutes);
				if (days > 0) {
					auto daysCount = lng_signin_reset_days(lt_count, days);
					auto hoursCount = lng_signin_reset_hours(lt_count, hours);
					when = lng_signin_reset_in_days(lt_days_count, daysCount, lt_hours_count, hoursCount, lt_minutes_count, when);
				} else if (hours > 0) {
					auto hoursCount = lng_signin_reset_hours(lt_count, hours);
					when = lng_signin_reset_in_hours(lt_hours_count, hoursCount, lt_minutes_count, when);
				}
				Ui::show(Box<InformBox>(lng_signin_reset_wait(lt_phone_number, App::formatPhone(getData()->phone), lt_when, when)));
			} else if (type == qstr("2FA_RECENT_CONFIRM")) {
				Ui::show(Box<InformBox>(lang(lng_signin_reset_cancelled)));
			} else {
				Ui::hideLayer();
				getStep()->showError(&Lang::Hard::ServerError);
			}
		}).send();
	})));
}

void Widget::getNearestDC() {
	request(MTPhelp_GetNearestDc()).done([this](const MTPNearestDc &result) {
		auto &nearest = result.c_nearestDc();
		DEBUG_LOG(("Got nearest dc, country: %1, nearest: %2, this: %3"
			).arg(qs(nearest.vcountry)
			).arg(nearest.vnearest_dc.v
			).arg(nearest.vthis_dc.v));
		Messenger::Instance().suggestMainDcId(nearest.vnearest_dc.v);
		auto nearestCountry = qs(nearest.vcountry);
		if (getData()->country != nearestCountry) {
			getData()->country = nearestCountry;
			getData()->updated.notify();
		}
	}).send();
}

void Widget::showTerms(Fn<void()> callback) {
	if (getData()->termsLock.text.text.isEmpty()) {
		return;
	}
	const auto weak = make_weak(this);
	const auto box = Ui::show(callback
		? Box<Window::TermsBox>(
			getData()->termsLock,
			langFactory(lng_terms_agree),
			langFactory(lng_terms_decline))
		: Box<Window::TermsBox>(
			getData()->termsLock.text,
			langFactory(lng_box_ok),
			nullptr));

	box->setCloseByEscape(false);
	box->setCloseByOutsideClick(false);

	box->agreeClicks(
	) | rpl::start_with_next([=] {
		if (callback) {
			callback();
		}
		if (box) {
			box->closeBox();
		}
	}, box->lifetime());

	box->cancelClicks(
	) | rpl::start_with_next([=] {
		const auto box = Ui::show(Box<Window::TermsBox>(
			TextWithEntities{ lang(lng_terms_signup_sorry) },
			langFactory(lng_intro_finish),
			langFactory(lng_terms_decline)));
		box->agreeClicks(
		) | rpl::start_with_next([=] {
			if (weak) {
				showTerms(callback);
			}
		}, box->lifetime());
		box->cancelClicks(
		) | rpl::start_with_next([=] {
			if (box) {
				box->closeBox();
			}
		}, box->lifetime());
	}, box->lifetime());
}

void Widget::showControls() {
	getStep()->show();
	_next->show();
	_next->setText([this] { return getStep()->nextButtonText(); });
	_connecting->setForceHidden(false);
	auto hasCover = getStep()->hasCover();
	_settings->toggle(!hasCover, anim::type::instant);
	if (_update) {
		_update->toggle(!hasCover, anim::type::instant);
	}
	if (_changeLanguage) {
		_changeLanguage->toggle(
			!_resetAccount && !_terms,
			anim::type::instant);
	}
	if (_terms) {
		_terms->show(anim::type::instant);
	}
	_back->toggle(getStep()->hasBack(), anim::type::instant);
}

void Widget::hideControls() {
	getStep()->hide();
	_next->hide();
	_connecting->setForceHidden(true);
	_settings->hide(anim::type::instant);
	if (_update) _update->hide(anim::type::instant);
	if (_changeLanguage) _changeLanguage->hide(anim::type::instant);
	if (_terms) _terms->hide(anim::type::instant);
	_back->hide(anim::type::instant);
}

void Widget::showAnimated(const QPixmap &bgAnimCache, bool back) {
	_showBack = back;

	(_showBack ? _cacheOver : _cacheUnder) = bgAnimCache;

	_a_show.finish();
	showControls();
	(_showBack ? _cacheUnder : _cacheOver) = Ui::GrabWidget(this);
	hideControls();

	_a_show.start([this] { animationCallback(); }, 0., 1., st::slideDuration, Window::SlideAnimation::transition());

	show();
}

void Widget::animationCallback() {
	update();
	if (!_a_show.animating()) {
		_cacheUnder = _cacheOver = QPixmap();

		showControls();
		getStep()->activate();
	}
}

void Widget::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());
	setMouseTracking(true);

	if (_coverShownAnimation.animating()) {
		_coverShownAnimation.step(getms());
	}

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	p.fillRect(e->rect(), st::windowBg);
	auto progress = _a_show.current(getms(), 1.);
	if (_a_show.animating()) {
		auto coordUnder = _showBack ? anim::interpolate(-st::slideShift, 0, progress) : anim::interpolate(0, -st::slideShift, progress);
		auto coordOver = _showBack ? anim::interpolate(0, width(), progress) : anim::interpolate(width(), 0, progress);
		auto shadow = _showBack ? (1. - progress) : progress;
		if (coordOver > 0) {
			p.drawPixmap(QRect(0, 0, coordOver, height()), _cacheUnder, QRect(-coordUnder * cRetinaFactor(), 0, coordOver * cRetinaFactor(), height() * cRetinaFactor()));
			p.setOpacity(shadow);
			p.fillRect(0, 0, coordOver, height(), st::slideFadeOutBg);
			p.setOpacity(1);
		}
		p.drawPixmap(coordOver, 0, _cacheOver);
		p.setOpacity(shadow);
		st::slideShadow.fill(p, QRect(coordOver - st::slideShadow.width(), 0, st::slideShadow.width(), height()));
	}
}

QRect Widget::calculateStepRect() const {
	auto stepInnerTop = (height() - st::introHeight) / 2;
	accumulate_max(stepInnerTop, st::introStepTopMin);
	auto nextTop = stepInnerTop + st::introStepHeight;
	auto additionalHeight = st::introStepHeightAdd;
	auto stepWidth = width();
	auto stepHeight = nextTop + additionalHeight;
	return QRect(0, 0, stepWidth, stepHeight);
}

void Widget::resizeEvent(QResizeEvent *e) {
	auto stepRect = calculateStepRect();
	for_const (auto step, _stepHistory) {
		step->setGeometry(stepRect);
	}

	updateControlsGeometry();
}

void Widget::updateControlsGeometry() {
	auto shown = _coverShownAnimation.current(1.);

	auto controlsTopTo = getStep()->hasCover() ? st::introCoverHeight : 0;
	auto controlsTop = anim::interpolate(_controlsTopFrom, controlsTopTo, shown);
	_settings->moveToRight(st::introSettingsSkip, controlsTop + st::introSettingsSkip);
	if (_update) {
		_update->moveToRight(st::introSettingsSkip + _settings->width() + st::introSettingsSkip, _settings->y());
	}
	_back->moveToLeft(0, controlsTop);

	auto nextTopTo = getStep()->contentTop() + st::introStepHeight;
	auto nextTop = anim::interpolate(_nextTopFrom, nextTopTo, shown);
	_next->moveToLeft((width() - _next->width()) / 2, nextTop);
	if (_changeLanguage) {
		_changeLanguage->moveToLeft((width() - _changeLanguage->width()) / 2, _next->y() + _next->height() + _changeLanguage->height());
	}
	if (_resetAccount) {
		_resetAccount->moveToLeft((width() - _resetAccount->width()) / 2, height() - st::introResetBottom - _resetAccount->height());
	}
	if (_terms) {
		_terms->moveToLeft((width() - _terms->width()) / 2, height() - st::introTermsBottom - _terms->height());
	}
}


void Widget::keyPressEvent(QKeyEvent *e) {
	if (_a_show.animating() || getStep()->animating()) return;

	if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		if (getStep()->hasBack()) {
			historyMove(Direction::Back);
		}
	} else if (e->key() == Qt::Key_Enter
		|| e->key() == Qt::Key_Return
		|| e->key() == Qt::Key_Space) {
		getStep()->submit();
	}
}

Widget::~Widget() {
	for (auto step : base::take(_stepHistory)) {
		delete step;
	}
	if (App::wnd()) App::wnd()->noIntro(this);
}

QString Widget::Step::nextButtonText() const {
	return lang(lng_intro_next);
}

void Widget::Step::finish(const MTPUser &user, QImage &&photo) {
	if (user.type() != mtpc_user
		|| !user.c_user().is_self()
		|| !user.c_user().vid.v) {
		// No idea what to do here.
		// We could've reset intro and MTP, but this really should not happen.
		Ui::show(Box<InformBox>("Internal error: bad user.is_self() after sign in."));
		return;
	}

	// Save the default language if we've suggested some other and user ignored it.
	const auto currentId = Lang::Current().id();
	const auto defaultId = Lang::DefaultLanguageId();
	const auto suggested = Lang::CurrentCloudManager().suggestedLanguage();
	if (currentId.isEmpty() && !suggested.isEmpty() && suggested != defaultId) {
		Lang::Current().switchToId(Lang::DefaultLanguage());
		Local::writeLangPack();
	}

	Messenger::Instance().authSessionCreate(user);
	Local::writeMtpData();
	App::wnd()->setupMain();

	// "this" is already deleted here by creating the main widget.
	if (AuthSession::Exists()) {
		if (!photo.isNull()) {
			Auth().api().uploadPeerPhoto(Auth().user(), std::move(photo));
		}
		if (Auth().supportMode()) {
			PrepareSupportMode();
		}
	}
}

void Widget::Step::paintEvent(QPaintEvent *e) {
	Painter p(this);
	paintAnimated(p, e->rect());
}

void Widget::Step::resizeEvent(QResizeEvent *e) {
	updateLabelsPosition();
}

void Widget::Step::updateLabelsPosition() {
	Ui::SendPendingMoveResizeEvents(_description->entity());
	if (hasCover()) {
		_title->moveToLeft((width() - _title->width()) / 2, contentTop() + st::introCoverTitleTop);
		_description->moveToLeft((width() - _description->width()) / 2, contentTop() + st::introCoverDescriptionTop);
	} else {
		_title->moveToLeft(contentLeft() + st::buttonRadius, contentTop() + st::introTitleTop);
		_description->resizeToWidth(st::introDescription.minWidth);
		_description->moveToLeft(contentLeft() + st::buttonRadius, contentTop() + st::introDescriptionTop);
	}
	if (_error) {
		if (_errorCentered) {
			_error->entity()->resizeToWidth(width());
		}
		Ui::SendPendingMoveResizeEvents(_error->entity());
		auto errorLeft = _errorCentered ? 0 : (contentLeft() + st::buttonRadius);
		auto errorTop = contentTop() + (_errorBelowLink ? st::introErrorBelowLinkTop : st::introErrorTop);
		_error->moveToLeft(errorLeft, errorTop);
	}
}

void Widget::Step::setTitleText(Fn<QString()> richTitleTextFactory) {
	_titleTextFactory = std::move(richTitleTextFactory);
	refreshTitle();
	updateLabelsPosition();
}

void Widget::Step::refreshTitle() {
	_title->setRichText(_titleTextFactory());
}

void Widget::Step::setDescriptionText(Fn<QString()> richDescriptionTextFactory) {
	_descriptionTextFactory = std::move(richDescriptionTextFactory);
	refreshDescription();
	updateLabelsPosition();
}

void Widget::Step::refreshDescription() {
	_description->entity()->setRichText(_descriptionTextFactory());
}

void Widget::Step::refreshLang() {
	refreshTitle();
	refreshDescription();
	refreshError();
	updateLabelsPosition();
}

void Widget::Step::showFinished() {
	_a_show.finish();
	_coverAnimation = CoverAnimation();
	_slideAnimation.reset();
	prepareCoverMask();
	activate();
}

bool Widget::Step::paintAnimated(Painter &p, QRect clip) {
	if (_slideAnimation) {
		_slideAnimation->paintFrame(p, (width() - st::introStepWidth) / 2, contentTop(), width(), getms());
		if (!_slideAnimation->animating()) {
			showFinished();
			return false;
		}
		return true;
	}

	auto dt = _a_show.current(getms(), 1.);
	if (!_a_show.animating()) {
		if (hasCover()) {
			paintCover(p, 0);
		}
		if (_coverAnimation.title) {
			showFinished();
		}
		if (!QRect(0, contentTop(), width(), st::introStepHeight).intersects(clip)) {
			return true;
		}
		return false;
	}

	auto progress = (hasCover() ? anim::easeOutCirc(1., dt) : anim::linear(1., dt));
	auto arrivingAlpha = progress;
	auto departingAlpha = 1. - progress;
	auto showCoverMethod = progress;
	auto hideCoverMethod = progress;
	auto coverTop = (hasCover() ? anim::interpolate(-st::introCoverHeight, 0, showCoverMethod) : anim::interpolate(0, -st::introCoverHeight, hideCoverMethod));

	paintCover(p, coverTop);

	auto positionReady = hasCover() ? showCoverMethod : hideCoverMethod;
	_coverAnimation.title->paintFrame(p, positionReady, departingAlpha, arrivingAlpha);
	_coverAnimation.description->paintFrame(p, positionReady, departingAlpha, arrivingAlpha);

	paintContentSnapshot(p, _coverAnimation.contentSnapshotWas, departingAlpha, showCoverMethod);
	paintContentSnapshot(p, _coverAnimation.contentSnapshotNow, arrivingAlpha, 1. - hideCoverMethod);

	return true;
}

void Widget::Step::fillSentCodeData(const MTPDauth_sentCode &data) {
	if (data.has_terms_of_service()) {
		const auto &terms = data.vterms_of_service.c_help_termsOfService();
		getData()->termsLock = Window::TermsLock::FromMTP(terms);
	} else {
		getData()->termsLock = Window::TermsLock();
	}

	const auto &type = data.vtype;
	switch (type.type()) {
	case mtpc_auth_sentCodeTypeApp: {
		getData()->codeByTelegram = true;
		getData()->codeLength = type.c_auth_sentCodeTypeApp().vlength.v;
	} break;
	case mtpc_auth_sentCodeTypeSms: {
		getData()->codeByTelegram = false;
		getData()->codeLength = type.c_auth_sentCodeTypeSms().vlength.v;
	} break;
	case mtpc_auth_sentCodeTypeCall: {
		getData()->codeByTelegram = false;
		getData()->codeLength = type.c_auth_sentCodeTypeCall().vlength.v;
	} break;
	case mtpc_auth_sentCodeTypeFlashCall: LOG(("Error: should not be flashcall!")); break;
	}
}

void Widget::Step::showDescription() {
	_description->show(anim::type::normal);
}

void Widget::Step::hideDescription() {
	_description->hide(anim::type::normal);
}

void Widget::Step::paintContentSnapshot(Painter &p, const QPixmap &snapshot, float64 alpha, float64 howMuchHidden) {
	if (!snapshot.isNull()) {
		auto contentTop = anim::interpolate(height() - (snapshot.height() / cIntRetinaFactor()), height(), howMuchHidden);
		if (contentTop < height()) {
			p.setOpacity(alpha);
			p.drawPixmap(QPoint(contentLeft(), contentTop), snapshot, QRect(0, 0, snapshot.width(), (height() - contentTop) * cIntRetinaFactor()));
		}
	}
}

void Widget::Step::prepareCoverMask() {
	if (!_coverMask.isNull()) return;

	auto maskWidth = cIntRetinaFactor();
	auto maskHeight = st::introCoverHeight * cIntRetinaFactor();
	auto mask = QImage(maskWidth, maskHeight, QImage::Format_ARGB32_Premultiplied);
	auto maskInts = reinterpret_cast<uint32*>(mask.bits());
	Assert(mask.depth() == (sizeof(uint32) << 3));
	auto maskIntsPerLineAdded = (mask.bytesPerLine() >> 2) - maskWidth;
	Assert(maskIntsPerLineAdded >= 0);
	auto realHeight = static_cast<float64>(maskHeight - 1);
	for (auto y = 0; y != maskHeight; ++y) {
		auto color = anim::color(st::introCoverTopBg, st::introCoverBottomBg, y / realHeight);
		auto colorInt = anim::getPremultiplied(color);
		for (auto x = 0; x != maskWidth; ++x) {
			*maskInts++ = colorInt;
		}
		maskInts += maskIntsPerLineAdded;
	}
	_coverMask = App::pixmapFromImageInPlace(std::move(mask));
}

void Widget::Step::paintCover(Painter &p, int top) {
	auto coverHeight = top + st::introCoverHeight;
	if (coverHeight > 0) {
		p.drawPixmap(QRect(0, 0, width(), coverHeight), _coverMask, QRect(0, -top * cIntRetinaFactor(), _coverMask.width(), coverHeight * cIntRetinaFactor()));
	}

	auto left = 0;
	auto right = 0;
	if (width() < st::introCoverMaxWidth) {
		auto iconsMaxSkip = st::introCoverMaxWidth - st::introCoverLeft.width() - st::introCoverRight.width();
		auto iconsSkip = st::introCoverIconsMinSkip + (iconsMaxSkip - st::introCoverIconsMinSkip) * (width() - st::introStepWidth) / (st::introCoverMaxWidth - st::introStepWidth);
		auto outside = iconsSkip + st::introCoverLeft.width() + st::introCoverRight.width() - width();
		left = -outside / 2;
		right = -outside - left;
	}
	if (top < 0) {
		auto shown = float64(coverHeight) / st::introCoverHeight;
		auto leftShown = qRound(shown * (left + st::introCoverLeft.width()));
		left = leftShown - st::introCoverLeft.width();
		auto rightShown = qRound(shown * (right + st::introCoverRight.width()));
		right = rightShown - st::introCoverRight.width();
	}
	st::introCoverLeft.paint(p, left, coverHeight - st::introCoverLeft.height(), width());
	st::introCoverRight.paint(p, width() - right - st::introCoverRight.width(), coverHeight - st::introCoverRight.height(), width());

	auto planeLeft = (width() - st::introCoverIcon.width()) / 2 - st::introCoverIconLeft;
	auto planeTop = top + st::introCoverIconTop;
	if (top < 0 && !_hasCover) {
		auto deltaLeft = -qRound(float64(st::introPlaneWidth / st::introPlaneHeight) * top);
//		auto deltaTop = top;
		planeLeft += deltaLeft;
	//	planeTop += top;
	}
	st::introCoverIcon.paint(p, planeLeft, planeTop, width());
}

int Widget::Step::contentLeft() const {
	return (width() - st::introNextButton.width) / 2;
}

int Widget::Step::contentTop() const {
	auto result = height() - st::introStepHeight - st::introStepHeightAdd;
	if (_hasCover) {
		auto added = 1. - snap(float64(height() - st::windowMinHeight) / (st::introStepHeightFull - st::windowMinHeight), 0., 1.);
		result += qRound(added * st::introStepHeightAdd);
	}
	return result;
}

void Widget::Step::setErrorCentered(bool centered) {
	_errorCentered = centered;
	_error.destroy();
}

void Widget::Step::setErrorBelowLink(bool below) {
	_errorBelowLink = below;
	if (_error) {
		updateLabelsPosition();
	}
}

void Widget::Step::showError(Fn<QString()> textFactory) {
	_errorTextFactory = std::move(textFactory);
	refreshError();
	updateLabelsPosition();
}

void Widget::Step::refreshError() {
	if (!_errorTextFactory) {
		if (_error) _error->hide(anim::type::normal);
	} else {
		if (!_error) {
			_error.create(
				this,
				object_ptr<Ui::FlatLabel>(
					this,
					_errorCentered
						? st::introErrorCentered
						: st::introError));
			_error->hide(anim::type::instant);
		}
		_error->entity()->setText(_errorTextFactory());
		updateLabelsPosition();
		_error->show(anim::type::normal);
	}
}

Widget::Step::Step(QWidget *parent, Data *data, bool hasCover) : TWidget(parent)
, _data(data)
, _hasCover(hasCover)
, _title(this, _hasCover ? st::introCoverTitle : st::introTitle)
, _description(
	this,
	object_ptr<Ui::FlatLabel>(
		this,
		_hasCover
			? st::introCoverDescription
			: st::introDescription)) {
	hide();
	subscribe(Window::Theme::Background(), [this](
			const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			if (!_coverMask.isNull()) {
				_coverMask = QPixmap();
				prepareCoverMask();
			}
		}
	});
	subscribe(Lang::Current().updated(), [this] { refreshLang(); });
}

void Widget::Step::prepareShowAnimated(Step *after) {
	setInnerFocus();
	if (hasCover() || after->hasCover()) {
		_coverAnimation = prepareCoverAnimation(after);
		prepareCoverMask();
	} else {
		auto leftSnapshot = after->prepareSlideAnimation();
		auto rightSnapshot = prepareSlideAnimation();
		_slideAnimation = std::make_unique<Ui::SlideAnimation>();
		_slideAnimation->setSnapshots(std::move(leftSnapshot), std::move(rightSnapshot));
		_slideAnimation->setOverflowHidden(false);
	}
}

Widget::Step::CoverAnimation Widget::Step::prepareCoverAnimation(Step *after) {
	auto result = CoverAnimation();
	result.title = Ui::FlatLabel::CrossFade(
		after->_title,
		_title,
		st::introBg);
	result.description = Ui::FlatLabel::CrossFade(
		after->_description->entity(),
		_description->entity(),
		st::introBg,
		after->_description->pos(),
		_description->pos());
	result.contentSnapshotWas = after->prepareContentSnapshot();
	result.contentSnapshotNow = prepareContentSnapshot();
	return result;
}

QPixmap Widget::Step::prepareContentSnapshot() {
	auto otherTop = _description->y() + _description->height();
	auto otherRect = myrtlrect(contentLeft(), otherTop, st::introStepWidth, height() - otherTop);
	return Ui::GrabWidget(this, otherRect);
}

QPixmap Widget::Step::prepareSlideAnimation() {
	auto grabLeft = (width() - st::introStepWidth) / 2;
	auto grabTop = contentTop();
	return Ui::GrabWidget(
		this,
		QRect(grabLeft, grabTop, st::introStepWidth, st::introStepHeight));
}

void Widget::Step::showAnimated(Direction direction) {
	setFocus();
	show();
	hideChildren();
	if (_slideAnimation) {
		auto slideLeft = (direction == Direction::Back);
		_slideAnimation->start(slideLeft, [this] { update(0, contentTop(), width(), st::introStepHeight); }, st::introSlideDuration);
	} else {
		_a_show.start([this] { update(); }, 0., 1., st::introCoverDuration);
	}
}

void Widget::Step::setGoCallback(Fn<void(Step *step, Direction direction)> callback) {
	_goCallback = std::move(callback);
}

void Widget::Step::setShowResetCallback(Fn<void()> callback) {
	_showResetCallback = std::move(callback);
}

void Widget::Step::setShowTermsCallback(Fn<void()> callback) {
	_showTermsCallback = std::move(callback);
}

void Widget::Step::setAcceptTermsCallback(
		Fn<void(Fn<void()> callback)> callback) {
	_acceptTermsCallback = std::move(callback);
}

void Widget::Step::showFast() {
	show();
	showFinished();
}

bool Widget::Step::animating() const {
	return (_slideAnimation && _slideAnimation->animating()) || _a_show.animating();
}

bool Widget::Step::hasCover() const {
	return _hasCover;
}

bool Widget::Step::hasBack() const {
	return false;
}

void Widget::Step::activate() {
	_title->show();
	_description->show(anim::type::instant);
	if (_errorTextFactory) {
		_error->show(anim::type::instant);
	}
}

void Widget::Step::cancelled() {
}

void Widget::Step::finished() {
	hide();
}

Widget::Step::CoverAnimation::~CoverAnimation() = default;

Widget::Step::~Step() = default;

} // namespace Intro
