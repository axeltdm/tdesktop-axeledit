/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/passcode_box.h"

#include "base/bytes.h"
#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "boxes/confirm_phone_box.h"
#include "mainwindow.h"
#include "auth_session.h"
#include "storage/localstorage.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "passport/passport_encryption.h"
#include "passport/passport_panel_edit_contact.h"
#include "styles/style_boxes.h"
#include "styles/style_passport.h"

namespace {

} // namespace

PasscodeBox::PasscodeBox(QWidget*, bool turningOff)
: _turningOff(turningOff)
, _about(st::boxWidth - st::boxPadding.left() * 1.5)
, _oldPasscode(this, st::defaultInputField, langFactory(lng_passcode_enter_old))
, _newPasscode(this, st::defaultInputField, langFactory(Global::LocalPasscode() ? lng_passcode_enter_new : lng_passcode_enter_first))
, _reenterPasscode(this, st::defaultInputField, langFactory(lng_passcode_confirm_new))
, _passwordHint(this, st::defaultInputField, langFactory(lng_cloud_password_hint))
, _recoverEmail(this, st::defaultInputField, langFactory(lng_cloud_password_email))
, _recover(this, lang(lng_signin_recover)) {
}

PasscodeBox::PasscodeBox(
	QWidget*,
	const Core::CloudPasswordCheckRequest &curRequest,
	const Core::CloudPasswordAlgo &newAlgo,
	bool hasRecovery,
	bool notEmptyPassport,
	const QString &hint,
	const Core::SecureSecretAlgo &newSecureSecretAlgo,
	bool turningOff)
: _turningOff(turningOff)
, _cloudPwd(true)
, _curRequest(curRequest)
, _newAlgo(newAlgo)
, _newSecureSecretAlgo(newSecureSecretAlgo)
, _hasRecovery(hasRecovery)
, _notEmptyPassport(notEmptyPassport)
, _about(st::boxWidth - st::boxPadding.left() * 1.5)
, _oldPasscode(this, st::defaultInputField, langFactory(lng_cloud_password_enter_old))
, _newPasscode(this, st::defaultInputField, langFactory(curRequest ? lng_cloud_password_enter_new : lng_cloud_password_enter_first))
, _reenterPasscode(this, st::defaultInputField, langFactory(lng_cloud_password_confirm_new))
, _passwordHint(this, st::defaultInputField, langFactory(curRequest ? lng_cloud_password_change_hint : lng_cloud_password_hint))
, _recoverEmail(this, st::defaultInputField, langFactory(lng_cloud_password_email))
, _recover(this, lang(lng_signin_recover)) {
	Expects(!_turningOff || curRequest);

	if (!hint.isEmpty()) _hintText.setText(st::passcodeTextStyle, lng_signin_hint(lt_password_hint, hint));
}

rpl::producer<QByteArray> PasscodeBox::newPasswordSet() const {
	return _newPasswordSet.events();
}

rpl::producer<> PasscodeBox::passwordReloadNeeded() const {
	return _passwordReloadNeeded.events();
}

rpl::producer<> PasscodeBox::clearUnconfirmedPassword() const {
	return _clearUnconfirmedPassword.events();
}

bool PasscodeBox::currentlyHave() const {
	return _cloudPwd ? (!!_curRequest) : Global::LocalPasscode();
}

