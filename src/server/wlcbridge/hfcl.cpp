/*
** NetXMS - Network Management System
** Copyright (C) 2023-2024 Raden Solutions
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** File: hfcl.cpp
**
**/

#include "wlcbridge.h"
#include <nddrv.h>

#define DEBUG_TAG WLCBRIDGE_DEBUG_TAG _T(".hfcl")

#define MAX_AUTH_TOKEN_SIZE   512

/**
 * Get custom attribute from domain object as UTF-8 sting
 */
static std::string GetDomainAttribute(NObject *wirelessDomain, const TCHAR *name)
{
   TCHAR buffer[1024];
   if (wirelessDomain->getCustomAttribute(name, buffer, 1024) == nullptr)
      return std::string();
#ifdef UNICODE
   char utf8buffer[1024];
   wchar_to_utf8(buffer, -1, utf8buffer, 1024);
   return std::string(utf8buffer);
#else
   return std::string(buffer);
#endif
}

/**
 * Callback for processing headers received from cURL
 */
static size_t OnCurlHeaderReceived(char *ptr, size_t size, size_t nmemb, void *context)
{
   size_t bytes = size * nmemb;
   if ((bytes > 16) && !strncmp(ptr, "Authorization: ", 15))
   {
      if (bytes > MAX_AUTH_TOKEN_SIZE - 1)
      {
         nxlog_debug_tag(DEBUG_TAG, 5, _T("Authorization header is too long (%u bytes)"), static_cast<uint32_t>(bytes));
         return 0;   // Abort transfer - authorization header is too long
      }
      memcpy(context, ptr, bytes);
      if (static_cast<char*>(context)[bytes - 1] == '\n')
         static_cast<char*>(context)[bytes - 1] = 0;
      else
         static_cast<char*>(context)[bytes] = 0;
   }
   return bytes;
}

/**
 * Login to controller at given address
 */
static bool Login(const char *baseUrl, const char *login, const char *password, char *token)
{
   ByteStream responseData(2048);
   responseData.setAllocationStep(2048);

   char errorBuffer[CURL_ERROR_SIZE];

   CURL *curl = CreateCurlHandle(&responseData, errorBuffer);
   if (curl == nullptr)
      return false;

   char decryptedPassword[128];
   DecryptPasswordA(login, password, decryptedPassword, 128);

   json_t *request = json_object();
   json_object_set_new(request, "username", json_string(login));
   json_object_set_new(request, "password", json_string(decryptedPassword));
   char *data = json_dumps(request, 0);
   json_decref(request);
   curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);

   struct curl_slist *headers = nullptr;
   headers = curl_slist_append(headers, "Content-Type: application/json");
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &OnCurlHeaderReceived);
   curl_easy_setopt(curl, CURLOPT_HEADERDATA, token);

   bool success = true;

   char url[256];
   snprintf(url, 256, "%s/api/login/", baseUrl);
   if (curl_easy_setopt(curl, CURLOPT_URL, url) != CURLE_OK)
   {
      nxlog_debug_tag(DEBUG_TAG, 4, _T("Call to curl_easy_setopt(CURLOPT_URL) failed"));
      success = false;
   }

   if (success)
   {
      nxlog_debug_tag(DEBUG_TAG, 9, _T("Sending login request '%hs' to %hs"), data, url);
      memset(token, 0, MAX_AUTH_TOKEN_SIZE);
      CURLcode rc = curl_easy_perform(curl);
      if (rc != CURLE_OK)
      {
         nxlog_debug_tag(DEBUG_TAG, 5, _T("Call to curl_easy_perform() failed (%d: %hs)"), rc, errorBuffer);
         success = false;
      }
   }

   if (success)
   {
      long httpCode = 0;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
      if (httpCode != 200)
      {
         nxlog_debug_tag(DEBUG_TAG, 5, _T("Error response from controller: HTTP response code is %d"), httpCode);
         success = false;
      }
   }

   if (success && (token[0] == 0))
   {
      nxlog_debug_tag(DEBUG_TAG, 5, _T("Authorization token not provided in login response (likely incorrect credentials)"));
      success = false;
   }

   curl_slist_free_all(headers);
   curl_easy_cleanup(curl);
   MemFree(data);

   return success;
}

