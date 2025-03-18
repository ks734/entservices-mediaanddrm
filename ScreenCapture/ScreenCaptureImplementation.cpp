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

#include "ScreenCaptureImplementation.h"

#include "UtilsJsonRpc.h"

#include <png.h>
#include <curl/curl.h>

#ifdef HAS_SCREENCAPTURE_PLATFORM
#include "screencaptureplatform.h"
#elif USE_DRM_SCREENCAPTURE
#include "Implementation/drm/drmsc.h"
#else
#error "Can't build without drm or platform getter !"
#endif

#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 0

namespace WPEFramework
{
    namespace Plugin
    {

        SERVICE_REGISTRATION(ScreenCaptureImplementation, 1, 0);
        ScreenCaptureImplementation *ScreenCaptureImplementation::_instance = nullptr;

        ScreenCaptureImplementation::ScreenCaptureImplementation()
            : _adminLock(), mShell(nullptr)
        {
            LOGINFO("Create ScreenCaptureImplementation Instance");
            ScreenCaptureImplementation::_instance = this;
        }

        ScreenCaptureImplementation::~ScreenCaptureImplementation()
        {
            LOGINFO("Call ScreenCaptureImplementation destructor\n");

            ScreenCaptureImplementation::_instance = nullptr;
            mShell = nullptr;
        }

        /**
         * Register a notification callback
         */
        Core::hresult ScreenCaptureImplementation::Register(Exchange::IScreenCapture::INotification *notification)
        {
            ASSERT(nullptr != notification);

            _adminLock.Lock();

            // Make sure we can't register the same notification callback multiple times
            if (std::find(_screenCaptureNotification.begin(), _screenCaptureNotification.end(), notification) == _screenCaptureNotification.end())
            {
                _screenCaptureNotification.push_back(notification);
                notification->AddRef();
            }
            else
            {
                LOGERR("same notification is registered already");
            }

            _adminLock.Unlock();

            return Core::ERROR_NONE;
        }

        /**
         * Unregister a notification callback
         */
        Core::hresult ScreenCaptureImplementation::Unregister(Exchange::IScreenCapture::INotification *notification)
        {
            Core::hresult status = Core::ERROR_GENERAL;

            ASSERT(nullptr != notification);

            _adminLock.Lock();

            // we just unregister one notification once
            auto itr = std::find(_screenCaptureNotification.begin(), _screenCaptureNotification.end(), notification);
            if (itr != _screenCaptureNotification.end())
            {
                (*itr)->Release();
                LOGINFO("Unregister notification");
                _screenCaptureNotification.erase(itr);
                status = Core::ERROR_NONE;
            }
            else
            {
                LOGERR("notification not found");
            }

            _adminLock.Unlock();

            return status;
        }

        uint32_t ScreenCaptureImplementation::Configure(PluginHost::IShell *shell)
        {
            LOGINFO("Configuring ScreenCaptureImplementation");
            uint32_t result = Core::ERROR_NONE;
            ASSERT(service != nullptr);
            mShell = shell;
            mShell->AddRef();

            return result;
        }

        void ScreenCaptureImplementation::dispatchEvent(Event event, const JsonObject &params)
        {
            Core::IWorkerPool::Instance().Submit(Job::Create(this, event, params));
        }

        void ScreenCaptureImplementation::Dispatch(Event event, const JsonObject params)
        {
            _adminLock.Lock();

            std::list<Exchange::IScreenCapture::INotification *>::const_iterator index(_screenCaptureNotification.begin());

            switch (event)
            {
            case SCREENCAPTURE_EVENT_UPLOADCOMPLETE:
                while (index != _screenCaptureNotification.end())
                {
                    bool status = params["status"].Boolean();
                    string message = params["message"].String();
                    string call_guid = params["call_guid"].String();
                    (*index)->UploadComplete(status, message, call_guid);
                    ++index;
                }
                break;

            default:
                LOGWARN("Event[%u] not handled", event);
                break;
            }
            _adminLock.Unlock();
        }

        Core::hresult ScreenCaptureImplementation::UploadScreenCapture(const string &url, const string &callGUID)
        {

            std::lock_guard<std::mutex> guard(m_callMutex);

            LOGINFO();

            if (url.empty())
            {
                LOGERR("Upload url is not specified");
                return Core::ERROR_GENERAL;
            }
            this->url = url;
            
            if (!callGUID.empty())
            {
                this->callGUID = callGUID;
                screenShotDispatcher->Schedule(Core::Time::Now().Add(0), ScreenShotJob(this));

                return Core::ERROR_NONE;
            }
            
            return Core::ERROR_GENERAL;
        }

