/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
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
 **/

#pragma once

#include "Module.h"
#include <interfaces/json/JsonData_ScreenCapture.h>
#include <interfaces/json/JScreenCapture.h>
#include <interfaces/IScreenCapture.h>
#include "UtilsLogging.h"
#include "tracing/Logging.h"
#include <mutex>

namespace WPEFramework
{
    namespace Plugin
    {
        class ScreenCapture : public PluginHost::IPlugin, public PluginHost::JSONRPC
        {
        private:
            class Notification : public RPC::IRemoteConnection::INotification,
                                 public Exchange::IScreenCapture::INotification
            {
            private:
                Notification() = delete;
                Notification(const Notification &) = delete;
                Notification &operator=(const Notification &) = delete;

            public:
                explicit Notification(ScreenCapture *parent)
                    : _parent(*parent)
                {
                    ASSERT(parent != nullptr);
                }

                virtual ~Notification()
                {
                }

                BEGIN_INTERFACE_MAP(Notification)
                INTERFACE_ENTRY(Exchange::IScreenCapture::INotification)
                INTERFACE_ENTRY(RPC::IRemoteConnection::INotification)
                END_INTERFACE_MAP

                void Activated(RPC::IRemoteConnection *) override
                {
                    LOGINFO("ScreenCapture Notification Activated");
                }

                void Deactivated(RPC::IRemoteConnection *connection) override
                {
                    LOGINFO("ScreenCapture Notification Deactivated");
                    _parent.Deactivated(connection);
                }

                void UploadComplete(const bool &status, const string &message, const string &call_guid) override
                {
                    LOGINFO("UploadComplete: status: %d message: %s call_guid: %s \n", status, message.c_str(), call_guid.c_str());
                    Exchange::JScreenCapture::Event::UploadComplete(_parent, status, message, call_guid);
                }

            private:
                ScreenCapture &_parent;
            };

        public:
            // We do not allow this plugin to be copied !!
            ScreenCapture(const ScreenCapture &) = delete;
            ScreenCapture &operator=(const ScreenCapture &) = delete;

            ScreenCapture();
            virtual ~ScreenCapture();

            BEGIN_INTERFACE_MAP(ScreenCapture)
            INTERFACE_ENTRY(PluginHost::IPlugin)
            INTERFACE_ENTRY(PluginHost::IDispatcher)
            INTERFACE_AGGREGATE(Exchange::IScreenCapture, _screenCapture)
            END_INTERFACE_MAP

            //  IPlugin methods
            // -------------------------------------------------------------------------------------------------------
            const string Initialize(PluginHost::IShell *service) override;
            void Deinitialize(PluginHost::IShell *service) override;
            string Information() const override;

        private:
            void Deactivated(RPC::IRemoteConnection *connection);

        private:
            PluginHost::IShell *_service{};
            uint32_t _connectionId{};
            Exchange::IScreenCapture *_screenCapture{};
            Core::Sink<Notification> _screenCaptureNotification;
        };

    } // namespace Plugin
} // namespace WPEFramework
