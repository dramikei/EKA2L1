#include <services/applist/applist.h>
#include <services/featmgr/featmgr.h>
#include <services/fontbitmap/fontbitmap.h>
#include <services/fs/fs.h>
#include <services/window/window.h>

#include <services/init.h>

#include <core.h>

#include <e32lang.h>

#ifdef WIN32
#include <Windows.h>
#endif

#define CREATE_SERVER_D(sys, svr)                   \
    server_ptr temp = std::make_shared<##svr>(sys); \
    sys->get_kernel_system()->add_custom_server(temp)

#define CREATE_SERVER(sys, svr)          \
    temp = std::make_shared<##svr>(sys); \
    sys->get_kernel_system()->add_custom_server(temp)

#define DEFINE_INT_PROP_D(sys, category, key, data)                                                 \
    property_ptr prop = sys->get_kernel_system()->create_prop(service::property_type::int_data, 0); \
    prop->first = category;                                                                         \
    prop->second = key;                                                                             \
    prop->set(data);

#define DEFINE_INT_PROP(sys, category, key, data)                                      \
    prop = sys->get_kernel_system()->create_prop(service::property_type::int_data, 0); \
    prop->first = category;                                                            \
    prop->second = key;                                                                \
    prop->set(data);

#define DEFINE_BIN_PROP_D(sys, category, key, size, data)                                              \
    property_ptr prop = sys->get_kernel_system()->create_prop(service::property_type::bin_data, size); \
    prop->first = category;                                                                            \
    prop->second = key;                                                                                \
    prop->set(data);

#define DEFINE_BIN_PROP(sys, category, key, size, data)                                   \
    prop = sys->get_kernel_system()->create_prop(service::property_type::bin_data, size); \
    prop->first = category;                                                               \
    prop->second = key;                                                                   \
    prop->set(data);

const uint32_t sys_category = 0x101f75b6;

const uint32_t hal_key_base = 0x1020e306;
const uint32_t unk_key1 = 0x1020e34e;

const uint32_t locale_data_key = 0x10208904;
const uint32_t locale_lang_key = 0x10208903;

namespace epoc {
    enum TDateFormat {
        EDateAmerican,
        EDateEuropean,
        EDateJapanese
    };

    enum TTimeFormat {
        ETime12,
        ETime24
    };

    enum TLocalePos {
        ELocaleBefore,
        ELocaleAfter
    };

    enum TNegativeCurrencyFormat {
        E_NegC_LeadingMinusSign,
        E_NegC_InBrackets,
        E_NegC_InterveningMinusSignWithSpaces,
        E_NegC_InterveningMinusSignWithoutSpaces,
        E_NegC_TrailingMinusSign
    };

    enum TDaylightSavingZone {
        EDstHome = 0x40000000,
        EDstNone = 0,
        EDstEuropean = 1,
        EDstNorthern = 2,
        EDstSouthern = 4
    };

    enum TDay {
        EMonday,
        ETuesday,
        EWednesday,
        EThursday,
        EFriday,
        ESaturday,
        ESunday
    };

    enum TClockFormat {
        EClockAnalog,
        EClockDigital
    };

    enum TUnitsFormat {
        EUnitsImperial,
        EUnitsMetric
    };

    enum TDigitType {
        EDigitTypeUnknown = 0x0000,
        EDigitTypeWestern = 0x0030,
        EDigitTypeArabicIndic = 0x0660,
        EDigitTypeEasternArabicIndic = 0x6F0,
        EDigitTypeDevanagari = 0x0966,
        EDigitTypeBengali = 0x09E6,
        EDigitTypeGurmukhi = 0x0A66,
        EDigitTypeGujarati = 0x0AE6,
        EDigitTypeOriya = 0x0B66,
        EDigitTypeTamil = 0x0BE6,
        EDigitTypeTelugu = 0x0C66,
        EDigitTypeKannada = 0x0CE6,
        EDigitTypeMalayalam = 0x0D66,
        EDigitTypeThai = 0x0E50,
        EDigitTypeLao = 0x0ED0,
        EDigitTypeTibetan = 0x0F20,
        EDigitTypeMayanmar = 0x1040,
        EDigitTypeKhmer = 0x17E0,
        EDigitTypeAllTypes = 0xFFFF
    };

