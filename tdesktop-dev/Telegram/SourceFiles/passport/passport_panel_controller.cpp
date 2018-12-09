/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_panel_controller.h"

#include "lang/lang_keys.h"
#include "passport/passport_panel_edit_document.h"
#include "passport/passport_panel_details_row.h"
#include "passport/passport_panel_edit_contact.h"
#include "passport/passport_panel_edit_scans.h"
#include "passport/passport_panel.h"
#include "base/openssl_help.h"
#include "boxes/passcode_box.h"
#include "boxes/confirm_box.h"
#include "ui/toast/toast.h"
#include "ui/rp_widget.h"
#include "ui/countryinput.h"
#include "core/update_checker.h"
#include "layout.h"
#include "styles/style_boxes.h"

namespace Passport {
namespace {

constexpr auto kMaxNameSize = 255;
constexpr auto kMaxDocumentSize = 24;
constexpr auto kMaxStreetSize = 64;
constexpr auto kMinCitySize = 2;
constexpr auto kMaxCitySize = 64;
constexpr auto kMaxPostcodeSize = 10;
const auto kLanguageNamePrefix = "cloud_lng_passport_in_";

ScanInfo CollectScanInfo(const EditFile &file) {
	const auto status = [&] {
		if (file.fields.accessHash) {
			if (file.fields.downloadOffset < 0) {
				return lang(lng_attach_failed);
			} else if (file.fields.downloadOffset < file.fields.size) {
				return formatDownloadText(
					file.fields.downloadOffset,
					file.fields.size);
			} else {
				return lng_passport_scan_uploaded(
					lt_date,
					langDateTimeFull(ParseDateTime(file.fields.date)));
			}
		} else if (file.uploadData) {
			if (file.uploadData->offset < 0) {
				return lang(lng_attach_failed);
			} else if (file.uploadData->fullId) {
				return formatDownloadText(
					file.uploadData->offset,
					file.uploadData->bytes.size());
			} else {
				return lng_passport_scan_uploaded(
					lt_date,
					langDateTimeFull(ParseDateTime(file.fields.date)));
			}
		} else {
			return formatDownloadText(0, file.fields.size);
		}
	}();
	return {
		file.type,
		FileKey{ file.fields.id, file.fields.dcId },
		!file.fields.error.isEmpty() ? file.fields.error : status,
		file.fields.image,
		file.deleted,
		file.fields.error };
}

ScanListData PrepareScanListData(const Value &value, FileType type) {
	auto result = ScanListData();
	for (const auto &scan : value.filesInEdit(type)) {
		result.files.push_back(CollectScanInfo(scan));
	}
	result.errorMissing = value.fileMissingError(type);
	return result;
}

std::map<FileType, ScanInfo> PrepareSpecialFiles(const Value &value) {
	auto result = std::map<FileType, ScanInfo>();
	const auto types = {
		FileType::FrontSide,
		FileType::ReverseSide,
		FileType::Selfie
	};
	for (const auto type : types) {
		if (value.requiresSpecialScan(type)) {
			const auto i = value.specialScansInEdit.find(type);
			const auto j = result.emplace(
				type,
				(i != end(value.specialScansInEdit)
					? CollectScanInfo(i->second)
					: ScanInfo(type))).first;
		}
	}
	return result;
}

} // namespace

EditDocumentScheme GetDocumentScheme(
		Scope::Type type,
		std::optional<Value::Type> scansType,
		bool nativeNames) {
	using Scheme = EditDocumentScheme;
	using ValueClass = Scheme::ValueClass;
	const auto DontFormat = nullptr;
	const auto CountryFormat = [](const QString &value) {
		const auto result = CountrySelectBox::NameByISO(value);
		return result.isEmpty() ? value : result;
	};
	const auto GenderFormat = [](const QString &value) {
		if (value == qstr("male")) {
			return lang(lng_passport_gender_male);
		} else if (value == qstr("female")) {
			return lang(lng_passport_gender_female);
		}
		return value;
	};
	const auto DontValidate = nullptr;
	const auto FromBoolean = [](auto validation) {
		return [=](const QString &value) {
			return validation(value)
				? std::nullopt
				: base::make_optional(QString());
		};
	};
	const auto LimitedValidate = [=](int max, int min = 1) {
		return FromBoolean([=](const QString &value) {
			return (value.size() >= min) && (value.size() <= max);
		});
	};
	using Result = std::optional<QString>;
	const auto NameValidate = [](const QString &value) -> Result {
		if (value.isEmpty() || value.size() > kMaxNameSize) {
			return QString();
		} else if (!QRegularExpression(
			"^[a-zA-Z0-9\\.,/&\\-' ]+$"
		).match(value).hasMatch()) {
			return lang(lng_passport_bad_name);
		}
		return std::nullopt;
	};
	const auto NativeNameValidate = LimitedValidate(kMaxNameSize);
	const auto NativeNameOrEmptyValidate = LimitedValidate(kMaxNameSize, 0);
	const auto DocumentValidate = LimitedValidate(kMaxDocumentSize);
	const auto StreetValidate = LimitedValidate(kMaxStreetSize);
	const auto CityValidate = LimitedValidate(kMaxCitySize, kMinCitySize);
	const auto PostcodeValidate = FromBoolean([](const QString &value) {
		return QRegularExpression(
			QString("^[a-zA-Z0-9\\-]{2,%1}$").arg(kMaxPostcodeSize)
		).match(value).hasMatch();
	});
	const auto DateValidateBoolean = [](const QString &value) {
		return QRegularExpression(
			"^\\d{2}\\.\\d{2}\\.\\d{4}$"
		).match(value).hasMatch();
	};
	const auto DateValidate = FromBoolean(DateValidateBoolean);
	const auto DateOrEmptyValidate = FromBoolean([=](const QString &value) {
		return value.isEmpty() || DateValidateBoolean(value);
	});
	const auto GenderValidate = FromBoolean([](const QString &value) {
		return value == qstr("male") || value == qstr("female");
	});
	const auto CountryValidate = FromBoolean([=](const QString &value) {
		return !CountryFormat(value).isEmpty();
	});
	const auto NameOrEmptyValidate = [=](const QString &value) -> Result {
		if (value.isEmpty()) {
			return std::nullopt;
		}
		return NameValidate(value);
	};

	switch (type) {
	case Scope::Type::PersonalDetails:
	case Scope::Type::Identity: {
		auto result = Scheme();
		result.detailsHeader = lang(lng_passport_personal_details);
		result.fieldsHeader = lang(lng_passport_document_details);
		if (scansType) {
			result.scansHeader = [&] {
				switch (*scansType) {
				case Value::Type::Passport:
					return lang(lng_passport_identity_passport);
				case Value::Type::DriverLicense:
					return lang(lng_passport_identity_license);
				case Value::Type::IdentityCard:
					return lang(lng_passport_identity_card);
				case Value::Type::InternalPassport:
					return lang(lng_passport_identity_internal);
				default:
					Unexpected("scansType in GetDocumentScheme:Identity.");
				}
			}();
		}
		result.rows = {
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("first_name"),
				lang(lng_passport_first_name),
				NameValidate,
				DontFormat,
				kMaxNameSize,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("middle_name"),
				lang(lng_passport_middle_name),
				NameOrEmptyValidate,
				DontFormat,
				kMaxNameSize,
				qsl("first_name"),
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("last_name"),
				lang(lng_passport_last_name),
				NameValidate,
				DontFormat,
				kMaxNameSize,
				qsl("first_name"),
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Date,
				qsl("birth_date"),
				lang(lng_passport_birth_date),
				DateValidate,
				DontFormat,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Gender,
				qsl("gender"),
				lang(lng_passport_gender),
				GenderValidate,
				GenderFormat,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Country,
				qsl("country_code"),
				lang(lng_passport_country),
				CountryValidate,
				CountryFormat,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Country,
				qsl("residence_country_code"),
				lang(lng_passport_residence_country),
				CountryValidate,
				CountryFormat,
			},
			{
				ValueClass::Scans,
				PanelDetailsType::Text,
				qsl("document_no"),
				lang(lng_passport_document_number),
				DocumentValidate,
				DontFormat,
				kMaxDocumentSize,
			},
			{
				ValueClass::Scans,
				PanelDetailsType::Date,
				qsl("expiry_date"),
				lang(lng_passport_expiry_date),
				DateOrEmptyValidate,
				DontFormat,
			},
		};
		if (nativeNames) {
			result.additionalDependencyKey = qsl("residence_country_code");

			const auto languageValue = [](const QString &countryCode) {
				if (countryCode.isEmpty()) {
					return QString();
				}
				const auto &config = ConfigInstance();
				const auto i = config.languagesByCountryCode.find(
					countryCode);
				if (i == end(config.languagesByCountryCode)) {
					return QString();
				}
				return Lang::Current().getNonDefaultValue(
					kLanguageNamePrefix + i->second.toUtf8());
			};
			result.additionalHeader = [=](const QString &countryCode) {
				const auto language = languageValue(countryCode);
				return language.isEmpty()
					? lang(lng_passport_native_name_title)
					: lng_passport_native_name_language(
						lt_language,
						language);
			};
			result.additionalDescription = [=](const QString &countryCode) {
				const auto language = languageValue(countryCode);
				if (!language.isEmpty()) {
					return lang(lng_passport_native_name_language_about);
				}
				const auto name = CountrySelectBox::NameByISO(countryCode);
				Assert(!name.isEmpty());
				return lng_passport_native_name_about(
					lt_country,
					name);
			};
			result.additionalShown = [](const QString &countryCode) {
				using Result = EditDocumentScheme::AdditionalVisibility;
				if (countryCode.isEmpty()) {
					return Result::Hidden;
				}
				const auto &config = ConfigInstance();
				const auto i = config.languagesByCountryCode.find(
					countryCode);
				if (i != end(config.languagesByCountryCode)
					&& i->second == "en") {
					return Result::OnlyIfError;
				}
				return Result::Shown;
			};
			using Row = EditDocumentScheme::Row;
			auto additional = std::initializer_list<Row>{
				{
					ValueClass::Additional,
					PanelDetailsType::Text,
					qsl("first_name_native"),
					lang(lng_passport_first_name),
					NativeNameValidate,
					DontFormat,
					kMaxNameSize,
					QString(),
					qsl("first_name"),
				},
				{
					ValueClass::Additional,
					PanelDetailsType::Text,
					qsl("middle_name_native"),
					lang(lng_passport_middle_name),
					NativeNameOrEmptyValidate,
					DontFormat,
					kMaxNameSize,
					qsl("first_name_native"),
					qsl("middle_name"),
				},
				{
					ValueClass::Additional,
					PanelDetailsType::Text,
					qsl("last_name_native"),
					lang(lng_passport_last_name),
					NativeNameValidate,
					DontFormat,
					kMaxNameSize,
					qsl("first_name_native"),
					qsl("last_name"),
				},
			};
			for (auto &row : additional) {
				result.rows.push_back(std::move(row));
			}
		}
		return result;
	} break;

	case Scope::Type::AddressDetails:
	case Scope::Type::Address: {
		auto result = Scheme();
		result.detailsHeader = lang(lng_passport_address);
		if (scansType) {
			switch (*scansType) {
			case Value::Type::UtilityBill:
				result.scansHeader = lang(lng_passport_address_bill);
				break;
			case Value::Type::BankStatement:
				result.scansHeader = lang(lng_passport_address_statement);
				break;
			case Value::Type::RentalAgreement:
				result.scansHeader = lang(lng_passport_address_agreement);
				break;
			case Value::Type::PassportRegistration:
				result.scansHeader = lang(lng_passport_address_registration);
				break;
			case Value::Type::TemporaryRegistration:
				result.scansHeader = lang(lng_passport_address_temporary);
				break;
			default:
				Unexpected("scansType in GetDocumentScheme:Address.");
			}
		}
		result.rows = {
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("street_line1"),
				lang(lng_passport_street),
				StreetValidate,
				DontFormat,
				kMaxStreetSize,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("street_line2"),
				lang(lng_passport_street),
				DontValidate,
				DontFormat,
				kMaxStreetSize,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("city"),
				lang(lng_passport_city),
				CityValidate,
				DontFormat,
				kMaxStreetSize,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("state"),
				lang(lng_passport_state),
				DontValidate,
				DontFormat,
				kMaxStreetSize,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Country,
				qsl("country_code"),
				lang(lng_passport_country),
				CountryValidate,
				CountryFormat,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Postcode,
				qsl("post_code"),
				lang(lng_passport_postcode),
				PostcodeValidate,
				DontFormat,
				kMaxPostcodeSize,
			},
		};
		return result;
	} break;
	}
	Unexpected("Type in GetDocumentScheme().");
}