/**
 * Read JSON document from controller
 */
static json_t *ReadJsonFromController(const char *baseUrl, const char *endpoint, const char *token)
{
   ByteStream responseData(32768);
   responseData.setAllocationStep(32768);

   char errorBuffer[CURL_ERROR_SIZE];

   CURL *curl = CreateCurlHandle(&responseData, errorBuffer);
   if (curl == nullptr)
      return nullptr;

   struct curl_slist *headers = nullptr;
   headers = curl_slist_append(headers, "Content-Type: application/json");
   headers = curl_slist_append(headers, token);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   bool success = true;

   char url[256];
   snprintf(url, 256, "%s/api/%s", baseUrl, endpoint);
   if (curl_easy_setopt(curl, CURLOPT_URL, url) != CURLE_OK)
   {
      nxlog_debug_tag(DEBUG_TAG, 4, _T("Call to curl_easy_setopt(CURLOPT_URL) failed"));
      success = false;
   }

   if (success)
   {
      CURLcode rc = curl_easy_perform(curl);
      if (rc != CURLE_OK)
      {
         nxlog_debug_tag(DEBUG_TAG, 5, _T("Call to curl_easy_perform(%hs) failed (%d: %hs)"), url, rc, errorBuffer);
         success = false;
      }
   }

   if (success)
   {
      nxlog_debug_tag(DEBUG_TAG, 7, _T("Got %d bytes from %hs"), static_cast<int>(responseData.size()), url);
      long httpCode = 0;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
      if (httpCode != 200)
      {
         nxlog_debug_tag(DEBUG_TAG, 5, _T("Error response from controller: HTTP response code is %d"), httpCode);
         success = false;
      }
   }

   if (success && responseData.size() <= 0)
   {
      nxlog_debug_tag(DEBUG_TAG, 5, _T("Empty response from controller"));
      success = false;
   }

   json_t *json = nullptr;
   if (success)
   {
      responseData.write('\0');
      const char *data = reinterpret_cast<const char*>(responseData.buffer());
      json_error_t error;
      json = json_loads(data, 0, &error);
      if (json == nullptr)
      {
         nxlog_debug_tag(DEBUG_TAG, 4, _T("Failed to parse JSON on line %d: %hs"), error.line, error.text);
         success = false;
      }
   }

   curl_slist_free_all(headers);
   curl_easy_cleanup(curl);
   return json;
}

/**
 * Do request from controller
 */
static json_t *DoRequest(NObject *wirelessDomain, const char *endpoint)
{
   if (IsShutdownInProgress())
      return nullptr;

   std::string baseUrl = GetDomainAttribute(wirelessDomain, _T("hfcl.base-url"));
   if (baseUrl.length() == 0)
   {
      nxlog_debug_tag(DEBUG_TAG, 5, _T("Controller base URL not provided"));
      return nullptr;
   }

   char token[MAX_AUTH_TOKEN_SIZE];
   strlcpy(token, GetDomainAttribute(wirelessDomain, _T("$hfcl.token")).c_str(), MAX_AUTH_TOKEN_SIZE);
   if (token[0] == 0)
   {
      if (!Login(baseUrl.c_str(), GetDomainAttribute(wirelessDomain, _T("hfcl.login")).c_str(), GetDomainAttribute(wirelessDomain, _T("hfcl.password")).c_str(), token))
      {
         nxlog_debug_tag(DEBUG_TAG, 5, _T("Login to controller at %hs failed"), baseUrl.c_str());
         return nullptr;
      }
      wirelessDomain->setCustomAttribute(_T("$hfcl.token"), SharedString(token, "ASCII"), StateChange::CLEAR);
      nxlog_debug_tag(DEBUG_TAG, 5, _T("Login to controller at %hs successful"), baseUrl.c_str());
   }

   json_t *response = ReadJsonFromController(baseUrl.c_str(), endpoint, token);
   if (response == nullptr)
      return nullptr;

   int status = json_object_get_int32(response, "status", -1);
   if ((status >= 200) && (status <= 299))
      return response;

   nxlog_debug_tag(DEBUG_TAG, 5, _T("Error response from controller: status code %d (%hs)"), status, json_object_get_string_utf8(response, "msg", "error message not provided"));
   json_decref(response);
   if (status != 401)
      return nullptr;

   if (!Login(baseUrl.c_str(), GetDomainAttribute(wirelessDomain, _T("hfcl.login")).c_str(), GetDomainAttribute(wirelessDomain, _T("hfcl.password")).c_str(), token))
   {
      nxlog_debug_tag(DEBUG_TAG, 5, _T("Login to controller at %hs failed"), baseUrl.c_str());
      return nullptr;
   }
   wirelessDomain->setCustomAttribute(_T("$hfcl.token"), SharedString(token, "ASCII"), StateChange::CLEAR);
   nxlog_debug_tag(DEBUG_TAG, 5, _T("Login to controller at %hs successful"), baseUrl.c_str());

   response = ReadJsonFromController(baseUrl.c_str(), endpoint, token);
   if (response == nullptr)
      return nullptr;

   status = json_object_get_int32(response, "status", -1);
   if ((status >= 200) && (status <= 299))
      return response;

   nxlog_debug_tag(DEBUG_TAG, 5, _T("Error response from controller: status code %d (%hs)"), status, json_object_get_string_utf8(response, "msg", "error message not provided"));
   json_decref(response);
   return nullptr;
}