void PasscodeBox::prepare() {
	addButton(langFactory(_turningOff ? lng_passcode_remove_button : lng_settings_save), [=] { save(); });
	addButton(langFactory(lng_cancel), [=] { closeBox(); });

	_about.setRichText(st::passcodeTextStyle, lang(_cloudPwd ? lng_cloud_password_about : lng_passcode_about));
	_aboutHeight = _about.countHeight(st::boxWidth - st::boxPadding.left() * 1.5);
	if (_turningOff) {
		_oldPasscode->show();
		setTitle(langFactory(_cloudPwd ? lng_cloud_password_remove : lng_passcode_remove));
		setDimensions(st::boxWidth, st::passcodePadding.top() + _oldPasscode->height() + st::passcodeTextLine + ((_hasRecovery && !_hintText.isEmpty()) ? st::passcodeTextLine : 0) + st::passcodeAboutSkip + _aboutHeight + st::passcodePadding.bottom());
	} else {
		if (currentlyHave()) {
			_oldPasscode->show();
			setTitle(langFactory(_cloudPwd ? lng_cloud_password_change : lng_passcode_change));
			setDimensions(st::boxWidth, st::passcodePadding.top() + _oldPasscode->height() + st::passcodeTextLine + ((_hasRecovery && !_hintText.isEmpty()) ? st::passcodeTextLine : 0) + _newPasscode->height() + st::passcodeLittleSkip + _reenterPasscode->height() + st::passcodeSkip + (_cloudPwd ? _passwordHint->height() + st::passcodeLittleSkip : 0) + st::passcodeAboutSkip + _aboutHeight + st::passcodePadding.bottom());
		} else {
			_oldPasscode->hide();
			setTitle(langFactory(_cloudPwd ? lng_cloud_password_create : lng_passcode_create));
			setDimensions(st::boxWidth, st::passcodePadding.top() + _newPasscode->height() + st::passcodeLittleSkip + _reenterPasscode->height() + st::passcodeSkip + (_cloudPwd ? _passwordHint->height() + st::passcodeLittleSkip : 0) + st::passcodeAboutSkip + _aboutHeight + (_cloudPwd ? (st::passcodeLittleSkip + _recoverEmail->height() + st::passcodeSkip) : st::passcodePadding.bottom()));
		}
	}

	connect(_oldPasscode, &Ui::MaskedInputField::changed, [=] { oldChanged(); });
	connect(_newPasscode, &Ui::MaskedInputField::changed, [=] { newChanged(); });
	connect(_reenterPasscode, &Ui::MaskedInputField::changed, [=] { newChanged(); });
	connect(_passwordHint, &Ui::InputField::changed, [=] { newChanged(); });
	connect(_recoverEmail, &Ui::InputField::changed, [=] { emailChanged(); });

	const auto fieldSubmit = [=] { submit(); };
	connect(_oldPasscode, &Ui::MaskedInputField::submitted, fieldSubmit);
	connect(_newPasscode, &Ui::MaskedInputField::submitted, fieldSubmit);
	connect(_reenterPasscode, &Ui::MaskedInputField::submitted, fieldSubmit);
	connect(_passwordHint, &Ui::InputField::submitted, fieldSubmit);
	connect(_recoverEmail, &Ui::InputField::submitted, fieldSubmit);

	_recover->addClickHandler([=] { recoverByEmail(); });

	const auto has = currentlyHave();
	_oldPasscode->setVisible(_turningOff || has);
	_recover->setVisible((_turningOff || has) && _cloudPwd && _hasRecovery);
	_newPasscode->setVisible(!_turningOff);
	_reenterPasscode->setVisible(!_turningOff);
	_passwordHint->setVisible(!_turningOff && _cloudPwd);
	_recoverEmail->setVisible(!_turningOff && _cloudPwd && !has);
}

void PasscodeBox::submit() {
	const auto has = currentlyHave();
	if (_oldPasscode->hasFocus()) {
		if (_turningOff) {
			save();
		} else {
			_newPasscode->setFocus();
		}
	} else if (_newPasscode->hasFocus()) {
		_reenterPasscode->setFocus();
	} else if (_reenterPasscode->hasFocus()) {
		if (has && _oldPasscode->text().isEmpty()) {
			_oldPasscode->setFocus();
			_oldPasscode->showError();
		} else if (_newPasscode->text().isEmpty()) {
			_newPasscode->setFocus();
			_newPasscode->showError();
		} else if (_reenterPasscode->text().isEmpty()) {
			_reenterPasscode->showError();
		} else if (!_passwordHint->isHidden()) {
			_passwordHint->setFocus();
		} else {
			save();
		}
	} else if (_passwordHint->hasFocus()) {
		if (_recoverEmail->isHidden()) {
			save();
		} else {
			_recoverEmail->setFocus();
		}
	} else if (_recoverEmail->hasFocus()) {
		save();
	}
}

void PasscodeBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	int32 w = st::boxWidth - st::boxPadding.left() * 1.5;
	int32 abouty = (_passwordHint->isHidden() ? ((_reenterPasscode->isHidden() ? (_oldPasscode->y() + (_hasRecovery && !_hintText.isEmpty() ? st::passcodeTextLine : 0)) : _reenterPasscode->y()) + st::passcodeSkip) : _passwordHint->y()) + _oldPasscode->height() + st::passcodeLittleSkip + st::passcodeAboutSkip;
	p.setPen(st::boxTextFg);
	_about.drawLeft(p, st::boxPadding.left(), abouty, w, width());

	if (!_hintText.isEmpty() && _oldError.isEmpty()) {
		_hintText.drawLeftElided(p, st::boxPadding.left(), _oldPasscode->y() + _oldPasscode->height() + ((st::passcodeTextLine - st::normalFont->height) / 2), w, width(), 1, style::al_topleft);
	}

	if (!_oldError.isEmpty()) {
		p.setPen(st::boxTextFgError);
		p.drawText(QRect(st::boxPadding.left(), _oldPasscode->y() + _oldPasscode->height(), w, st::passcodeTextLine), _oldError, style::al_left);
	}

	if (!_newError.isEmpty()) {
		p.setPen(st::boxTextFgError);
		p.drawText(QRect(st::boxPadding.left(), _reenterPasscode->y() + _reenterPasscode->height(), w, st::passcodeTextLine), _newError, style::al_left);
	}

	if (!_emailError.isEmpty()) {
		p.setPen(st::boxTextFgError);
		p.drawText(QRect(st::boxPadding.left(), _recoverEmail->y() + _recoverEmail->height(), w, st::passcodeTextLine), _emailError, style::al_left);
	}
}

void PasscodeBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	const auto has = currentlyHave();
	int32 w = st::boxWidth - st::boxPadding.left() - st::boxPadding.right();
	_oldPasscode->resize(w, _oldPasscode->height());
	_oldPasscode->moveToLeft(st::boxPadding.left(), st::passcodePadding.top());
	_newPasscode->resize(w, _newPasscode->height());
	_newPasscode->moveToLeft(st::boxPadding.left(), _oldPasscode->y() + ((_turningOff || has) ? (_oldPasscode->height() + st::passcodeTextLine + ((_hasRecovery && !_hintText.isEmpty()) ? st::passcodeTextLine : 0)) : 0));
	_reenterPasscode->resize(w, _reenterPasscode->height());
	_reenterPasscode->moveToLeft(st::boxPadding.left(), _newPasscode->y() + _newPasscode->height() + st::passcodeLittleSkip);
	_passwordHint->resize(w, _passwordHint->height());
	_passwordHint->moveToLeft(st::boxPadding.left(), _reenterPasscode->y() + _reenterPasscode->height() + st::passcodeSkip);
	_recoverEmail->resize(w, _passwordHint->height());
	_recoverEmail->moveToLeft(st::boxPadding.left(), _passwordHint->y() + _passwordHint->height() + st::passcodeLittleSkip + _aboutHeight + st::passcodeLittleSkip);

	if (!_recover->isHidden()) {
		_recover->moveToLeft(st::boxPadding.left(), _oldPasscode->y() + _oldPasscode->height() + (_hintText.isEmpty() ? ((st::passcodeTextLine - _recover->height()) / 2) : st::passcodeTextLine));
	}
}

void PasscodeBox::setInnerFocus() {
	if (_skipEmailWarning && !_recoverEmail->isHidden()) {
		_recoverEmail->setFocusFast();
	} else if (_oldPasscode->isHidden()) {
		_newPasscode->setFocusFast();
	} else {
		_oldPasscode->setFocusFast();
	}
}

void PasscodeBox::setPasswordDone(const QByteArray &newPasswordBytes) {
	_setRequest = 0;
	_newPasswordSet.fire_copy(newPasswordBytes);
	auto text = lang(_reenterPasscode->isHidden() ? lng_cloud_password_removed : (_oldPasscode->isHidden() ? lng_cloud_password_was_set : lng_cloud_password_updated));
	getDelegate()->show(Box<InformBox>(text), LayerOption::CloseOther);
}

void PasscodeBox::closeReplacedBy() {
	if (isHidden()) {
		if (_replacedBy && !_replacedBy->isHidden()) {
			_replacedBy->closeBox();
		}
	}
}

void PasscodeBox::setPasswordFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		closeReplacedBy();
		_setRequest = 0;

		_oldPasscode->selectAll();
		_oldPasscode->setFocus();
		_oldPasscode->showError();
		_oldError = lang(lng_flood_error);
		if (_hasRecovery && _hintText.isEmpty()) {
			_recover->hide();
		}
		update();
		return;
	}

	closeReplacedBy();
	_setRequest = 0;
	const auto err = error.type();
	if (err == qstr("PASSWORD_HASH_INVALID")
		|| err == qstr("SRP_PASSWORD_CHANGED")) {
		if (_oldPasscode->isHidden()) {
			_passwordReloadNeeded.fire({});
			closeBox();
		} else {
			badOldPasscode();
		}
	} else if (error.type() == qstr("SRP_ID_INVALID")) {
		handleSrpIdInvalid();
	//} else if (err == qstr("NEW_PASSWORD_BAD")) {
	//} else if (err == qstr("NEW_SALT_INVALID")) {
	} else if (err == qstr("EMAIL_INVALID")) {
		_emailError = lang(lng_cloud_password_bad_email);
		_recoverEmail->setFocus();
		_recoverEmail->showError();
		update();
	}
}

void PasscodeBox::setPasswordFail(
		const QByteArray &newPasswordBytes,
		const QString &email,
		const RPCError &error) {
	const auto prefix = qstr("EMAIL_UNCONFIRMED_");
	if (error.type().startsWith(prefix)) {
		const auto codeLength = error.type().mid(prefix.size()).toInt();

		closeReplacedBy();
		_setRequest = 0;

		validateEmail(email, codeLength, newPasswordBytes);
	} else {
		setPasswordFail(error);
	}
}