EditContactScheme GetContactScheme(Scope::Type type) {
	using Scheme = EditContactScheme;
	using ValueType = Scheme::ValueType;

	switch (type) {
	case Scope::Type::Phone: {
		auto result = Scheme(ValueType::Phone);
		result.aboutExisting = lang(lng_passport_use_existing_phone);
		result.newHeader = lang(lng_passport_new_phone);
		result.aboutNew = lang(lng_passport_new_phone_code);
		result.validate = [](const QString &value) {
			return QRegularExpression(
				"^\\d{2,12}$"
			).match(value).hasMatch();
		};
		result.format = [](const QString &value) {
			return App::formatPhone(value);
		};
		result.postprocess = [](QString value) {
			return value.replace(QRegularExpression("[^\\d]"), QString());
		};
		return result;
	} break;

	case Scope::Type::Email: {
		auto result = Scheme(ValueType::Text);
		result.aboutExisting = lang(lng_passport_use_existing_email);
		result.newHeader = lang(lng_passport_new_email);
		result.newPlaceholder = langFactory(lng_passport_email_title);
		result.aboutNew = lang(lng_passport_new_email_code);
		result.validate = [](const QString &value) {
			const auto at = value.indexOf('@');
			const auto dot = value.lastIndexOf('.');
			return (at > 0) && (dot > at);
		};
		result.format = result.postprocess = [](const QString &value) {
			return value.trimmed();
		};
		return result;
	} break;
	}
	Unexpected("Type in GetContactScheme().");
}