/**
 * Access point data cache
 */
static ObjectMemoryPool<AccessPointCacheEntry> s_apMemPool;
typedef char SerialNumber[16];
static HashMap<SerialNumber, AccessPointCacheEntry> s_apCache(Ownership::True,
   [] (void *e, HashMapBase *hashMap) -> void
   {
      s_apMemPool.destroy(static_cast<AccessPointCacheEntry*>(e));
   });
static Mutex s_apCacheLock(MutexType::FAST);

/**
 * Get access point data from cache or bridge process. Assumes lock on s_apCacheLock.
 */
static json_t *GetAccessPointData(NObject *wirelessDomain, const char *serial)
{
   SerialNumber key;
   memset(key, 0, sizeof(key));
   strlcpy(key, serial, sizeof(key));

   AccessPointCacheEntry *ce = s_apCache.get(key);
   if ((ce != nullptr) && (ce->timestamp >= time(nullptr) - 10))
      return ce->data;

   if (ce == nullptr)
   {
      ce = s_apMemPool.create();
      s_apCache.set(key, ce);
   }

   // Make sure that only one thread sends request to bridge process
   if (!ce->processing)
   {
      ce->processing = true;
      s_apCacheLock.unlock();

      char endpoint[64] = "devices/retrieve/";
      strlcat(endpoint, serial, 64);
      strlcat(endpoint, "/", 64);
      json_t *response = DoRequest(wirelessDomain, endpoint);
      json_t *ap = nullptr;
      if (response != nullptr)
      {
         ap = json_object_get(response, "data");
         if (json_is_object(ap))
         {
            json_incref(ap);
         }
         else
         {
            ap = nullptr;
            nxlog_debug_tag(DEBUG_TAG, 5, _T("GetAccessPointData: invalid document received from controller (apSerial=%hs)"), serial);
         }
         json_decref(response);
      }
      else
      {
         nxlog_debug_tag(DEBUG_TAG, 5, _T("GetAccessPointData: cannot read access point details from controller (apSerial=%hs)"), serial);
      }

      s_apCacheLock.lock();

      json_decref(ce->data);
      ce->data = ap;
      ce->timestamp = time(nullptr);
      ce->processing = false;
   }
   else
   {
      // Wait for other thread to complete request
      do
      {
         s_apCacheLock.unlock();
         ThreadSleepMs(200);
         s_apCacheLock.lock();
      } while(ce->processing);
   }

   return ce->data;
}

/**
 * Get radios for AP
 */