void PasscodeBox::validateEmail(
		const QString &email,
		int codeLength,
		const QByteArray &newPasswordBytes) {
	const auto errors = std::make_shared<rpl::event_stream<QString>>();
	const auto resent = std::make_shared<rpl::event_stream<QString>>();
	const auto set = std::make_shared<bool>(false);
	const auto submit = [=](QString code) {
		if (_setRequest) {
			return;
		}
		_setRequest = request(MTPaccount_ConfirmPasswordEmail(
			MTP_string(code)
		)).done([=](const MTPBool &result) {
			*set = true;
			setPasswordDone(newPasswordBytes);
		}).fail([=](const RPCError &error) {
			_setRequest = 0;
			if (MTP::isFloodError(error)) {
				errors->fire(lang(lng_flood_error));
			} else if (error.type() == qstr("CODE_INVALID")) {
				errors->fire(lang(lng_signin_wrong_code));
			} else if (error.type() == qstr("EMAIL_HASH_EXPIRED")) {
				const auto weak = make_weak(this);
				_clearUnconfirmedPassword.fire({});
				if (weak) {
					auto box = Box<InformBox>(
						Lang::Hard::EmailConfirmationExpired());
					weak->getDelegate()->show(
						std::move(box),
						LayerOption::CloseOther);
				}
			} else {
				errors->fire(Lang::Hard::ServerError());
			}
		}).handleFloodErrors().send();
	};
	const auto resend = [=] {
		if (_setRequest) {
			return;
		}
		_setRequest = request(MTPaccount_ResendPasswordEmail(
		)).done([=](const MTPBool &result) {
			_setRequest = 0;
			resent->fire(lang(lng_cloud_password_resent));
		}).fail([=](const RPCError &error) {
			_setRequest = 0;
			errors->fire(Lang::Hard::ServerError());
		}).send();
	};
	const auto box = getDelegate()->show(
		Passport::VerifyEmailBox(
			email,
			codeLength,
			submit,
			resend,
			errors->events(),
			resent->events()),
		LayerOption::KeepOther);

	box->setCloseByOutsideClick(false);
	box->setCloseByEscape(false);
	box->boxClosing(
	) | rpl::filter([=] {
		return !*set;
	}) | start_with_next([=, weak = make_weak(this)] {
		if (weak) {
			weak->_clearUnconfirmedPassword.fire({});
		}
		if (weak) {
			weak->closeBox();
		}
	}, box->lifetime());
}

void PasscodeBox::handleSrpIdInvalid() {
	const auto now = getms(true);
	if (_lastSrpIdInvalidTime > 0
		&& now - _lastSrpIdInvalidTime < Core::kHandleSrpIdInvalidTimeout) {
		_curRequest.id = 0;
		_oldError = Lang::Hard::ServerError();
		update();
	} else {
		_lastSrpIdInvalidTime = now;
		requestPasswordData();
	}
}

void PasscodeBox::save(bool force) {
	if (_setRequest) return;

	QString old = _oldPasscode->text(), pwd = _newPasscode->text(), conf = _reenterPasscode->text();
	const auto has = currentlyHave();
	if (!_cloudPwd && (_turningOff || has)) {
		if (!passcodeCanTry()) {
			_oldError = lang(lng_flood_error);
			_oldPasscode->setFocus();
			_oldPasscode->showError();
			update();
			return;
		}

		if (Local::checkPasscode(old.toUtf8())) {
			cSetPasscodeBadTries(0);
			if (_turningOff) pwd = conf = QString();
		} else {
			cSetPasscodeBadTries(cPasscodeBadTries() + 1);
			cSetPasscodeLastTry(getms(true));
			badOldPasscode();
			return;
		}
	}
	if (!_turningOff && pwd.isEmpty()) {
		_newPasscode->setFocus();
		_newPasscode->showError();
		closeReplacedBy();
		return;
	}
	if (pwd != conf) {
		_reenterPasscode->selectAll();
		_reenterPasscode->setFocus();
		_reenterPasscode->showError();
		if (!conf.isEmpty()) {
			_newError = lang(_cloudPwd ? lng_cloud_password_differ : lng_passcode_differ);
			update();
		}
		closeReplacedBy();
	} else if (!_turningOff && has && old == pwd) {
		_newPasscode->setFocus();
		_newPasscode->showError();
		_newError = lang(_cloudPwd ? lng_cloud_password_is_same : lng_passcode_is_same);
		update();
		closeReplacedBy();
	} else if (_cloudPwd) {
		QString hint = _passwordHint->getLastText(), email = _recoverEmail->getLastText().trimmed();
		if (_cloudPwd && pwd == hint && !_passwordHint->isHidden() && !_newPasscode->isHidden()) {
			_newPasscode->setFocus();
			_newPasscode->showError();
			_newError = lang(lng_cloud_password_bad);
			update();
			closeReplacedBy();
			return;
		}
		if (!_recoverEmail->isHidden() && email.isEmpty() && !force) {
			_skipEmailWarning = true;
			_replacedBy = getDelegate()->show(Box<ConfirmBox>(lang(lng_cloud_password_about_recover), lang(lng_cloud_password_skip_email), st::attentionBoxButton, crl::guard(this, [this] {
				save(true);
			})));
		} else if (_newPasscode->isHidden()) {
			clearCloudPassword(old);
		} else if (_oldPasscode->isHidden()) {
			setNewCloudPassword(pwd);
		} else {
			changeCloudPassword(old, pwd);
		}
	} else {
		cSetPasscodeBadTries(0);
		Local::setPasscode(pwd.toUtf8());
		Auth().checkAutoLock();
		closeBox();
	}
}

void PasscodeBox::clearCloudPassword(const QString &oldPassword) {
	Expects(!_oldPasscode->isHidden());

	const auto send = [=] {
		sendClearCloudPassword(oldPassword);
	};
	if (_notEmptyPassport) {
		const auto box = std::make_shared<QPointer<BoxContent>>();
		const auto confirmed = [=] {
			send();
			if (*box) {
				(*box)->closeBox();
			}
		};
		*box = getDelegate()->show(Box<ConfirmBox>(
			lang(lng_cloud_password_passport_losing),
			lang(lng_continue),
			confirmed));
	} else {
		send();
	}
}