const std::map<QString, QString> &LatinToNativeMap() {
	static const auto result = std::map<QString, QString> {
		{ qsl("first_name"), qsl("first_name_native") },
		{ qsl("last_name"), qsl("last_name_native") },
		{ qsl("middle_name"), qsl("middle_name_native") },
	};
	return result;
}

const std::map<QString, QString> &NativeToLatinMap() {
	static const auto result = std::map<QString, QString> {
		{ qsl("first_name_native"), qsl("first_name") },
		{ qsl("last_name_native"), qsl("last_name") },
		{ qsl("middle_name_native"), qsl("middle_name") },
	};
	return result;
}

QString AdjustKeyName(not_null<const Value*> value, const QString &key) {
	if (!value->nativeNames) {
		return key;
	}
	const auto &map = LatinToNativeMap();
	const auto i = map.find(key);
	return (i == end(map)) ? key : i->second;
}

bool SkipFieldCheck(not_null<const Value*> value, const QString &key) {
	if (value->type != Value::Type::PersonalDetails) {
		return false;
	}
	const auto &dontCheckNames = value->nativeNames
		? LatinToNativeMap()
		: NativeToLatinMap();
	return dontCheckNames.find(key) != end(dontCheckNames);
}

ScanInfo::ScanInfo(FileType type) : type(type) {
}

ScanInfo::ScanInfo(
	FileType type,
	const FileKey &key,
	const QString &status,
	const QImage &thumb,
	bool deleted,
	const QString &error)
: type(type)
, key(key)
, status(status)
, thumb(thumb)
, deleted(deleted)
, error(error) {
}

BoxPointer::BoxPointer(QPointer<BoxContent> value)
: _value(value) {
}

BoxPointer::BoxPointer(BoxPointer &&other)
: _value(base::take(other._value)) {
}

