/**
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

#include "ScreenCapture.h"
#include <interfaces/IConfiguration.h>

#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 0

namespace WPEFramework
{
	namespace
	{
		static Plugin::Metadata<Plugin::ScreenCapture> metadata(
			// Version (Major, Minor, Patch)
			API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH,
			// Preconditions
			{},
			// Terminations
			{},
			// Controls
			{});
	}

	namespace Plugin
	{
		/*
		 *Register ScreenCapture module as wpeframework plugin
		 **/
		SERVICE_REGISTRATION(ScreenCapture, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

		ScreenCapture::ScreenCapture() : _service(nullptr), _connectionId(0), _screenCapture(nullptr), _screenCaptureNotification(this)
		{
			SYSLOG(Logging::Startup, (_T("ScreenCapture Constructor")));
		}

		ScreenCapture::~ScreenCapture()
		{
			SYSLOG(Logging::Shutdown, (string(_T("ScreenCapture Destructor"))));
		}

		const string ScreenCapture::Initialize(PluginHost::IShell *service)
		{
			string message = "";

			ASSERT(nullptr != service);
			ASSERT(nullptr == _service);
			ASSERT(nullptr == _screenCapture);
			ASSERT(0 == _connectionId);

			SYSLOG(Logging::Startup, (_T("ScreenCapture::Initialize: PID=%u"), getpid()));

			_service = service;
			_service->AddRef();
			_service->Register(&_screenCaptureNotification);
			_screenCapture = _service->Root<Exchange::IScreenCapture>(_connectionId, 5000, _T("ScreenCaptureImplementation"));

			if (nullptr != _screenCapture)
			{
				auto configConnection = _screenCapture->QueryInterface<Exchange::IConfiguration>();
				if (configConnection != nullptr)
				{
					configConnection->Configure(service);
					configConnection->Release();
				}
				// Register for notifications
				_screenCapture->Register(&_screenCaptureNotification);
				// Invoking Plugin API register to wpeframework
				Exchange::JScreenCapture::Register(*this, _screenCapture);
			}
			else
			{
				SYSLOG(Logging::Startup, (_T("ScreenCapture::Initialize: Failed to initialise ScreenCapture plugin")));
				message = _T("ScreenCapture plugin could not be initialised");
			}

			if (0 != message.length())
			{
				Deinitialize(service);
			}

			return message;
		}

		void ScreenCapture::Deinitialize(PluginHost::IShell *service)
		{
			ASSERT(_service == service);

			SYSLOG(Logging::Shutdown, (string(_T("ScreenCapture::Deinitialize"))));

			// Make sure the Activated and Deactivated are no longer called before we start cleaning up..
			if (_service != nullptr)
			{
				_service->Unregister(&_screenCaptureNotification);
			}
			if (nullptr != _screenCapture)
			{
				_screenCapture->Unregister(&_screenCaptureNotification);
				Exchange::JScreenCapture::Unregister(*this);

				// Stop processing:
				RPC::IRemoteConnection *connection = service->RemoteConnection(_connectionId);
				VARIABLE_IS_NOT_USED uint32_t result = _screenCapture->Release();

				_screenCapture = nullptr;

				// It should have been the last reference we are releasing,
				// so it should endup in a DESTRUCTION_SUCCEEDED, if not we
				// are leaking...
				ASSERT(result == Core::ERROR_DESTRUCTION_SUCCEEDED);

				// If this was running in a (container) process...
				if (nullptr != connection)
				{
					// Lets trigger the cleanup sequence for
					// out-of-process code. Which will guard
					// that unwilling processes, get shot if
					// not stopped friendly :-)
					connection->Terminate();
					connection->Release();
				}
			}

			_connectionId = 0;

			if (_service != nullptr)
			{
				_service->Release();
				_service = nullptr;
			}
			SYSLOG(Logging::Shutdown, (string(_T("ScreenCapture de-initialised"))));
		}

		string ScreenCapture::Information() const
		{
			return ("This ScreenCapture Plugin facilitates to persist event data for monitoring applications");
		}

		void ScreenCapture::Deactivated(RPC::IRemoteConnection *connection)
		{
			if (connection->Id() == _connectionId)
			{
				ASSERT(nullptr != _service);
				Core::IWorkerPool::Instance().Submit(PluginHost::IShell::Job::Create(_service, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
			}
		}
	} // namespace Plugin
} // namespace WPEFramework
