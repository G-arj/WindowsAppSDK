﻿#include "pch.h"
#include "AppNotificationManager.h"
#include "Microsoft.Windows.AppNotifications.AppNotificationManager.g.cpp"
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Storage.h>
#include <winrt/base.h>
#include "AppNotificationActivationCallback.h"
#include "externs.h"
#include "PushNotificationUtility.h"
#include "AppNotificationUtility.h"
#include <frameworkudk/pushnotifications.h>
#include <frameworkudk/toastnotifications.h>
#include <FrameworkUdk/toastnotificationsrt.h>
#include "NotificationProperties.h"
#include "NotificationTransientProperties.h"
#include "AppNotification.h"
#include <NotificationProgressData.h>
#include <algorithm>
#include <winerror.h>
#include <string_view>

using namespace std::literals;

constexpr std::wstring_view expectedAppServerArgs = L"----AppNotificationActivated:"sv;

namespace winrt
{
    using namespace winrt::Windows::UI;
    using namespace winrt::Windows::Foundation;
    using namespace Windows::ApplicationModel::Core;
    using namespace winrt::Microsoft::Windows::AppNotifications;
}

namespace ToastABI
{
    using namespace ::ABI::Microsoft::Internal::ToastNotifications;
}

namespace PushNotificationHelpers
{
    using namespace winrt::Microsoft::Windows::PushNotifications::Helpers;
}

static winrt::event<NotificationActivationEventHandler> g_notificationHandlers;

winrt::event<NotificationActivationEventHandler>& Microsoft::Windows::AppNotifications::Helpers::GetAppNotificationHandlers()
{
    return g_notificationHandlers;
}

using namespace Microsoft::Windows::AppNotifications::Helpers;

namespace winrt::Microsoft::Windows::AppNotifications::implementation
{
    static wil::unique_com_class_object_cookie s_notificationComActivatorRegistration;

    winrt::Microsoft::Windows::AppNotifications::AppNotificationManager AppNotificationManager::Default()
    {
        static auto appNotificationManager{winrt::make<AppNotificationManager>()};
        return appNotificationManager;
    }

    void AppNotificationManager::Register(winrt::Microsoft::Windows::AppNotifications::AppNotificationActivationInfo const& details)
    {
        THROW_HR_IF_MSG(E_INVALIDARG, s_notificationComActivatorRegistration, "Toast activator already registered.");

        THROW_HR_IF_NULL(E_INVALIDARG, details);

        std::wstring storedComActivatorString;
        if (!PushNotificationHelpers::IsPackagedAppScenario())
        {
            std::wstring toastAppId{ RetrieveNotificationAppId() };
            if (!AppModel::Identity::IsPackagedProcess())
            {
                THROW_IF_FAILED(PushNotifications_RegisterFullTrustApplication(toastAppId.c_str(), GUID_NULL));

                storedComActivatorString = RegisterComActivatorGuidAndAssets(details);
                // Remove braces around the guid string
                storedComActivatorString = storedComActivatorString.substr(1, storedComActivatorString.size() - 2);
            }

            wil::unique_cotaskmem_string processName;
            THROW_IF_FAILED(GetCurrentProcessPath(processName));
            auto notificationPlatform{ PushNotificationHelpers::GetNotificationPlatform() };
            THROW_IF_FAILED(notificationPlatform->AddToastRegistrationMapping(processName.get(), toastAppId.c_str()));
        }

        if (AppModel::Identity::IsPackagedProcess())
        {
            winrt::guid registeredClsid{ GUID_NULL };
            THROW_IF_FAILED(PushNotificationHelpers::CheckComServerClsid(registeredClsid, expectedAppServerArgs.data()));
            THROW_HR_IF(E_INVALIDARG, registeredClsid != details.TaskClsid());
        }

        THROW_IF_FAILED(::CoRegisterClassObject(
            AppModel::Identity::IsPackagedProcess() ? details.TaskClsid() : winrt::guid(storedComActivatorString),
            winrt::make<AppNotificationActivationCallbackFactory>().get(),
            CLSCTX_LOCAL_SERVER,
            REGCLS_MULTIPLEUSE,
            &s_notificationComActivatorRegistration));

        GetWaitHandleForArgs().create();
    }

    void AppNotificationManager::Unregister()
    {
        THROW_HR_IF_MSG(HRESULT_FROM_WIN32(ERROR_NOT_FOUND), !s_notificationComActivatorRegistration, "Toast activator not registered.");

        s_notificationComActivatorRegistration.reset();

        if (!AppModel::Identity::IsPackagedProcess())
        {
            std::wstring storedComActivatorString;
            THROW_IF_WIN32_ERROR(GetActivatorGuid(storedComActivatorString));

            UnRegisterComServer(storedComActivatorString);
            UnRegisterNotificationAppIdentifierFromRegistry();

            wil::unique_cotaskmem_string processName;
            THROW_IF_FAILED(GetCurrentProcessPath(processName));
            auto notificationPlatform{ PushNotificationHelpers::GetNotificationPlatform() };
            THROW_IF_FAILED(notificationPlatform->RemoveToastRegistrationMapping(processName.get()));
        }
    }