BoxPointer &BoxPointer::operator=(BoxPointer &&other) {
	std::swap(_value, other._value);
	return *this;
}

BoxPointer::~BoxPointer() {
	if (const auto strong = get()) {
		strong->closeBox();
	}
}

BoxContent *BoxPointer::get() const {
	return _value.data();
}

BoxPointer::operator BoxContent*() const {
	return get();
}

BoxPointer::operator bool() const {
	return get();
}

BoxContent *BoxPointer::operator->() const {
	return get();
}

PanelController::PanelController(not_null<FormController*> form)
: _form(form)
, _scopes(ComputeScopes(_form->form())) {
	_form->secretReadyEvents(
	) | rpl::start_with_next([=] {
		ensurePanelCreated();
		_panel->showForm();
	}, lifetime());

	_form->verificationNeeded(
	) | rpl::start_with_next([=](not_null<const Value*> value) {
		processVerificationNeeded(value);
	}, lifetime());

	_form->verificationUpdate(
	) | rpl::filter([=](not_null<const Value*> field) {
		return (field->verification.codeLength == 0);
	}) | rpl::start_with_next([=](not_null<const Value*> field) {
		_verificationBoxes.erase(field);
	}, lifetime());
}

not_null<UserData*> PanelController::bot() const {
	return _form->bot();
}

QString PanelController::privacyPolicyUrl() const {
	return _form->privacyPolicyUrl();
}

void PanelController::fillRows(
	Fn<void(
		QString title,
		QString description,
		bool ready,
		bool error)> callback) {
	if (_scopes.empty()) {
		_scopes = ComputeScopes(_form->form());
	}
	for (const auto &scope : _scopes) {
		const auto row = ComputeScopeRow(scope);
		const auto main = scope.details
			? not_null<const Value*>(scope.details)
			: scope.documents[0];
		if (main && !row.ready.isEmpty()) {
			_submitErrors.erase(
				ranges::remove(_submitErrors, main),
				_submitErrors.end());
		}
		const auto submitError = base::contains(_submitErrors, main);
		callback(
			row.title,
			(!row.error.isEmpty()
				? row.error
				: !row.ready.isEmpty()
				? row.ready
				: row.description),
			!row.ready.isEmpty(),
			!row.error.isEmpty() || submitError);
	}
}

rpl::producer<> PanelController::refillRows() const {
	return rpl::merge(
		_submitFailed.events(),
		_form->valueSaveFinished() | rpl::map([] {
			return rpl::empty_value();
		}));
}

void PanelController::submitForm() {
	_submitErrors = _form->submitGetErrors();
	if (!_submitErrors.empty()) {
		_submitFailed.fire({});
	}
}

void PanelController::submitPassword(const QByteArray &password) {
	_form->submitPassword(password);
}

void PanelController::recoverPassword() {
	_form->recoverPassword();
}

rpl::producer<QString> PanelController::passwordError() const {
	return _form->passwordError();
}

QString PanelController::passwordHint() const {
	return _form->passwordSettings().hint;
}

QString PanelController::unconfirmedEmailPattern() const {
	return _form->passwordSettings().unconfirmedPattern;
}

QString PanelController::defaultEmail() const {
	return _form->defaultEmail();
}

QString PanelController::defaultPhoneNumber() const {
	return _form->defaultPhoneNumber();
}

void PanelController::setupPassword() {
	Expects(_panel != nullptr);

	const auto &settings = _form->passwordSettings();
	if (settings.unknownAlgo
		|| !settings.newAlgo
		|| !settings.newSecureAlgo) {
		showUpdateAppBox();
		return;
	} else if (settings.request) {
		showAskPassword();
		return;
	}

	const auto hasRecovery = false;
	const auto notEmptyPassport = false;
	const auto hint = QString();
	auto box = show(Box<PasscodeBox>(
		Core::CloudPasswordCheckRequest(), // current
		settings.newAlgo,
		hasRecovery,
		notEmptyPassport,
		hint,
		settings.newSecureAlgo));
	box->newPasswordSet(
	) | rpl::filter([=](const QByteArray &password) {
		return !password.isEmpty();
	}) | rpl::start_with_next([=](const QByteArray &password) {
		_form->reloadAndSubmitPassword(password);
	}, box->lifetime());

	rpl::merge(
		box->passwordReloadNeeded(),
		box->newPasswordSet(
		) | rpl::filter([=](const QByteArray &password) {
			return password.isEmpty();
		}) | rpl::map([] { return rpl::empty_value(); })
	) | rpl::start_with_next([=] {
		_form->reloadPassword();
	}, box->lifetime());

	box->clearUnconfirmedPassword(
	) | rpl::start_with_next([=] {
		_form->cancelPassword();
	}, box->lifetime());
}

void PanelController::cancelPasswordSubmit() {
	const auto box = std::make_shared<QPointer<BoxContent>>();
	*box = show(Box<ConfirmBox>(
		lang(lng_passport_stop_password_sure),
		lang(lng_passport_stop),
		[=] { if (*box) (*box)->closeBox(); _form->cancelPassword(); }));
}

void PanelController::validateRecoveryEmail() {
	auto validation = ConfirmRecoveryEmail(unconfirmedEmailPattern());

	std::move(
		validation.reloadRequests
	) | rpl::start_with_next([=] {
		_form->reloadPassword();
	}, validation.box->lifetime());

	std::move(
		validation.cancelRequests
	) | rpl::start_with_next([=] {
		_form->cancelPassword();
	}, validation.box->lifetime());

	show(std::move(validation.box));
}