        static void PngWriteCallback(png_structp png_ptr, png_bytep data, png_size_t length)
        {
            std::vector<unsigned char> *p = (std::vector<unsigned char> *)png_get_io_ptr(png_ptr);
            p->insert(p->end(), data, data + length);
        }

        static bool saveToPng(unsigned char *data, int width, int height, std::vector<unsigned char> &png_out_data)
        {
            int bitdepth = 8;
            int colortype = PNG_COLOR_TYPE_RGBA;
            int pitch = 4 * width;
            int transform = PNG_TRANSFORM_IDENTITY;

            int i = 0;
            int r = 0;
            png_structp png_ptr = NULL;
            png_infop info_ptr = NULL;
            png_bytep *row_pointers = NULL;

            if (NULL == data)
            {
                LOGERR("Error: failed to save the png because the given data is NULL.");
                r = -1;
                goto error;
            }

            if (0 == pitch)
            {
                LOGERR("Error: failed to save the png because the given pitch is 0.");
                r = -3;
                goto error;
            }

            png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
            if (NULL == png_ptr)
            {
                LOGERR("Error: failed to create the png write struct.");
                r = -5;
                goto error;
            }

            info_ptr = png_create_info_struct(png_ptr);
            if (NULL == info_ptr)
            {
                LOGERR("Error: failed to create the png info struct.");
                r = -6;
                goto error;
            }

            png_set_IHDR(png_ptr,
                         info_ptr,
                         width,
                         height,
                         bitdepth,           /* e.g. 8 */
                         colortype,          /* PNG_COLOR_TYPE_{GRAY, PALETTE, RGB, RGB_ALPHA, GRAY_ALPHA, RGBA, GA} */
                         PNG_INTERLACE_NONE, /* PNG_INTERLACE_{NONE, ADAM7 } */
                         PNG_COMPRESSION_TYPE_BASE,
                         PNG_FILTER_TYPE_BASE);

            row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * height);

            for (i = 0; i < height; ++i)
                row_pointers[i] = data + i * pitch;

            png_set_write_fn(png_ptr, &png_out_data, PngWriteCallback, NULL);
            png_set_rows(png_ptr, info_ptr, row_pointers);
            png_write_png(png_ptr, info_ptr, transform, NULL);

        error:

            if (NULL != png_ptr)
            {

                if (NULL == info_ptr)
                {
                    LOGERR("Error: info ptr is null. not supposed to happen here.\n");
                }

                png_destroy_write_struct(&png_ptr, &info_ptr);
                png_ptr = NULL;
                info_ptr = NULL;
            }

            if (NULL != row_pointers)
            {
                free(row_pointers);
                row_pointers = NULL;
            }

            return (r == 0);
        }

#ifdef USE_DRM_SCREENCAPTURE
        static bool getScreenContent(std::vector<unsigned char> &png_out_data)
        {
            bool ret = true;
            uint8_t *buffer = nullptr;
            DRMScreenCapture *handle = nullptr;
            uint32_t size;
            do
            {
                handle = DRMScreenCapture_Init();
                if (handle == nullptr)
                {
                    LOGERR("[SCREENCAP] fail to DRMScreenCapture_Init ");
                    ret = false;
                    break;
                }

                // get screen size
                ret = DRMScreenCapture_GetScreenInfo(handle);
                if (!ret)
                {
                    LOGERR("[SCREENCAP] fail to DRMScreenCapture_GetScreenInfo ");
                    break;
                }

                // allocate buffer
                size = handle->pitch * handle->height;
                buffer = (uint8_t *)malloc(size);
                if (!buffer)
                {
                    LOGERR("[SCREENCAP] out of memory, fail to allocate buffer size=%d", size);
                    ret = false;
                    break;
                }

                ret = DRMScreenCapture_ScreenCapture(handle, buffer, size);
                if (!ret)
                {
                    LOGERR("[SCREENCAP] fail to DRMScreenCapture_ScreenCapture ");
                    break;
                }

                // process the rgb buffer
                for (unsigned int n = 0; n < handle->height; n++)
                {
                    for (unsigned int i = 0; i < handle->width; i++)
                    {
                        unsigned char *color = buffer + n * handle->pitch + i * 4;
                        unsigned char blue = color[0];
                        color[0] = color[2];
                        color[2] = blue;
                    }
                }
                if (!saveToPng(buffer, handle->width, handle->height, png_out_data))
                {
                    LOGERR("[SCREENCAP] could not convert screenshot to png");
                    ret = false;
                    break;
                }

                LOGINFO("[SCREENCAP] done");
            } while (false);

            // Finish and clean up
            if (buffer)
            {
                free(buffer);
            }
            if (handle)
            {
                ret = DRMScreenCapture_Destroy(handle);
                if (!ret)
                    LOGERR("[SCREENCAP] fail to DRMScreenCapture_Destroy ");
            }

            return ret;
        }
#endif