    winrt::event_token AppNotificationManager::AppNotificationActivated(winrt::Windows::Foundation::TypedEventHandler<winrt::Microsoft::Windows::AppNotifications::AppNotificationManager, winrt::Microsoft::Windows::AppNotifications::AppNotificationActivatedEventArgs> const& handler)
    {
        return GetAppNotificationHandlers().add(handler);
    }

    void AppNotificationManager::AppNotificationActivated(winrt::event_token const& token) noexcept
    {
        GetAppNotificationHandlers().remove(token);
    }

    void AppNotificationManager::Show(winrt::Microsoft::Windows::AppNotifications::AppNotification const& notification)
    {
        THROW_HR_IF(WPN_E_NOTIFICATION_POSTED, notification.Id() != 0);

        winrt::com_ptr<::ABI::Microsoft::Internal::ToastNotifications::INotificationProperties> notificationProperties = winrt::make_self<NotificationProperties>(notification);

        winrt::com_ptr<::ABI::Microsoft::Internal::ToastNotifications::INotificationTransientProperties> notificationTransientProperties = winrt::make_self<NotificationTransientProperties>(notification);

        auto notificationAppId{ ::Microsoft::Windows::AppNotifications::Helpers::RetrieveNotificationAppId() };

        DWORD notificationId = 0;
        THROW_IF_FAILED(ToastNotifications_PostToast(notificationAppId.c_str(), notificationProperties.get(), notificationTransientProperties.get(), &notificationId));

        THROW_HR_IF(E_UNEXPECTED, notificationId == 0);

        winrt::Microsoft::Windows::AppNotifications::implementation::AppNotification* notificationImpl = get_self< winrt::Microsoft::Windows::AppNotifications::implementation::AppNotification>(notification);
        notificationImpl->SetNotificationId(notificationId);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::Windows::AppNotifications::AppNotificationProgressResult> AppNotificationManager::UpdateProgressDataAsync(winrt::Microsoft::Windows::AppNotifications::AppNotificationProgressData const data, hstring const tag, hstring const group)
    {
        THROW_HR_IF_MSG(E_INVALIDARG, tag == winrt::hstring(L""), "Update operation isn't guarenteed to find a specific notification to replace correctly.");

        std::wstring appId{ RetrieveNotificationAppId() };

        winrt::com_ptr<ToastABI::IToastProgressData> toastProgressData{ winrt::make_self<NotificationProgressData>(data) };

        auto hr = ToastNotifications_UpdateNotificationData(appId.c_str(), tag.c_str(), group.c_str(), toastProgressData.get());

        if (SUCCEEDED(hr))
        {
            co_return winrt::AppNotificationProgressResult::Succeeded;
        }
        else if (hr == E_NOT_SET)
        {
            co_return winrt::AppNotificationProgressResult::AppNotificationNotFound;
        }
        else
        {
            THROW_HR(hr);
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::Windows::AppNotifications::AppNotificationProgressResult> AppNotificationManager::UpdateProgressDataAsync(winrt::Microsoft::Windows::AppNotifications::AppNotificationProgressData const data, hstring const tag)
    {
        co_return co_await UpdateProgressDataAsync(data, tag, L"");
    }

    winrt::Microsoft::Windows::AppNotifications::AppNotificationSetting AppNotificationManager::Enablement()
    {
        std::wstring appId{ RetrieveNotificationAppId() };
        DWORD appNotificationSetting{ 0 };
        ToastNotifications_QuerySettings(appId.c_str(), &appNotificationSetting);
        return static_cast<winrt::Microsoft::Windows::AppNotifications::AppNotificationSetting>(appNotificationSetting);
    }

    winrt::Windows::Foundation::IAsyncAction AppNotificationManager::RemoveByIdAsync(uint32_t notificationId)
    {
        throw hresult_not_implemented();
    }

    winrt::Windows::Foundation::IAsyncAction AppNotificationManager::RemoveWithTagAsync(hstring tag)
    {
        throw hresult_not_implemented();
    }

    winrt::Windows::Foundation::IAsyncAction AppNotificationManager::RemoveWithTagGroupAsync(hstring tag, hstring group)
    {
        throw hresult_not_implemented();
    }

    winrt::Windows::Foundation::IAsyncAction AppNotificationManager::RemoveGroupAsync(hstring group)
    {
        throw hresult_not_implemented();
    }

    winrt::Windows::Foundation::IAsyncAction AppNotificationManager::RemoveAllAsync()
    {
        throw hresult_not_implemented();
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Foundation::Collections::IVector<winrt::Microsoft::Windows::AppNotifications::AppNotification>> AppNotificationManager::GetAllAsync()
    {
        throw hresult_not_implemented();
    }
}