bool PanelController::canAddScan(FileType type) const {
	Expects(_editScope != nullptr);
	Expects(_editDocument != nullptr);

	return _form->canAddScan(_editDocument, type);
}

void PanelController::uploadScan(FileType type, QByteArray &&content) {
	Expects(_editScope != nullptr);
	Expects(_editDocument != nullptr);
	Expects(_editDocument->requiresScan(type));

	_form->uploadScan(_editDocument, type, std::move(content));
}

void PanelController::deleteScan(
		FileType type,
		std::optional<int> fileIndex) {
	Expects(_editScope != nullptr);
	Expects(_editDocument != nullptr);
	Expects(_editDocument->requiresScan(type));

	_form->deleteScan(_editDocument, type, fileIndex);
}

void PanelController::restoreScan(
		FileType type,
		std::optional<int> fileIndex) {
	Expects(_editScope != nullptr);
	Expects(_editDocument != nullptr);
	Expects(_editDocument->requiresScan(type));

	_form->restoreScan(_editDocument, type, fileIndex);
}

rpl::producer<ScanInfo> PanelController::scanUpdated() const {
	return _form->scanUpdated(
	) | rpl::filter([=](not_null<const EditFile*> file) {
		return (file->value == _editDocument);
	}) | rpl::map([](not_null<const EditFile*> file) {
		return CollectScanInfo(*file);
	});
}

rpl::producer<ScopeError> PanelController::saveErrors() const {
	return _saveErrors.events();
}

std::vector<ScopeError> PanelController::collectSaveErrors(
		not_null<const Value*> value) const {
	auto result = std::vector<ScopeError>();
	for (const auto &[key, value] : value->data.parsedInEdit.fields) {
		if (!value.error.isEmpty()) {
			result.push_back({ key, value.error });
		}
	}
	return result;
}

auto PanelController::deleteValueLabel() const
-> std::optional<rpl::producer<QString>> {
	Expects(_editScope != nullptr);

	if (hasValueDocument()) {
		return Lang::Viewer(lng_passport_delete_document);
	} else if (!hasValueFields()) {
		return std::nullopt;
	}
	switch (_editScope->type) {
	case Scope::Type::PersonalDetails:
	case Scope::Type::Identity:
		return Lang::Viewer(lng_passport_delete_details);
	case Scope::Type::AddressDetails:
	case Scope::Type::Address:
		return Lang::Viewer(lng_passport_delete_address);
	case Scope::Type::Email:
		return Lang::Viewer(lng_passport_delete_email);
	case Scope::Type::Phone:
		return Lang::Viewer(lng_passport_delete_phone);
	}
	Unexpected("Type in PanelController::deleteValueLabel.");
}

bool PanelController::hasValueDocument() const {
	Expects(_editScope != nullptr);

	if (!_editDocument) {
		return false;
	}
	return !_editDocument->data.parsed.fields.empty()
		|| !_editDocument->files(FileType::Scan).empty()
		|| !_editDocument->files(FileType::Translation).empty()
		|| !_editDocument->specialScans.empty();
}

bool PanelController::hasValueFields() const {
	return _editValue && !_editValue->data.parsed.fields.empty();
}

void PanelController::deleteValue() {
	Expects(_editScope != nullptr);
	Expects(hasValueDocument() || hasValueFields());

	if (savingScope()) {
		return;
	}
	const auto text = [&] {
		switch (_editScope->type) {
		case Scope::Type::PersonalDetails:
			return lang(lng_passport_delete_details_sure);
		case Scope::Type::Identity:
			return lang(lng_passport_delete_document_sure);
		case Scope::Type::AddressDetails:
			return lang(lng_passport_delete_address_sure);
		case Scope::Type::Address:
			return lang(lng_passport_delete_document_sure);
		case Scope::Type::Phone:
			return lang(lng_passport_delete_phone_sure);
		case Scope::Type::Email:
			return lang(lng_passport_delete_email_sure);
		}
		Unexpected("Type in deleteValue.");
	}();
	const auto checkbox = (hasValueDocument() && hasValueFields()) ? [&] {
		switch (_editScope->type) {
		case Scope::Type::Identity:
			return lang(lng_passport_delete_details);
		case Scope::Type::Address:
			return lang(lng_passport_delete_address);
		}
		Unexpected("Type in deleteValue.");
	}() : QString();

	_editScopeBoxes.emplace_back(show(ConfirmDeleteDocument(
		[=](bool withDetails) { deleteValueSure(withDetails); },
		text,
		checkbox)));
}

void PanelController::deleteValueSure(bool withDetails) {
	Expects(!withDetails || _editValue != nullptr);

	if (hasValueDocument()) {
		_form->deleteValueEdit(_editDocument);
	}
	if (withDetails || !hasValueDocument()) {
		_form->deleteValueEdit(_editValue);
	}
}

void PanelController::suggestReset(Fn<void()> callback) {
	_resetBox = BoxPointer(show(Box<ConfirmBox>(
		Lang::Hard::PassportCorrupted(),
		Lang::Hard::PassportCorruptedReset(),
		[=] { resetPassport(callback); },
		[=] { cancelReset(); })).data());
}