        uint64_t ScreenShotJob::Timed(const uint64_t scheduledTime)
        {
            if (!m_screenCapture)
            {
                LOGERR("!m_screenCapture");
                return 0;
            }

            std::vector<unsigned char> png_data;
            bool got_screenshot = false;

            got_screenshot = getScreenContent(png_data);

            m_screenCapture->doUploadScreenCapture(png_data, got_screenshot);

            return 0;
        }

        bool ScreenCaptureImplementation::doUploadScreenCapture(const std::vector<unsigned char> &png_data, bool got_screenshot)
        {
            if (got_screenshot)
            {
                std::string error_str;

                LOGWARN("uploading %u of png data to '%s'", (uint32_t)png_data.size(), url.c_str());

                if (uploadDataToUrl(png_data, url.c_str(), error_str))
                {
                    JsonObject params;
                    params["status"] = true;
                    params["message"] = "Success";
                    params["call_guid"] = callGUID;

                    dispatchEvent(SCREENCAPTURE_EVENT_UPLOADCOMPLETE, params);

                    return true;
                }
                else
                {
                    JsonObject params;
                    params["status"] = false;
                    params["message"] = std::string("Upload Failed: ") + error_str;
                    params["call_guid"] = callGUID;

                    dispatchEvent(SCREENCAPTURE_EVENT_UPLOADCOMPLETE, params);

                    return false;
                }
            }
            else
            {
                LOGERR("Error: could not get the screenshot");

                JsonObject params;
                params["status"] = false;
                params["message"] = "Failed to get screen data";
                params["call_guid"] = callGUID;

                dispatchEvent(SCREENCAPTURE_EVENT_UPLOADCOMPLETE, params);

                return false;
            }
        }

        bool ScreenCaptureImplementation::uploadDataToUrl(const std::vector<unsigned char> &data, const char *url, std::string &error_str)
        {
            CURL *curl;
            CURLcode res;
            bool call_succeeded = true;

            if (!url || !strlen(url))
            {
                LOGERR("no url given");
                return false;
            }

            LOGWARN("uploading png data of size %u to '%s'", (uint32_t)data.size(), url);

            // init curl
            curl_global_init(CURL_GLOBAL_ALL);
            curl = curl_easy_init();

            if (!curl)
            {
                LOGERR("could not init curl\n");
                return false;
            }

            // create header
            struct curl_slist *chunk = NULL;
            chunk = curl_slist_append(chunk, "Content-Type: image/png");

            // set url and data
            res = curl_easy_setopt(curl, CURLOPT_URL, url);
            if (res != CURLE_OK)
            {
                LOGERR("CURLOPT_URL failed URL with error code: %s\n", curl_easy_strerror(res));
            }
            res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
            if (res != CURLE_OK)
            {
                LOGERR("Failed to set CURLOPT_HTTPHEADER with error code  %s\n", curl_easy_strerror(res));
            }
            res = curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.size());
            if (res != CURLE_OK)
            {
                LOGERR("Failed to set CURLOPT_POSTFIELDSIZE with error code  %s\n", curl_easy_strerror(res));
            }
            res = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, &data[0]);
            if (res != CURLE_OK)
            {
                LOGERR("Failed to set CURLOPT_POSTFIELDS with code  %s\n", curl_easy_strerror(res));
            }

            // perform blocking upload call
            res = curl_easy_perform(curl);

            // output success / failure log
            if (CURLE_OK == res)
            {
                long response_code;

                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

                if (600 > response_code && response_code >= 400)
                {
                    LOGERR("uploading failed with response code %ld\n", response_code);
                    error_str = std::string("response code:") + std::to_string(response_code);
                    call_succeeded = false;
                }
                else
                    LOGWARN("upload done");
            }
            else
            {
                LOGERR("upload failed with error %d:'%s'", res, curl_easy_strerror(res));
                error_str = std::to_string(res) + std::string(":'") + std::string(curl_easy_strerror(res)) + std::string("'");
                call_succeeded = false;
            }

            // clean up curl object
            curl_easy_cleanup(curl);
            curl_slist_free_all(chunk);

            return call_succeeded;
        }

    } // namespace Plugin
} // namespace WPEFramework