void PasscodeBox::sendClearCloudPassword(const QString &oldPassword) {
	checkPassword(oldPassword, [=](const Core::CloudPasswordResult &check) {
		sendClearCloudPassword(check);
	});
}

void PasscodeBox::checkPassword(
		const QString &oldPassword,
		CheckPasswordCallback callback) {
	const auto passwordUtf = oldPassword.toUtf8();
	_checkPasswordHash = Core::ComputeCloudPasswordHash(
		_curRequest.algo,
		bytes::make_span(passwordUtf));
	checkPasswordHash(std::move(callback));
}

void PasscodeBox::checkPasswordHash(CheckPasswordCallback callback) {
	_checkPasswordCallback = std::move(callback);
	if (_curRequest.id) {
		passwordChecked();
	} else {
		requestPasswordData();
	}
}

void PasscodeBox::passwordChecked() {
	if (!_curRequest || !_curRequest.id || !_checkPasswordCallback) {
		return serverError();
	}
	const auto check = Core::ComputeCloudPasswordCheck(
		_curRequest,
		_checkPasswordHash);
	if (!check) {
		return serverError();
	}
	_curRequest.id = 0;
	_checkPasswordCallback(check);
}

void PasscodeBox::requestPasswordData() {
	if (!_checkPasswordCallback) {
		return serverError();
	}

	request(base::take(_setRequest)).cancel();
	_setRequest = request(
		MTPaccount_GetPassword()
	).done([=](const MTPaccount_Password &result) {
		_setRequest = 0;
		result.match([&](const MTPDaccount_password &data) {
			_curRequest = Core::ParseCloudPasswordCheckRequest(data);
			passwordChecked();
		});
	}).send();
}

void PasscodeBox::serverError() {
	getDelegate()->show(Box<InformBox>(Lang::Hard::ServerError()));
	closeBox();
}

void PasscodeBox::sendClearCloudPassword(
		const Core::CloudPasswordResult &check) {
	const auto newPasswordData = QByteArray();
	const auto newPasswordHash = QByteArray();
	const auto hint = QString();
	const auto email = QString();
	const auto flags = MTPDaccount_passwordInputSettings::Flag::f_new_algo
		| MTPDaccount_passwordInputSettings::Flag::f_new_password_hash
		| MTPDaccount_passwordInputSettings::Flag::f_hint
		| MTPDaccount_passwordInputSettings::Flag::f_email;
	_setRequest = request(MTPaccount_UpdatePasswordSettings(
		check.result,
		MTP_account_passwordInputSettings(
			MTP_flags(flags),
			Core::PrepareCloudPasswordAlgo(_newAlgo),
			MTP_bytes(QByteArray()), // new_password_hash
			MTP_string(hint),
			MTP_string(email),
			MTPSecureSecretSettings())
	)).done([=](const MTPBool &result) {
		setPasswordDone({});
	}).handleFloodErrors().fail([=](const RPCError &error) mutable {
		setPasswordFail({}, QString(), error);
	}).send();
}

void PasscodeBox::setNewCloudPassword(const QString &newPassword) {
	const auto newPasswordBytes = newPassword.toUtf8();
	const auto newPasswordHash = Core::ComputeCloudPasswordDigest(
		_newAlgo,
		bytes::make_span(newPasswordBytes));
	if (newPasswordHash.modpow.empty()) {
		return serverError();
	}
	const auto hint = _passwordHint->getLastText();
	const auto email = _recoverEmail->getLastText().trimmed();
	const auto flags = MTPDaccount_passwordInputSettings::Flag::f_new_algo
		| MTPDaccount_passwordInputSettings::Flag::f_new_password_hash
		| MTPDaccount_passwordInputSettings::Flag::f_hint
		| MTPDaccount_passwordInputSettings::Flag::f_email;
	_checkPasswordCallback = nullptr;
	_setRequest = request(MTPaccount_UpdatePasswordSettings(
		MTP_inputCheckPasswordEmpty(),
		MTP_account_passwordInputSettings(
			MTP_flags(flags),
			Core::PrepareCloudPasswordAlgo(_newAlgo),
			MTP_bytes(newPasswordHash.modpow),
			MTP_string(hint),
			MTP_string(email),
			MTPSecureSecretSettings())
	)).done([=](const MTPBool &result) {
		setPasswordDone(newPasswordBytes);
	}).fail([=](const RPCError &error) {
		setPasswordFail(newPasswordBytes, email, error);
	}).send();
}

void PasscodeBox::changeCloudPassword(
		const QString &oldPassword,
		const QString &newPassword) {
	checkPassword(oldPassword, [=](const Core::CloudPasswordResult &check) {
		changeCloudPassword(oldPassword, check, newPassword);
	});
}