void PanelController::resetPassport(Fn<void()> callback) {
	const auto box = show(Box<ConfirmBox>(
		Lang::Hard::PassportCorruptedResetSure(),
		Lang::Hard::PassportCorruptedReset(),
		st::attentionBoxButton,
		[=] { base::take(_resetBox); callback(); },
		[=] { suggestReset(callback); }));
	_resetBox = BoxPointer(box.data());
}

void PanelController::cancelReset() {
	const auto weak = base::take(_resetBox);
	_form->cancelSure();
}

QString PanelController::getDefaultContactValue(Scope::Type type) const {
	switch (type) {
	case Scope::Type::Phone:
		return _form->defaultPhoneNumber();
	case Scope::Type::Email:
		return _form->defaultEmail();
	}
	Unexpected("Type in PanelController::getDefaultContactValue().");
}

void PanelController::showAskPassword() {
	ensurePanelCreated();
	_panel->showAskPassword();
}

void PanelController::showNoPassword() {
	ensurePanelCreated();
	_panel->showNoPassword();
}

void PanelController::showCriticalError(const QString &error) {
	ensurePanelCreated();
	_panel->showCriticalError(error);
}

void PanelController::showUpdateAppBox() {
	ensurePanelCreated();

	const auto callback = [=] {
		_form->cancelSure();
		Core::UpdateApplication();
	};
	show(
		Box<ConfirmBox>(
			lang(lng_passport_app_out_of_date),
			lang(lng_menu_update),
			callback,
			[=] { _form->cancelSure(); }),
		LayerOption::KeepOther,
		anim::type::instant);
}

void PanelController::ensurePanelCreated() {
	if (!_panel) {
		_panel = std::make_unique<Panel>(this);
	}
}

std::optional<int> PanelController::findBestDocumentIndex(
		const Scope &scope) const {
	Expects(!scope.documents.empty());

	const auto &documents = scope.documents;
	const auto i = ranges::min_element(
		documents,
		std::less<>(),
		[](not_null<const Value*> document) {
			return document->whatNotFilled();
		});
	return ((*i)->whatNotFilled() == Value::kNothingFilled)
		? std::nullopt
		: base::make_optional(int(i - begin(documents)));
	return -1;
}

void PanelController::editScope(int index) {
	Expects(_panel != nullptr);
	Expects(index >= 0 && index < _scopes.size());

	const auto &scope = _scopes[index];
	if (scope.documents.empty()) {
		editScope(index, std::nullopt);
	} else {
		const auto documentIndex = findBestDocumentIndex(scope);
		if (documentIndex || scope.documents.size() == 1) {
			editScope(index, documentIndex ? *documentIndex : 0);
		} else {
			requestScopeFilesType(index);
		}
	}
}

void PanelController::requestScopeFilesType(int index) {
	Expects(_panel != nullptr);
	Expects(index >= 0 && index < _scopes.size());

	const auto type = _scopes[index].type;
	_scopeDocumentTypeBox = [&] {
		if (type == Scope::Type::Identity) {
			return show(RequestIdentityType(
				[=](int documentIndex) {
					editWithUpload(index, documentIndex);
				},
				ranges::view::all(
					_scopes[index].documents
				) | ranges::view::transform([](auto value) {
					return value->type;
				}) | ranges::view::transform([](Value::Type type) {
					switch (type) {
					case Value::Type::Passport:
						return lang(lng_passport_identity_passport);
					case Value::Type::IdentityCard:
						return lang(lng_passport_identity_card);
					case Value::Type::DriverLicense:
						return lang(lng_passport_identity_license);
					case Value::Type::InternalPassport:
						return lang(lng_passport_identity_internal);
					default:
						Unexpected("IdentityType in requestScopeFilesType");
					}
				}) | ranges::to_vector));
		} else if (type == Scope::Type::Address) {
			return show(RequestAddressType(
				[=](int documentIndex) {
					editWithUpload(index, documentIndex);
				},
				ranges::view::all(
					_scopes[index].documents
				) | ranges::view::transform([](auto value) {
					return value->type;
				}) | ranges::view::transform([](Value::Type type) {
					switch (type) {
					case Value::Type::UtilityBill:
						return lang(lng_passport_address_bill);
					case Value::Type::BankStatement:
						return lang(lng_passport_address_statement);
					case Value::Type::RentalAgreement:
						return lang(lng_passport_address_agreement);
					case Value::Type::PassportRegistration:
						return lang(lng_passport_address_registration);
					case Value::Type::TemporaryRegistration:
						return lang(lng_passport_address_temporary);
					default:
						Unexpected("AddressType in requestScopeFilesType");
					}
				}) | ranges::to_vector));
		} else {
			Unexpected("Type in processVerificationNeeded.");
		}
	}();
}

void PanelController::editWithUpload(int index, int documentIndex) {
	Expects(_panel != nullptr);
	Expects(index >= 0 && index < _scopes.size());
	Expects(documentIndex >= 0
		&& documentIndex < _scopes[index].documents.size());

	const auto document = _scopes[index].documents[documentIndex];
	const auto type = document->requiresSpecialScan(FileType::FrontSide)
		? FileType::FrontSide
		: FileType::Scan;
	const auto allowMany = (type == FileType::Scan);
	const auto widget = _panel->widget();
	EditScans::ChooseScan(widget.get(), type, [=](QByteArray &&content) {
		if (_scopeDocumentTypeBox) {
			_scopeDocumentTypeBox = BoxPointer();
		}
		if (!_editScope || !_editDocument) {
			startScopeEdit(index, documentIndex);
		}
		uploadScan(type, std::move(content));
	}, [=](ReadScanError error) {
		readScanError(error);
	});
}

