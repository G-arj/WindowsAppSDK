﻿// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.

#pragma once
#include "pch.h"
#include <winrt/Windows.Foundation.h>
#include <wrl/implements.h>
#include <wil/resource.h>
#include <ToastActivatedEventArgs.h>

#include <FrameworkUdk/toastnotificationsrt.h>

const std::wstring c_appIdentifierPath{ LR"(Software\Classes\AppUserModelId\)" };
const std::wstring c_clsIdPath{ LR"(Software\Classes\CLSID\)" };
const std::wstring c_quote{ LR"(")" };
const std::wstring c_toastActivatedArgument{ L" ----AppNotificationActivated:" };

winrt::event<winrt::Windows::Foundation::EventHandler<winrt::Microsoft::Windows::ToastNotifications::ToastActivatedEventArgs>>& GetToastHandlers();

inline const int GUID_LENGTH = 39; // GUID + '{' + '}' + '/0'

inline std::wstring ConvertPathToKey(std::wstring path)
{
    for (int i = 0; i < path.length(); i++)
    {
        if (path[i] == '\\')
        {
            path[i] = '.';
        }
    }
    return path;
}

inline void RegisterValue(wil::unique_hkey const& hKey, PCWSTR const& key, const BYTE* value, DWORD const& valueType, size_t const& size)
{
    THROW_IF_FAILED(RegSetValueExW(hKey.get(), key, 0, valueType, value, (DWORD) size));
}

inline wil::unique_hstring safe_make_unique_hstring(PCWSTR str)
{
    // wil::make_unique_string can't allocate a zero byte memory buffer.
    return (str == nullptr || wcslen(str) == 0) ? wil::unique_hstring() : wil::make_unique_string<wil::unique_hstring>(str);
}

std::wstring RetrieveUnpackagedAppId();

std::wstring RetrieveAppId();

void RegisterAssets(std::wstring const& appId, winrt::Microsoft::Windows::ToastNotifications::ToastAssets const& activationInfo, wil::unique_cotaskmem_string const& clsid);

void RegisterComServer(wil::unique_cotaskmem_string const& processName, wil::unique_cotaskmem_string const& clsid);

void UnRegisterComServer(std::wstring const& clsid);

void UnRegisterAppIdentifierFromRegistry();

HRESULT GetActivatorGuid(std::wstring& activatorGuid) noexcept;

std::wstring RegisterComActivatorGuidAndAssets(winrt::Microsoft::Windows::ToastNotifications::ToastActivationInfo const& details);