void PasscodeBox::changeCloudPassword(
		const QString &oldPassword,
		const Core::CloudPasswordResult &check,
		const QString &newPassword) {
	_setRequest = request(MTPaccount_GetPasswordSettings(
		check.result
	)).done([=](const MTPaccount_PasswordSettings &result) {
		_setRequest = 0;

		Expects(result.type() == mtpc_account_passwordSettings);
		const auto &data = result.c_account_passwordSettings();

		if (!data.has_secure_settings()) {
			checkPasswordHash([=](const Core::CloudPasswordResult &check) {
				const auto empty = QByteArray();
				sendChangeCloudPassword(check, newPassword, empty);
			});
			return;
		}
		const auto &wrapped = data.vsecure_settings;
		const auto &settings = wrapped.c_secureSecretSettings();
		const auto passwordUtf = oldPassword.toUtf8();
		const auto secret = Passport::DecryptSecureSecret(
			bytes::make_span(settings.vsecure_secret.v),
			Core::ComputeSecureSecretHash(
				Core::ParseSecureSecretAlgo(settings.vsecure_algo),
				bytes::make_span(passwordUtf)));
		if (secret.empty()) {
			LOG(("API Error: Failed to decrypt secure secret."));
			suggestSecretReset(newPassword);
		} else if (Passport::CountSecureSecretId(secret)
				!= settings.vsecure_secret_id.v) {
			LOG(("API Error: Wrong secure secret id."));
			suggestSecretReset(newPassword);
		} else {
			const auto secureSecret = QByteArray(
				reinterpret_cast<const char*>(secret.data()),
				secret.size());
			checkPasswordHash([=](const Core::CloudPasswordResult &check) {
				sendChangeCloudPassword(check, newPassword, secureSecret);
			});
		}
	}).handleFloodErrors().fail([=](const RPCError &error) {
		setPasswordFail(error);
	}).send();
}

void PasscodeBox::suggestSecretReset(const QString &newPassword) {
	const auto box = std::make_shared<QPointer<BoxContent>>();
	const auto resetSecretAndSave = [=] {
		checkPasswordHash([=](const Core::CloudPasswordResult &check) {
			resetSecret(check, newPassword, [=] {
				if (*box) {
					(*box)->closeBox();
				}
			});
		});
	};
	*box = getDelegate()->show(Box<ConfirmBox>(
		Lang::Hard::PassportCorruptedChange(),
		Lang::Hard::PassportCorruptedReset(),
		[=] { resetSecretAndSave(); }));
}

void PasscodeBox::resetSecret(
		const Core::CloudPasswordResult &check,
		const QString &newPassword,
		Fn<void()> callback) {
	using Flag = MTPDaccount_passwordInputSettings::Flag;
	_setRequest = request(MTPaccount_UpdatePasswordSettings(
		check.result,
		MTP_account_passwordInputSettings(
			MTP_flags(Flag::f_new_secure_settings),
			MTPPasswordKdfAlgo(), // new_algo
			MTPbytes(), // new_password_hash
			MTPstring(), // hint
			MTPstring(), // email
			MTP_secureSecretSettings(
				MTP_securePasswordKdfAlgoUnknown(), // secure_algo
				MTP_bytes(QByteArray()), // secure_secret
				MTP_long(0))) // secure_secret_id
	)).done([=](const MTPBool &result) {
		_setRequest = 0;
		callback();
		checkPasswordHash([=](const Core::CloudPasswordResult &check) {
			const auto empty = QByteArray();
			sendChangeCloudPassword(check, newPassword, empty);
		});
	}).fail([=](const RPCError &error) {
		_setRequest = 0;
		if (error.type() == qstr("SRP_ID_INVALID")) {
			handleSrpIdInvalid();
		}
	}).send();
}

void PasscodeBox::sendChangeCloudPassword(
		const Core::CloudPasswordResult &check,
		const QString &newPassword,
		const QByteArray &secureSecret) {
	const auto newPasswordBytes = newPassword.toUtf8();
	const auto newPasswordHash = Core::ComputeCloudPasswordDigest(
		_newAlgo,
		bytes::make_span(newPasswordBytes));
	if (newPasswordHash.modpow.empty()) {
		return serverError();
	}
	const auto hint = _passwordHint->getLastText();
	auto flags = MTPDaccount_passwordInputSettings::Flag::f_new_algo
		| MTPDaccount_passwordInputSettings::Flag::f_new_password_hash
		| MTPDaccount_passwordInputSettings::Flag::f_hint;
	auto newSecureSecret = bytes::vector();
	auto newSecureSecretId = 0ULL;
	if (!secureSecret.isEmpty()) {
		flags |= MTPDaccount_passwordInputSettings::Flag::f_new_secure_settings;
		newSecureSecretId = Passport::CountSecureSecretId(
			bytes::make_span(secureSecret));
		newSecureSecret = Passport::EncryptSecureSecret(
			bytes::make_span(secureSecret),
			Core::ComputeSecureSecretHash(
				_newSecureSecretAlgo,
				bytes::make_span(newPasswordBytes)));
	}
	_setRequest = request(MTPaccount_UpdatePasswordSettings(
		check.result,
		MTP_account_passwordInputSettings(
			MTP_flags(flags),
			Core::PrepareCloudPasswordAlgo(_newAlgo),
			MTP_bytes(newPasswordHash.modpow),
			MTP_string(hint),
			MTPstring(), // email is not changing
			MTP_secureSecretSettings(
				Core::PrepareSecureSecretAlgo(_newSecureSecretAlgo),
				MTP_bytes(newSecureSecret),
				MTP_long(newSecureSecretId)))
	)).done([=](const MTPBool &result) {
		setPasswordDone(newPasswordBytes);
	}).handleFloodErrors().fail([=](const RPCError &error) {
		setPasswordFail(newPasswordBytes, QString(), error);
	}).send();
}

