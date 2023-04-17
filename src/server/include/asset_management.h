/*  * For clarity, attributes should more correctly be considered metadata. An attribute is frequently and generally a property of a property.
 * 
 */

/*
** NetXMS - Network Management System
** Copyright (C) 2003-2023 Raden Solutions
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
** File: asset_management.h
**/

#ifndef _asset_management_h_
#define _asset_management_h_

#define DEBUG_TAG_ASSET_MGMT _T("am")

/**
 * Asset management attribute data types
 */
enum class AMDataType
{
   String = 0,
   Integer = 1,
   Number = 2,
   Boolean = 3,
   Enum = 4,
   MacAddress = 5,
   IPAddress = 6,
   UUID = 7,
   ObjectReference = 8
};

/**
 * Asset management attribute system types
 */
enum class AMSystemType
{
   None = 0,
   Serial = 1,
   IPAddress = 2,
   MacAddress = 3,
   Vendor = 4,
   Model = 5
};

/**
 * Asset attribute (metadata). Term "property" is used to identify attribute instance.
 * Citation from Wikipedia: "For clarity, attributes should more correctly be considered metadata. An attribute is frequently and generally a property of a property."
 * https://en.wikipedia.org/wiki/Attribute_(computing)
 */
class AssetAttribute
{
private:
   TCHAR *m_name;
   TCHAR *m_displayName;
   AMDataType m_dataType;
   bool m_isMandatory;
   bool m_isUnique;
   TCHAR *m_autofillScriptSource;
   NXSL_Program *m_autofillScript;
   int32_t m_rangeMin;
   int32_t m_rangeMax;
   AMSystemType m_systemType;
   StringMap m_enumValues;

   void setScript(TCHAR *script);

public:
   AssetAttribute(const NXCPMessage &msg);
   AssetAttribute(DB_RESULT result, int row);
   AssetAttribute(const TCHAR *name, const ConfigEntry& entry);
   ~AssetAttribute();

   void loadEnumValues(DB_RESULT result);
   void fillMessage(NXCPMessage *msg, uint32_t baseId);
   void updateFromMessage(const NXCPMessage &msg);
   bool saveToDatabase() const;
   bool deleteFromDatabase();

   json_t *toJson() const;
   void fillEntry(StringBuffer &xml, int id);

   const TCHAR *getName() const { return m_name; }
   AMDataType getDataType() const { return m_dataType; }
   bool isMandatory() const { return m_isMandatory; }
   bool isUnique() const { return m_isUnique; }
   int32_t getMinRange() const { return m_rangeMin; }
   int32_t getMaxRange() const { return m_rangeMax; }
   NXSL_Program *getScript() const { return m_autofillScript; }

   const TCHAR *getActualDisplayName() const
   {
      return ((m_displayName != nullptr) && (*m_displayName != 0)) ? m_displayName : m_name;
   }

   bool isValidEnumValue(const TCHAR *value) const { return m_enumValues.contains(value); }
   bool isRangeSet() const { return (m_rangeMin != 0) || (m_rangeMax != 0); }
   bool hasScript() const { return m_autofillScript != nullptr; }
};

/**
 * Context for running autofill for asset property
 */
struct AssetPropertyAutofillContext
{
   TCHAR name[MAX_OBJECT_NAME];
   AMDataType dataType;
   NXSL_VM *vm;

   AssetPropertyAutofillContext(const TCHAR *name, AMDataType dataType, NXSL_VM *vm)
   {
      _tcslcpy(this->name, name, MAX_OBJECT_NAME);
      this->dataType = dataType;
      this->vm = vm;
   }

   ~AssetPropertyAutofillContext()
   {
      delete vm;
   }
};

// Attribute management finctions
void AssetManagementSchemaToMessage(NXCPMessage *msg);
uint32_t CreateAssetAttribute(const NXCPMessage& msg, const ClientSession& session);
uint32_t UpdateAssetAttribute(const NXCPMessage& msg, const ClientSession& session);
uint32_t DeleteAssetAttribute(const NXCPMessage& msg, const ClientSession& session);
std::pair<uint32_t, String> ValidateAssetPropertyValue(const TCHAR *name, const TCHAR *value);
String GetAssetAttributeDisplayName(const TCHAR *name);
bool IsMandatoryAssetProperty(const TCHAR *name);
bool IsBooleanAssetProperty(const TCHAR *name);
bool IsValidAssetPropertyName(const TCHAR *name);
unique_ptr<StringSet> GetAssetAttributeNames(bool mandatoryOnly = false);
unique_ptr<ObjectArray<AssetPropertyAutofillContext>> PrepareAssetPropertyAutofill(const shared_ptr<Asset>& asset);
void LinkAsset(const shared_ptr<Asset>& asset, const shared_ptr<NetObj>& object, ClientSession *session);
void UnlinkAsset(const shared_ptr<Asset>& asset, ClientSession *session);

#endif