void PanelController::readScanError(ReadScanError error) {
	show(Box<InformBox>([&] {
		switch (error) {
		case ReadScanError::FileTooLarge:
			return lang(lng_passport_error_too_large);
		case ReadScanError::BadImageSize:
			return lang(lng_passport_error_bad_size);
		case ReadScanError::CantReadImage:
			return lang(lng_passport_error_cant_read);
		case ReadScanError::Unknown:
			return Lang::Hard::UnknownSecureScanError();
		}
		Unexpected("Error type in PanelController::readScanError.");
	}()));
}

bool PanelController::editRequiresScanUpload(
		int index,
		std::optional<int> documentIndex) const {
	Expects(index >= 0 && index < _scopes.size());
	Expects(!documentIndex
		|| (*documentIndex >= 0
			&& *documentIndex < _scopes[index].documents.size()));

	if (!documentIndex) {
		return false;
	}
	const auto document = _scopes[index].documents[*documentIndex];
	if (document->requiresSpecialScan(FileType::FrontSide)) {
		const auto &scans = document->specialScans;
		return (scans.find(FileType::FrontSide) == end(scans));
	}
	return document->files(FileType::Scan).empty();
}

void PanelController::editScope(
		int index,
		std::optional<int> documentIndex) {
	if (editRequiresScanUpload(index, documentIndex)) {
		editWithUpload(index, *documentIndex);
	} else {
		startScopeEdit(index, documentIndex);
	}
}

void PanelController::startScopeEdit(
		int index,
		std::optional<int> documentIndex) {
	Expects(_panel != nullptr);
	Expects(index >= 0 && index < _scopes.size());
	Expects(_scopes[index].details != 0 || documentIndex.has_value());
	Expects(!documentIndex.has_value()
		|| (*documentIndex >= 0
			&& *documentIndex < _scopes[index].documents.size()));

	_editScope = &_scopes[index];
	_editValue = _editScope->details;
	_editDocument = documentIndex
		? _scopes[index].documents[*documentIndex].get()
		: nullptr;

	if (_editValue) {
		_form->startValueEdit(_editValue);
	}
	if (_editDocument) {
		_form->startValueEdit(_editDocument);
	}

	auto content = [&]() -> object_ptr<Ui::RpWidget> {
		switch (_editScope->type) {
		case Scope::Type::Identity:
		case Scope::Type::Address: {
			Assert(_editDocument != nullptr);
			auto scans = PrepareScanListData(
				*_editDocument,
				FileType::Scan);
			auto translations = _editDocument->translationRequired
				? base::make_optional(PrepareScanListData(
					*_editDocument,
					FileType::Translation))
				: std::nullopt;
			auto result = _editValue
				? object_ptr<PanelEditDocument>(
					_panel->widget(),
					this,
					GetDocumentScheme(
						_editScope->type,
						_editDocument->type,
						_editValue->nativeNames),
					_editValue->error,
					_editValue->data.parsedInEdit,
					_editDocument->error,
					_editDocument->data.parsedInEdit,
					std::move(scans),
					std::move(translations),
					PrepareSpecialFiles(*_editDocument))
				: object_ptr<PanelEditDocument>(
					_panel->widget(),
					this,
					GetDocumentScheme(
						_editScope->type,
						_editDocument->type,
						false),
					_editDocument->error,
					_editDocument->data.parsedInEdit,
					std::move(scans),
					std::move(translations),
					PrepareSpecialFiles(*_editDocument));
			const auto weak = make_weak(result.data());
			_panelHasUnsavedChanges = [=] {
				return weak ? weak->hasUnsavedChanges() : false;
			};
			return std::move(result);
		} break;
		case Scope::Type::PersonalDetails:
		case Scope::Type::AddressDetails: {
			Assert(_editValue != nullptr);
			auto result = object_ptr<PanelEditDocument>(
				_panel->widget(),
				this,
				GetDocumentScheme(
					_editScope->type,
					std::nullopt,
					_editValue->nativeNames),
				_editValue->error,
				_editValue->data.parsedInEdit);
			const auto weak = make_weak(result.data());
			_panelHasUnsavedChanges = [=] {
				return weak ? weak->hasUnsavedChanges() : false;
			};
			return std::move(result);
		} break;
		case Scope::Type::Phone:
		case Scope::Type::Email: {
			Assert(_editValue != nullptr);
			const auto &parsed = _editValue->data.parsedInEdit;
			const auto valueIt = parsed.fields.find("value");
			const auto value = (valueIt == end(parsed.fields)
				? QString()
				: valueIt->second.text);
			const auto existing = getDefaultContactValue(_editScope->type);
			_panelHasUnsavedChanges = nullptr;
			return object_ptr<PanelEditContact>(
				_panel->widget(),
				this,
				GetContactScheme(_editScope->type),
				value,
				(existing.toLower().trimmed() != value.toLower().trimmed()
					? existing
					: QString()));
		} break;
		}
		Unexpected("Type in PanelController::editScope().");
	}();

	content->lifetime().add([=] {
		cancelValueEdit();
	});

	_panel->setBackAllowed(true);

	_panel->backRequests(
	) | rpl::start_with_next([=] {
		cancelEditScope();
	}, content->lifetime());

	_form->valueSaveFinished(
	) | rpl::start_with_next([=](not_null<const Value*> value) {
		processValueSaveFinished(value);
	}, content->lifetime());

	_panel->showEditValue(std::move(content));
}