void PasscodeBox::badOldPasscode() {
	_oldPasscode->selectAll();
	_oldPasscode->setFocus();
	_oldPasscode->showError();
	_oldError = lang(_cloudPwd ? lng_cloud_password_wrong : lng_passcode_wrong);
	if (_hasRecovery && _hintText.isEmpty()) {
		_recover->hide();
	}
	update();
}

void PasscodeBox::oldChanged() {
	if (!_oldError.isEmpty()) {
		_oldError = QString();
		if (_hasRecovery && _hintText.isEmpty()) {
			_recover->show();
		}
		update();
	}
}

void PasscodeBox::newChanged() {
	if (!_newError.isEmpty()) {
		_newError = QString();
		update();
	}
}

void PasscodeBox::emailChanged() {
	if (!_emailError.isEmpty()) {
		_emailError = QString();
		update();
	}
}

void PasscodeBox::recoverByEmail() {
	if (_pattern.isEmpty()) {
		_pattern = "-";
		request(MTPauth_RequestPasswordRecovery(
		)).done([=](const MTPauth_PasswordRecovery &result) {
			recoverStarted(result);
		}).fail([=](const RPCError &error) {
			recoverStartFail(error);
		}).send();
	} else {
		recover();
	}
}

void PasscodeBox::recoverExpired() {
	_pattern = QString();
}

void PasscodeBox::recover() {
	if (_pattern == "-") return;

	const auto box = getDelegate()->show(Box<RecoverBox>(
		_pattern,
		_notEmptyPassport));

	box->passwordCleared(
	) | rpl::map([] {
		return QByteArray();
	}) | rpl::start_to_stream(_newPasswordSet, lifetime());

	box->recoveryExpired(
	) | rpl::start_with_next([=] {
		recoverExpired();
	}, lifetime());

	_replacedBy = box;
}

void PasscodeBox::recoverStarted(const MTPauth_PasswordRecovery &result) {
	_pattern = qs(result.c_auth_passwordRecovery().vemail_pattern);
	recover();
}

void PasscodeBox::recoverStartFail(const RPCError &error) {
	_pattern = QString();
	closeBox();
}

RecoverBox::RecoverBox(
	QWidget*,
	const QString &pattern,
	bool notEmptyPassport)
: _pattern(st::normalFont->elided(lng_signin_recover_hint(lt_recover_email, pattern), st::boxWidth - st::boxPadding.left() * 1.5))
, _notEmptyPassport(notEmptyPassport)
, _recoverCode(this, st::defaultInputField, langFactory(lng_signin_code)) {
}

rpl::producer<> RecoverBox::passwordCleared() const {
	return _passwordCleared.events();
}

rpl::producer<> RecoverBox::recoveryExpired() const {
	return _recoveryExpired.events();
}

void RecoverBox::prepare() {
	setTitle(langFactory(lng_signin_recover_title));

	addButton(langFactory(lng_passcode_submit), [=] { submit(); });
	addButton(langFactory(lng_cancel), [=] { closeBox(); });

	setDimensions(st::boxWidth, st::passcodePadding.top() + st::passcodePadding.bottom() + st::passcodeTextLine + _recoverCode->height() + st::passcodeTextLine);

	connect(_recoverCode, &Ui::InputField::changed, [=] { codeChanged(); });
	connect(_recoverCode, &Ui::InputField::submitted, [=] { submit(); });
}

void RecoverBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	p.setFont(st::normalFont);
	p.setPen(st::boxTextFg);
	int32 w = st::boxWidth - st::boxPadding.left() * 1.5;
	p.drawText(QRect(st::boxPadding.left(), _recoverCode->y() - st::passcodeTextLine - st::passcodePadding.top(), w, st::passcodePadding.top() + st::passcodeTextLine), _pattern, style::al_left);

	if (!_error.isEmpty()) {
		p.setPen(st::boxTextFgError);
		p.drawText(QRect(st::boxPadding.left(), _recoverCode->y() + _recoverCode->height(), w, st::passcodeTextLine), _error, style::al_left);
	}
}

void RecoverBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_recoverCode->resize(st::boxWidth - st::boxPadding.left() - st::boxPadding.right(), _recoverCode->height());
	_recoverCode->moveToLeft(st::boxPadding.left(), st::passcodePadding.top() + st::passcodePadding.bottom() + st::passcodeTextLine);
}

void RecoverBox::setInnerFocus() {
	_recoverCode->setFocusFast();
}