static void GetAccessPointRadios(NObject *wirelessDomain, const char *apSerial, AccessPointInfo *ap)
{
   LockGuard lockGuard(s_apCacheLock);

   json_t *data = GetAccessPointData(wirelessDomain, apSerial);
   if (data == nullptr)
      return;

   int count = json_object_get_int32(data, "radio_count");
   for(int i = 0; i < count; i++)
   {
      RadioInterfaceInfo radio;
      memset(&radio, 0, sizeof(radio));
      radio.index = i;
      _sntprintf(radio.name, MAX_OBJECT_NAME, _T("radio%d"), i);

      char tag[32];
      snprintf(tag, 32, "radio_mac_%d", i);
      MacAddress bssid = MacAddress::parse(json_object_get_string_utf8(data, tag, ""));
      if (bssid.isValid())
         memcpy(radio.bssid, bssid.value(), 6);

      TCHAR wlanList[256];
      utf8_to_tchar(json_object_get_string_utf8(data, "wlan_list", ""), -1, wlanList, 256);
      TCHAR *p = _tcschr(wlanList, _T(';')); // Take only first SSID - how to handle multiple SSIDs?
      if (p != nullptr)
         *p = 0;
      _tcslcpy(radio.ssid, wlanList, MAX_SSID_LENGTH);

      // TODO: read channels and power if they can be matched to BSSID - currently unclear

      ap->addRadioInterface(radio);
   }
}

/**
 * Get access points
 */
static ObjectArray<AccessPointInfo> *GetAccessPoints(NObject *wirelessDomain)
{
   json_t *response = DoRequest(wirelessDomain, "devices/list/");
   if (response == nullptr)
      return nullptr;

   auto accessPoints = new ObjectArray<AccessPointInfo>(0, 128, Ownership::True);
   json_t *list = json_object_get(response, "results");
   if (json_is_array(list))
   {
      size_t i;
      json_t *element;
      json_array_foreach(list, i, element)
      {
         MacAddress macAddress = MacAddress::parse(json_object_get_string_utf8(element, "ap_mac", ""));
         InetAddress ipAddress = InetAddress::parse(json_object_get_string_utf8(element, "deviceIP", ""));
         String name(json_object_get_string_utf8(element, "ap_name", ""), "utf8");
         String model(json_object_get_string_utf8(element, "ap_model", ""), "utf8");
         String serial(json_object_get_string_utf8(element, "serial_no", ""), "utf8");
         int connected = json_object_get_int32(element, "is_connected");

         AccessPointInfo *ap = new AccessPointInfo(i, macAddress, ipAddress, connected ? AP_UP : AP_DOWN, !name.isEmpty() ? name : serial, _T("HFCL"), model, serial);
         GetAccessPointRadios(wirelessDomain, json_object_get_string_utf8(element, "serial_no", ""), ap);
         accessPoints->add(ap);
      }
   }

   json_decref(response);
   return accessPoints;
}

/**
 * Get access point state
 */
static AccessPointState GetAccessPointState(NObject *wirelessDomain, uint32_t apIndex, const MacAddress& macAddr, const InetAddress& ipAddr, const TCHAR *serial, const StructArray<RadioInterfaceInfo>& radioInterfaces)
{
   LockGuard lockGuard(s_apCacheLock);

   char utf8serial[64];
   tchar_to_utf8(serial, -1, utf8serial, 64);
   json_t *ap = GetAccessPointData(wirelessDomain, utf8serial);
   if (ap == nullptr)
      return AP_UNKNOWN;

   json_t *connected = json_object_get(ap, "is_connected");
   return json_is_integer(connected) ? (json_integer_value(connected) ? AP_UP : AP_DOWN) : AP_UNKNOWN;
}

/**
 * Get access point metric
 */
static DataCollectionError GetAccessPointMetric(NObject *wirelessDomain, uint32_t apIndex, const MacAddress& macAddr, const InetAddress& ipAddr, const TCHAR *serial, const TCHAR *name, TCHAR *value, size_t size)
{
   LockGuard lockGuard(s_apCacheLock);

   char utf8serial[64];
   tchar_to_utf8(serial, -1, utf8serial, 64);
   json_t *ap = GetAccessPointData(wirelessDomain, utf8serial);
   if (ap == nullptr)
   {
      nxlog_debug_tag(DEBUG_TAG, 6, _T("GetAccessPointMetric(%s/%s, %s): cannot read access point data"), wirelessDomain->getName(), serial, name);
      return DCE_COLLECTION_ERROR;
   }

   return GetValueFromJson(ap, name, value, size);
}