    enum TDeviceTimeState // must match TLocale:: version
    {
        EDeviceUserTime,
        ENITZNetworkTimeSync
    };

    struct TLocale {
        int iCountryCode;
        int iUniversalTimeOffset;
        TDateFormat iDateFormat;
        TTimeFormat iTimeFormat;
        TLocalePos iCurrencySymbolPosition;
        bool iCurrencySpaceBetween;
        int iCurrencyDecimalPlaces;
        TNegativeCurrencyFormat iNegativeCurrencyFormat;
        bool iCurrencyTriadsAllowed;
        int iThousandsSeparator;
        int iDecimalSeparator;
        int iDateSeparator[4];
        int iTimeSeparator[4];
        TLocalePos iAmPmSymbolPosition;
        bool iAmPmSpaceBetween;
        uint32_t iDaylightSaving;
        TDaylightSavingZone iHomeDaylightSavingZone;
        uint32_t iWorkDays;
        TDay iStartOfWeek;
        TClockFormat iClockFormat;
        TUnitsFormat iUnitsGeneral;
        TUnitsFormat iUnitsDistanceShort;
        TUnitsFormat iUnitsDistanceLong;
        uint32_t iExtraNegativeCurrencyFormatFlags;
        uint16_t iLanguageDowngrade[3];
        uint16_t iRegionCode;
        TDigitType iDigitType;
        TDeviceTimeState iDeviceTimeState;
        int iSpare[0x1E];
    };

    struct SLocaleLanguage {
        TLanguage iLanguage;
        eka2l1::ptr<char> iDateSuffixTable;
        eka2l1::ptr<char> iDayTable;
        eka2l1::ptr<char> iDayAbbTable;
        eka2l1::ptr<char> iMonthTable;
        eka2l1::ptr<char> iMonthAbbTable;
        eka2l1::ptr<char> iAmPmTable;
        eka2l1::ptr<uint16_t> iMsgTable;
    };

    TLocale GetEpocLocaleInfo() {
        TLocale locale;
#ifdef WIN32
        locale.iCountryCode = static_cast<int>(GetProfileInt("intl", "iCountry", 0));
#else
#endif
        locale.iClockFormat = EClockDigital;
        locale.iStartOfWeek = epoc::TDay::EMonday;
        locale.iDateFormat = epoc::TDateFormat::EDateAmerican;
        locale.iTimeFormat = epoc::TTimeFormat::ETime24;
        locale.iUniversalTimeOffset = -14400;
        locale.iDeviceTimeState = epoc::TDeviceTimeState::EDeviceUserTime;

        return locale;
    }
}

namespace eka2l1 {
    namespace service {
        void init_services(system *sys) {
            CREATE_SERVER_D(sys, applist_server);
            CREATE_SERVER(sys, featmgr_server);
            CREATE_SERVER(sys, fs_server);
            CREATE_SERVER(sys, fontbitmap_server);
            CREATE_SERVER(sys, window_server);

            auto lang = epoc::SLocaleLanguage{ TLanguage::ELangEnglish, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
            auto locale = epoc::GetEpocLocaleInfo();

            // Unknown key, testing show that this prop return 65535 most of times
            // The prop belongs to HAL server, but the key usuage is unknown. (TODO)
            DEFINE_INT_PROP_D(sys, sys_category, unk_key1, 65535);
            DEFINE_BIN_PROP(sys, sys_category, locale_lang_key, sizeof(epoc::SLocaleLanguage), lang);
            DEFINE_BIN_PROP(sys, sys_category, locale_data_key, sizeof(epoc::TLocale), locale);
        }
    }
}