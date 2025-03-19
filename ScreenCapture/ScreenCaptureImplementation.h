/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "Module.h"
#include <interfaces/Ids.h>
#include <interfaces/IScreenCapture.h>
#include <interfaces/IConfiguration.h>

#include <com/com.h>
#include <core/core.h>
#include <mutex>
#include <vector>

namespace WPEFramework
{
    namespace Plugin
    {
        class ScreenCaptureImplementation;

        class ScreenShotJob
        {
        private:
            ScreenShotJob() = delete;
            ScreenShotJob &operator=(const ScreenShotJob &RHS) = delete;

        public:
            ScreenShotJob(WPEFramework::Plugin::ScreenCaptureImplementation *tpt) : m_screenCapture(tpt) {}
            ScreenShotJob(const ScreenShotJob &copy) : m_screenCapture(copy.m_screenCapture) {}
            ~ScreenShotJob() {}

            inline bool operator==(const ScreenShotJob &RHS) const
            {
                return (m_screenCapture == RHS.m_screenCapture);
            }

        public:
            uint64_t Timed(const uint64_t scheduledTime);

        private:
            WPEFramework::Plugin::ScreenCaptureImplementation *m_screenCapture;
        };

        class ScreenCaptureImplementation : public Exchange::IScreenCapture, public Exchange::IConfiguration
        {
        public:
            // We do not allow this plugin to be copied !!
            ScreenCaptureImplementation();
            ~ScreenCaptureImplementation() override;

            static ScreenCaptureImplementation *instance(ScreenCaptureImplementation *ScreenCaptureImpl = nullptr);

            // We do not allow this plugin to be copied !!
            ScreenCaptureImplementation(const ScreenCaptureImplementation &) = delete;
            ScreenCaptureImplementation &operator=(const ScreenCaptureImplementation &) = delete;

            BEGIN_INTERFACE_MAP(ScreenCaptureImplementation)
            INTERFACE_ENTRY(Exchange::IScreenCapture)
            INTERFACE_ENTRY(Exchange::IConfiguration)
            END_INTERFACE_MAP

        public:
            enum Event
            {
                SCREENCAPTURE_EVENT_UPLOADCOMPLETE
            };
            class EXTERNAL Job : public Core::IDispatch
            {
            protected:
                Job(ScreenCaptureImplementation *ScreenCaptureImplementation, Event event, JsonValue &params)
                    : _screenCaptureImplementation(ScreenCaptureImplementation), _event(event), _params(params)
                {
                    if (_screenCaptureImplementation != nullptr)
                    {
                        _screenCaptureImplementation->AddRef();
                    }
                }

            public:
                Job() = delete;
                Job(const Job &) = delete;
                Job &operator=(const Job &) = delete;
                ~Job()
                {
                    if (_screenCaptureImplementation != nullptr)
                    {
                        _screenCaptureImplementation->Release();
                    }
                }

            public:
                static Core::ProxyType<Core::IDispatch> Create(ScreenCaptureImplementation *screenCaptureImplementation, Event event, JsonValue params)
                {
#ifndef USE_THUNDER_R4
                    return (Core::proxy_cast<Core::IDispatch>(Core::ProxyType<Job>::Create(screenCaptureImplementation, event, params)));
#else
                    return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<Job>::Create(screenCaptureImplementation, event, params)));
#endif
                }

                virtual void Dispatch()
                {
                    _screenCaptureImplementation->Dispatch(_event, _params);
                }

            private:
                ScreenCaptureImplementation *_screenCaptureImplementation;
                const Event _event;
                const JsonObject _params;
            };

        public:
            Core::hresult Register(Exchange::IScreenCapture::INotification *notification) override;
            Core::hresult Unregister(Exchange::IScreenCapture::INotification *notification) override;

            Core::hresult UploadScreenCapture(const string &url, const string &callGUID) override;

            bool uploadDataToUrl(const std::vector<unsigned char> &data, const char *url, std::string &error_str);

            bool doUploadScreenCapture(const std::vector<unsigned char> &png_data, bool got_screenshot);

            // IConfiguration interface
            uint32_t Configure(PluginHost::IShell *service) override;

        private:
            mutable Core::CriticalSection _adminLock;
            PluginHost::IShell *mShell;
            std::list<Exchange::IScreenCapture::INotification *> _screenCaptureNotification;

            void dispatchEvent(Event, const JsonObject &params);
            void Dispatch(Event event, const JsonObject params);

            std::mutex m_callMutex;

            WPEFramework::Core::TimerType<ScreenShotJob> *screenShotDispatcher;
            std::string url;
            std::string callGUID;
            friend class ScreenShotJob;

        public:
            static ScreenCaptureImplementation *_instance;

            friend class Job;
        };
    } // namespace Plugin
} // namespace WPEFramework