void PanelController::processValueSaveFinished(
		not_null<const Value*> value) {
	Expects(_editScope != nullptr);

	const auto boxIt = _verificationBoxes.find(value);
	if (boxIt != end(_verificationBoxes)) {
		const auto saved = std::move(boxIt->second);
		_verificationBoxes.erase(boxIt);
	}

	if ((_editValue == value || _editDocument == value) && !savingScope()) {
		if (auto errors = collectSaveErrors(value); !errors.empty()) {
			for (auto &&error : errors) {
				_saveErrors.fire(std::move(error));
			}
		} else {
			_panel->showForm();
		}
	}
}

bool PanelController::uploadingScopeScan() const {
	return (_editValue && _editValue->uploadingScan())
		|| (_editDocument && _editDocument->uploadingScan());
}

bool PanelController::savingScope() const {
	return (_editValue && _editValue->saving())
		|| (_editDocument && _editDocument->saving());
}

void PanelController::processVerificationNeeded(
		not_null<const Value*> value) {
	const auto i = _verificationBoxes.find(value);
	if (i != _verificationBoxes.end()) {
		LOG(("API Error: Requesting for verification repeatedly."));
		return;
	}
	const auto textIt = value->data.parsedInEdit.fields.find("value");
	Assert(textIt != end(value->data.parsedInEdit.fields));
	const auto text = textIt->second.text;
	const auto type = value->type;
	const auto update = _form->verificationUpdate(
	) | rpl::filter([=](not_null<const Value*> field) {
		return (field == value);
	});
	const auto box = [&] {
		if (type == Value::Type::Phone) {
			return show(VerifyPhoneBox(
				text,
				value->verification.codeLength,
				[=](const QString &code) { _form->verify(value, code); },

				value->verification.call ? rpl::single(
					value->verification.call->getText()
				) | rpl::then(rpl::duplicate(
					update
				) | rpl::filter([=](not_null<const Value*> field) {
					return field->verification.call != nullptr;
				}) | rpl::map([=](not_null<const Value*> field) {
					return field->verification.call->getText();
				})) : (rpl::single(QString()) | rpl::type_erased()),

				rpl::duplicate(
					update
				) | rpl::map([=](not_null<const Value*> field) {
					return field->verification.error;
				}) | rpl::distinct_until_changed()));
		} else if (type == Value::Type::Email) {
			return show(VerifyEmailBox(
				text,
				value->verification.codeLength,
				[=](const QString &code) { _form->verify(value, code); },
				nullptr, // resend

				rpl::duplicate(
					update
				) | rpl::map([=](not_null<const Value*> field) {
					return field->verification.error;
				}) | rpl::distinct_until_changed(),

				rpl::never<QString>()));
		} else {
			Unexpected("Type in processVerificationNeeded.");
		}
	}();

	box->boxClosing(
	) | rpl::start_with_next([=] {
		_form->cancelValueVerification(value);
	}, lifetime());

	_verificationBoxes.emplace(value, box);
}

void PanelController::cancelValueEdit() {
	Expects(_editScope != nullptr);

	_editScopeBoxes.clear();
	if (const auto value = base::take(_editValue)) {
		_form->cancelValueEdit(value);
	}
	if (const auto document = base::take(_editDocument)) {
		_form->cancelValueEdit(document);
	}
	_editScope = nullptr;
}

void PanelController::saveScope(ValueMap &&data, ValueMap &&filesData) {
	Expects(_panel != nullptr);

	if (uploadingScopeScan()) {
		showToast(lang(lng_passport_wait_upload));
		return;
	} else if (savingScope()) {
		return;
	}

	if (_editValue) {
		_form->saveValueEdit(_editValue, std::move(data));
	} else {
		Assert(data.fields.empty());
	}
	if (_editDocument) {
		_form->saveValueEdit(_editDocument, std::move(filesData));
	} else {
		Assert(filesData.fields.empty());
	}
}

bool PanelController::editScopeChanged(
		const ValueMap &data,
		const ValueMap &filesData) const {
	if (_editValue && ValueChanged(_editValue, data)) {
		return true;
	} else if (_editDocument && ValueChanged(_editDocument, filesData)) {
		return true;
	}
	return false;
}

void PanelController::cancelEditScope() {
	Expects(_editScope != nullptr);

	if (_panelHasUnsavedChanges && _panelHasUnsavedChanges()) {
		if (!_confirmForgetChangesBox) {
			_confirmForgetChangesBox = show(Box<ConfirmBox>(
				lang(lng_passport_sure_cancel),
				lang(lng_continue),
				[=] { _panel->showForm(); }));
			_editScopeBoxes.emplace_back(_confirmForgetChangesBox);
		}
	} else {
		_panel->showForm();
	}
}

int PanelController::closeGetDuration() {
	if (_panel) {
		return _panel->hideAndDestroyGetDuration();
	}
	return 0;
}

void PanelController::cancelAuth() {
	_form->cancel();
}

void PanelController::cancelAuthSure() {
	_form->cancelSure();
}

void PanelController::showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) {
	_panel->showBox(std::move(box), options, animated);
}

void PanelController::showToast(const QString &text) {
	_panel->showToast(text);
}

rpl::lifetime &PanelController::lifetime() {
	return _lifetime;
}

PanelController::~PanelController() = default;

} // namespace Passport