/**
 * Create wireless station info from JSON document
 */
static WirelessStationInfo *WirelessStationInfoFromJSON(json_t *client)
{
   auto ws = new WirelessStationInfo;
   memset(ws, 0, sizeof(WirelessStationInfo));
   memcpy(ws->macAddr, MacAddress::parse(json_object_get_string_utf8(client, "client_mac", "00:00:00:00:00:00")).value(), MAC_ADDR_LENGTH);
   ws->ipAddr = InetAddress::parse(json_object_get_string_utf8(client, "ip_address", "0.0.0.0"));
   ws->vlan = json_object_get_int32(client, "vlan_id", 0);
   memcpy(ws->bssid, MacAddress::parse(json_object_get_string_utf8(client, "bssid", "00:00:00:00:00:00")).value(), MAC_ADDR_LENGTH);
   ws->rssi = json_object_get_int32(client, "rssi", 0);
   utf8_to_wchar(json_object_get_string_utf8(client, "ssid", ""), -1, ws->ssid, MAX_SSID_LENGTH);

   // Assume that 5GHz radio is a second interface
   const char *iface = json_object_get_string_utf8(client, "interface", "");
   if (!strcmp(iface, "5GHz"))
      ws->rfIndex = 1;
   else
      ws->rfIndex = 0;

   return ws;
}

/**
 * Get wireless stations
 */
static ObjectArray<WirelessStationInfo> *GetWirelessStations(NObject *wirelessDomain)
{
   json_t *response = DoRequest(wirelessDomain, "devices/list/");
   if (response == nullptr)
      return nullptr;

   auto stations = new ObjectArray<WirelessStationInfo>(0, 128, Ownership::True);
   json_t *list = json_object_get(response, "results");
   if (json_is_array(list))
   {
      size_t i;
      json_t *ap;
      json_array_foreach(list, i, ap)
      {
         const char *serial = json_object_get_string_utf8(ap, "serial_no", "");
         char endpoint[64];
         snprintf(endpoint, 64, "sites/ap-client/list/%s/", serial);
         json_t *apResponse = DoRequest(wirelessDomain, endpoint);
         if (apResponse != nullptr)
         {
            json_t *stationList = json_object_get(response, "results");
            if (json_is_array(stationList))
            {
               size_t j;
               json_t *station;
               json_array_foreach(stationList, j, station)
               {
                  WirelessStationInfo *ws = WirelessStationInfoFromJSON(station);
                  ws->apMatchPolicy = AP_MATCH_BY_SERIAL;
                  utf8_to_tchar(serial, -1, ws->apSerial, sizeof(ws->apSerial) / sizeof(TCHAR) - 1);
                  stations->add(ws);
               }
            }
            json_decref(apResponse);
         }
      }
   }

   json_decref(response);
   return stations;
}

/**
 * Get list of stations registered at given controller
 */
static ObjectArray<WirelessStationInfo> *GetAccessPointWirelessStations(NObject *wirelessDomain, uint32_t apIndex, const MacAddress& macAddr, const InetAddress& ipAddr, const TCHAR *serial)
{
   char utf8serial[64];
   tchar_to_utf8(serial, -1, utf8serial, 64);
   char endpoint[64] = "sites/ap-client/list/";
   strlcat(endpoint, utf8serial, 64);
   strlcat(endpoint, "/", 64);

   json_t *response = DoRequest(wirelessDomain, endpoint);
   if (response == nullptr)
      return nullptr;

   auto stations = new ObjectArray<WirelessStationInfo>(0, 128, Ownership::True);

   json_t *list = json_object_get(response, "results");
   if (json_is_array(list))
   {
      size_t i;
      json_t *element;
      json_array_foreach(list, i, element)
      {
         stations->add(WirelessStationInfoFromJSON(element));
      }
   }

   json_decref(response);
   return stations;
}

/**
 * Bridge interface
 */
WirelessControllerBridge g_hfclBridge =
{
   GetAccessPoints,
   GetWirelessStations,
   GetAccessPointState,
   GetAccessPointMetric,
   GetAccessPointWirelessStations
};