void RecoverBox::submit() {
	if (_submitRequest) return;

	QString code = _recoverCode->getLastText().trimmed();
	if (code.isEmpty()) {
		_recoverCode->setFocus();
		_recoverCode->showError();
		return;
	}

	const auto send = crl::guard(this, [=] {
		_submitRequest = MTP::send(
			MTPauth_RecoverPassword(MTP_string(code)),
			rpcDone(&RecoverBox::codeSubmitDone, true),
			rpcFail(&RecoverBox::codeSubmitFail));
	});
	if (_notEmptyPassport) {
		const auto box = std::make_shared<QPointer<BoxContent>>();
		const auto confirmed = [=] {
			send();
			if (*box) {
				(*box)->closeBox();
			}
		};
		*box = getDelegate()->show(Box<ConfirmBox>(
			lang(lng_cloud_password_passport_losing),
			lang(lng_continue),
			confirmed));
	} else {
		send();
	}
}

void RecoverBox::codeChanged() {
	_error = QString();
	update();
}

void RecoverBox::codeSubmitDone(
		bool recover,
		const MTPauth_Authorization &result) {
	_submitRequest = 0;

	_passwordCleared.fire({});
	getDelegate()->show(
		Box<InformBox>(lang(lng_cloud_password_removed)),
		LayerOption::CloseOther);
}

bool RecoverBox::codeSubmitFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		_submitRequest = 0;
		_error = lang(lng_flood_error);
		update();
		_recoverCode->showError();
		return true;
	}
	if (MTP::isDefaultHandledError(error)) return false;

	_submitRequest = 0;

	const QString &err = error.type();
	if (err == qstr("PASSWORD_EMPTY")) {
		_passwordCleared.fire({});
		getDelegate()->show(
			Box<InformBox>(lang(lng_cloud_password_removed)),
			LayerOption::CloseOther);
		return true;
	} else if (err == qstr("PASSWORD_RECOVERY_NA")) {
		closeBox();
		return true;
	} else if (err == qstr("PASSWORD_RECOVERY_EXPIRED")) {
		_recoveryExpired.fire({});
		closeBox();
		return true;
	} else if (err == qstr("CODE_INVALID")) {
		_error = lang(lng_signin_wrong_code);
		update();
		_recoverCode->selectAll();
		_recoverCode->setFocus();
		_recoverCode->showError();
		return true;
	}
	if (Logs::DebugEnabled()) { // internal server error
		_error =  err + ": " + error.description();
	} else {
		_error = Lang::Hard::ServerError();
	}
	update();
	_recoverCode->setFocus();
	return false;
}

RecoveryEmailValidation ConfirmRecoveryEmail(const QString &pattern) {
	const auto errors = std::make_shared<rpl::event_stream<QString>>();
	const auto resent = std::make_shared<rpl::event_stream<QString>>();
	const auto requestId = std::make_shared<mtpRequestId>(0);
	const auto weak = std::make_shared<QPointer<BoxContent>>();
	const auto reloads = std::make_shared<rpl::event_stream<>>();
	const auto cancels = std::make_shared<rpl::event_stream<>>();

	const auto submit = [=](QString code) {
		if (*requestId) {
			return;
		}
		const auto done = [=](const MTPBool &result) {
			*requestId = 0;
			reloads->fire({});
			if (*weak) {
				(*weak)->getDelegate()->show(
					Box<InformBox>(lang(lng_cloud_password_was_set)),
					LayerOption::CloseOther);
			}
		};
		const auto fail = [=](const RPCError &error) {
			const auto skip = MTP::isDefaultHandledError(error)
				&& !MTP::isFloodError(error);
			if (skip) {
				return false;
			}
			*requestId = 0;
			if (MTP::isFloodError(error)) {
				errors->fire(lang(lng_flood_error));
			} else if (error.type() == qstr("CODE_INVALID")) {
				errors->fire(lang(lng_signin_wrong_code));
			} else if (error.type() == qstr("EMAIL_HASH_EXPIRED")) {
				cancels->fire({});
				if (*weak) {
					auto box = Box<InformBox>(
						Lang::Hard::EmailConfirmationExpired());
					(*weak)->getDelegate()->show(
						std::move(box),
						LayerOption::CloseOther);
				}
			} else {
				errors->fire(Lang::Hard::ServerError());
			}
			return true;
		};
		*requestId = MTP::send(
			MTPaccount_ConfirmPasswordEmail(MTP_string(code)),
			rpcDone(done),
			rpcFail(fail));
	};
	const auto resend = [=] {
		if (*requestId) {
			return;
		}
		*requestId = MTP::send(MTPaccount_ResendPasswordEmail(
		), rpcDone([=](const MTPBool &result) {
			*requestId = 0;
			resent->fire(lang(lng_cloud_password_resent));
		}), rpcFail([=](const RPCError &error) {
			*requestId = 0;
			errors->fire(Lang::Hard::ServerError());
			return true;
		}));
	};

	auto box = Passport::VerifyEmailBox(
		pattern,
		0,
		submit,
		resend,
		errors->events(),
		resent->events());

	*weak = box.data();
	return { std::move(box), reloads->events(), cancels->events() };
}
