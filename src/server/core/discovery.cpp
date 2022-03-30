/*
** NetXMS - Network Management System
** Copyright (C) 2003-2022 Victor Kirhenshtein
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
** File: discovery.cpp
**
**/

#include "nxcore.h"
#include <agent_tunnel.h>
#include <nxcore_discovery.h>

#define DEBUG_TAG_DISCOVERY      _T("poll.discovery")

/**
 * Node poller queue (polls new nodes)
 */
ObjectQueue<DiscoveredAddress> g_nodePollerQueue;

/**
 * Thread pool
 */
ThreadPool *g_discoveryThreadPool = nullptr;

/**
 * Discovery source type names
 */
static const TCHAR *s_discoveredAddrSourceTypeAsText[] = {
   _T("ARP Cache"),
   _T("Routing Table"),
   _T("Agent Registration"),
   _T("SNMP Trap"),
   _T("Syslog"),
   _T("Active Discovery"),
};

/**
 * IP addresses being processed by node poller
 */
static ObjectArray<DiscoveredAddress> s_processingList(64, 64, Ownership::False);
static Mutex s_processingListLock;

/**
 * Check if given address is in processing by new node poller
 */
static bool IsNodePollerActiveAddress(const InetAddress& addr)
{
   bool result = false;
   s_processingListLock.lock();
   for(int i = 0; i < s_processingList.size(); i++)
   {
      if (s_processingList.get(i)->ipAddr.equals(addr))
      {
         result = true;
         break;
      }
   }
   s_processingListLock.unlock();
   return result;
}

/**
 * Find existing node by MAC address to detect IP address change for already known node.
 * This function increments reference count for interface object.
 *
 * @param ipAddr new (discovered) IP address
 * @param zoneUIN zone ID
 * @param bMacAddr MAC address of discovered node, or nullptr if not known
 *
 * @return pointer to existing interface object with given MAC address or nullptr if no such interface found
 */
static shared_ptr<Interface> FindExistingNodeByMAC(const InetAddress& ipAddr, int32_t zoneUIN, const MacAddress& macAddr)
{
   TCHAR szIpAddr[64], szMacAddr[64];

   ipAddr.toString(szIpAddr);
   macAddr.toString(szMacAddr);
   nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("FindExistingNodeByMAC: IP=%s MAC=%s"), szIpAddr, szMacAddr);

   MacAddress nodeMacAddr;
   if (macAddr.isValid())
   {
      nodeMacAddr = macAddr;
   }
   else
   {
      shared_ptr<Subnet> subnet = FindSubnetForNode(zoneUIN, ipAddr);
      if (subnet != nullptr)
      {
         nodeMacAddr = subnet->findMacAddress(ipAddr);
         if (!nodeMacAddr.isValid())
         {
            nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("FindExistingNodeByMAC: MAC address not found"));
            return shared_ptr<Interface>();
         }
      }
      else
      {
         nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("FindExistingNodeByMAC: subnet not found"));
         return shared_ptr<Interface>();
      }
   }

   shared_ptr<Interface> iface = FindInterfaceByMAC(nodeMacAddr);
   if (iface == nullptr)
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("FindExistingNodeByMAC: returning null (FindInterfaceByMAC!)"));

   return iface;
}

/**
 * Check if host at given IP address is reachable by NetXMS server
 */
static bool HostIsReachable(const InetAddress& ipAddr, int32_t zoneUIN, bool fullCheck, SNMP_Transport **transport, shared_ptr<AgentConnection> *preparedAgentConnection)
{
   bool reachable = false;

   if (transport != nullptr)
      *transport = nullptr;
   if (preparedAgentConnection != nullptr)
      preparedAgentConnection->reset();

   uint32_t zoneProxy = 0;
   if (IsZoningEnabled() && (zoneUIN != 0))
   {
      shared_ptr<Zone> zone = FindZoneByUIN(zoneUIN);
      if (zone != nullptr)
      {
         zoneProxy = zone->getProxyNodeId(nullptr);
      }
   }

   // *** ICMP PING ***
   if (zoneProxy != 0)
   {
      shared_ptr<Node> proxyNode = static_pointer_cast<Node>(g_idxNodeById.get(zoneProxy));
      if ((proxyNode != nullptr) && proxyNode->isNativeAgent() && !proxyNode->isDown())
      {
         shared_ptr<AgentConnectionEx> conn = proxyNode->createAgentConnection();
         if (conn != nullptr)
         {
            TCHAR parameter[128], buffer[64];

            _sntprintf(parameter, 128, _T("Icmp.Ping(%s)"), ipAddr.toString(buffer));
            if (conn->getParameter(parameter, buffer, 64) == ERR_SUCCESS)
            {
               TCHAR *eptr;
               long value = _tcstol(buffer, &eptr, 10);
               if ((*eptr == 0) && (value >= 0))
               {
                  if (value < 10000)
                  {
                     reachable = true;
                  }
               }
            }
         }
      }
   }
   else  // not using ICMP proxy
   {
      if (IcmpPing(ipAddr, 3, g_icmpPingTimeout, nullptr, g_icmpPingSize, false) == ICMP_SUCCESS)
         reachable = true;
   }

   if (reachable && !fullCheck)
      return true;

   // *** NetXMS agent ***
   auto agentConnection = make_shared<AgentConnectionEx>(0, ipAddr, AGENT_LISTEN_PORT, nullptr);
   shared_ptr<Node> proxyNode;
   if (zoneProxy != 0)
   {
      proxyNode = static_pointer_cast<Node>(g_idxNodeById.get(zoneProxy));
      if (proxyNode != nullptr)
      {
         shared_ptr<AgentTunnel> tunnel = GetTunnelForNode(zoneProxy);
         if (tunnel != nullptr)
         {
            agentConnection->setProxy(tunnel, proxyNode->getAgentSecret());
         }
         else
         {
            agentConnection->setProxy(proxyNode->getIpAddress(), proxyNode->getAgentPort(), proxyNode->getAgentSecret());
         }
      }
   }

   agentConnection->setCommandTimeout(g_agentCommandTimeout);
   uint32_t rcc;
   if (!agentConnection->connect(g_pServerKey, &rcc))
   {
      // If there are authentication problem, try default shared secret
      if ((rcc == ERR_AUTH_REQUIRED) || (rcc == ERR_AUTH_FAILED))
      {
         StringList secrets;
         DB_HANDLE hdb = DBConnectionPoolAcquireConnection();
         DB_STATEMENT hStmt = DBPrepare(hdb, _T("SELECT secret FROM shared_secrets WHERE zone=? OR zone=-1 ORDER BY zone DESC, id ASC"));
         if (hStmt != nullptr)
         {
           DBBind(hStmt, 1, DB_SQLTYPE_INTEGER, (proxyNode != nullptr) ? proxyNode->getZoneUIN() : 0);

           DB_RESULT hResult = DBSelectPrepared(hStmt);
           if (hResult != nullptr)
           {
              int count = DBGetNumRows(hResult);
              for(int i = 0; i < count; i++)
                 secrets.addPreallocated(DBGetField(hResult, i, 0, nullptr, 0));
              DBFreeResult(hResult);
           }
           DBFreeStatement(hStmt);
         }
         DBConnectionPoolReleaseConnection(hdb);

         for (int i = 0; (i < secrets.size()) && !IsShutdownInProgress(); i++)
         {
            agentConnection->setSharedSecret(secrets.get(i));
            if (agentConnection->connect(g_pServerKey, &rcc))
            {
               break;
            }

            if (((rcc != ERR_AUTH_REQUIRED) || (rcc != ERR_AUTH_FAILED)))
            {
               break;
            }
         }
      }
   }
   if (rcc == ERR_SUCCESS)
   {
      if (preparedAgentConnection != nullptr)
         *preparedAgentConnection = agentConnection;
      reachable = true;
   }
   else
   {
      TCHAR ipAddrText[64];
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("HostIsReachable(%s): agent connection check failed with error %u (%s)%s%s"),
               ipAddr.toString(ipAddrText), rcc, AgentErrorCodeToText(rcc), (proxyNode != nullptr) ? _T(", proxy node ") : _T(""),
               (proxyNode != nullptr) ? proxyNode->getName() : _T(""));
   }

   if (reachable && !fullCheck)
      return true;

   // *** SNMP ***
   SNMP_Version version;
   StringList oids;
   oids.add(_T(".1.3.6.1.2.1.1.2.0"));
   oids.add(_T(".1.3.6.1.2.1.1.1.0"));
   AddDriverSpecificOids(&oids);
   SNMP_Transport *pTransport = SnmpCheckCommSettings(zoneProxy, ipAddr, &version, 0, nullptr, oids, zoneUIN);
   if (pTransport != nullptr)
   {
      if (transport != nullptr)
      {
         pTransport->setSnmpVersion(version);
         *transport = pTransport;
         pTransport = nullptr;   // prevent deletion
      }
      reachable = true;
      delete pTransport;
   }

   return reachable;
}

/**
 * Check if newly discovered node should be added
 */
static bool AcceptNewNode(NewNodeData *newNodeData, const MacAddress& macAddr)
{
   TCHAR szFilter[MAX_CONFIG_VALUE], szBuffer[256], szIpAddr[64];
   UINT32 dwTemp;
   SNMP_Transport *pTransport;

   newNodeData->ipAddr.toString(szIpAddr);
   if ((FindNodeByIP(newNodeData->zoneUIN, newNodeData->ipAddr) != nullptr) ||
       (FindSubnetByIP(newNodeData->zoneUIN, newNodeData->ipAddr) != nullptr))
   {
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): node already exist in database"), szIpAddr);
      return false;  // Node already exist in database
   }

   if (macAddr.isBroadcast())
   {
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): broadcast MAC address"), szIpAddr);
      return false;  // Broadcast MAC
   }

   NXSL_VM *hook = FindHookScript(_T("AcceptNewNode"), shared_ptr<NetObj>());
   if (hook != nullptr)
   {
      bool stop = false;
      hook->setGlobalVariable("$ipAddr", hook->createValue(szIpAddr));
      hook->setGlobalVariable("$ipNetMask", hook->createValue(newNodeData->ipAddr.getMaskBits()));
      hook->setGlobalVariable("$macAddr", hook->createValue(macAddr.toString(szBuffer)));
      hook->setGlobalVariable("$zoneUIN", hook->createValue(newNodeData->zoneUIN));
      if (hook->run())
      {
         NXSL_Value *result = hook->getResult();
         if (result->isFalse())
         {
            stop = true;
            nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): rejected by hook script"), szIpAddr);
         }
      }
      else
      {
         nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): hook script execution error: %s"), szIpAddr, hook->getErrorText());
      }
      delete hook;
      if (stop)
         return false;  // blocked by hook
   }

   int retryCount = 5;
   while (retryCount-- > 0)
   {
      shared_ptr<Interface> iface = FindExistingNodeByMAC(newNodeData->ipAddr, newNodeData->zoneUIN, macAddr);
      if (iface == nullptr)
         break;

      if (!HostIsReachable(newNodeData->ipAddr, newNodeData->zoneUIN, false, nullptr, nullptr))
      {
         nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): found existing interface with same MAC address, but new IP is not reachable"), szIpAddr);
         return false;
      }

      // Interface could be deleted by configuration poller while HostIsReachable was running
      shared_ptr<Node> oldNode = iface->getParentNode();
      if (!iface->isDeleted() && (oldNode != nullptr))
      {
         if (iface->getIpAddressList()->hasAddress(oldNode->getIpAddress()))
         {
            // we should change node's primary IP only if old IP for this MAC was also node's primary IP
            TCHAR szOldIpAddr[16];
            oldNode->getIpAddress().toString(szOldIpAddr);
            nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): node already exist in database with IP %s, will change to new"), szIpAddr, szOldIpAddr);
            oldNode->changeIPAddress(newNodeData->ipAddr);
         }
         return false;
      }

      ThreadSleepMs(100);
   }
   if (retryCount == 0)
   {
      // Still getting deleted interface
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): found existing but marked for deletion interface with same MAC address"), szIpAddr);
      return false;
   }

   // Allow filtering by loaded modules
   ENUMERATE_MODULES(pfAcceptNewNode)
   {
      if (!CURRENT_MODULE.pfAcceptNewNode(newNodeData->ipAddr, newNodeData->zoneUIN, macAddr))
         return false;  // filtered out by module
   }

   // Read configuration
   ConfigReadStr(_T("NetworkDiscovery.Filter"), szFilter, MAX_CONFIG_VALUE, _T(""));
   Trim(szFilter);

   // Initialize discovered node data
   DiscoveryFilterData data(newNodeData->ipAddr, newNodeData->zoneUIN);

   // Check for address range if we use simple filter instead of script
   uint32_t autoFilterFlags = 0;
   if (!_tcsicmp(szFilter, _T("auto")))
   {
      autoFilterFlags = ConfigReadULong(_T("NetworkDiscovery.FilterFlags"), DFF_ALLOW_AGENT | DFF_ALLOW_SNMP);
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): auto filter, flags=%04X"), szIpAddr, autoFilterFlags);

      if (autoFilterFlags & DFF_ONLY_RANGE)
      {
         nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): auto filter - checking range"), szIpAddr);
         ObjectArray<InetAddressListElement> *list = LoadServerAddressList(2);
         bool result = false;
         if (list != nullptr)
         {
            for(int i = 0; (i < list->size()) && !result; i++)
            {
               result = list->get(i)->contains(data.ipAddr);
            }
            delete list;
         }
         nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): auto filter - range check result is %d"), szIpAddr, result);
         if (!result)
            return false;
      }
   }

   // Check if host is reachable
   shared_ptr<AgentConnection> agentConnection;
   if (!HostIsReachable(newNodeData->ipAddr, newNodeData->zoneUIN, true, &pTransport, &agentConnection))
   {
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): host is not reachable"), szIpAddr);
      return false;
   }

   // Basic communication settings
   if (pTransport != nullptr)
   {
      data.flags |= NNF_IS_SNMP;
      data.snmpVersion = pTransport->getSnmpVersion();
      newNodeData->snmpSecurity = new SNMP_SecurityContext(pTransport->getSecurityContext());

      // Get SNMP OID
      SnmpGet(data.snmpVersion, pTransport,
              _T(".1.3.6.1.2.1.1.2.0"), nullptr, 0, data.snmpObjectId, MAX_OID_LEN * 4, 0);

      data.driver = FindDriverForNode(szIpAddr, data.snmpObjectId, nullptr, pTransport);
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): selected device driver %s"), szIpAddr, data.driver->getName());
   }
   if (agentConnection != nullptr)
   {
      data.flags |= NNF_IS_AGENT;
      agentConnection->getParameter(_T("Agent.Version"), data.agentVersion, MAX_AGENT_VERSION_LEN);
      agentConnection->getParameter(_T("System.PlatformName"), data.platform, MAX_PLATFORM_NAME_LEN);
   }

   // Read interface list if possible
   if (data.flags & NNF_IS_AGENT)
   {
      data.ifList = agentConnection->getInterfaceList();
   }
   if ((data.ifList == nullptr) && (data.flags & NNF_IS_SNMP))
   {
      data.driver->analyzeDevice(pTransport, data.snmpObjectId, &data, &data.driverData);
      data.ifList = data.driver->getInterfaces(pTransport, &data, data.driverData,
               ConfigReadInt(_T("Objects.Interfaces.UseAliases"), 0), ConfigReadBoolean(_T("Objects.Interfaces.UseIfXTable"), true));
   }

   // TODO: check all interfaces for matching existing nodes

   // Check for filter script
   if ((szFilter[0] == 0) || (!_tcsicmp(szFilter, _T("none"))))
   {
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): no filtering, node accepted"), szIpAddr);
      delete pTransport;
      return true;   // No filtering
   }

   // Check if node is a router
   if (data.flags & NNF_IS_SNMP)
   {
      if (SnmpGet(data.snmpVersion, pTransport,
                  _T(".1.3.6.1.2.1.4.1.0"), nullptr, 0, &dwTemp, sizeof(UINT32), 0) == SNMP_ERR_SUCCESS)
      {
         if (dwTemp == 1)
            data.flags |= NNF_IS_ROUTER;
      }
   }
   else if (data.flags & NNF_IS_AGENT)
   {
      // Check IP forwarding status
      if (agentConnection->getParameter(_T("Net.IP.Forwarding"), szBuffer, 16) == ERR_SUCCESS)
      {
         if (_tcstoul(szBuffer, nullptr, 10) != 0)
            data.flags |= NNF_IS_ROUTER;
      }
   }

   // Check various SNMP device capabilities
   if (data.flags & NNF_IS_SNMP)
   {
      // Check if node is a bridge
      if (SnmpGet(data.snmpVersion, pTransport,
                  _T(".1.3.6.1.2.1.17.1.1.0"), nullptr, 0, szBuffer, sizeof(szBuffer), 0) == SNMP_ERR_SUCCESS)
      {
         data.flags |= NNF_IS_BRIDGE;
      }

      // Check for CDP (Cisco Discovery Protocol) support
      if (SnmpGet(data.snmpVersion, pTransport,
                  _T(".1.3.6.1.4.1.9.9.23.1.3.1.0"), nullptr, 0, &dwTemp, sizeof(UINT32), 0) == SNMP_ERR_SUCCESS)
      {
         if (dwTemp == 1)
            data.flags |= NNF_IS_CDP;
      }

      // Check for SONMP (Nortel topology discovery protocol) support
      if (SnmpGet(data.snmpVersion, pTransport,
                  _T(".1.3.6.1.4.1.45.1.6.13.1.2.0"), nullptr, 0, &dwTemp, sizeof(UINT32), 0) == SNMP_ERR_SUCCESS)
      {
         if (dwTemp == 1)
            data.flags |= NNF_IS_SONMP;
      }

      // Check for LLDP (Link Layer Discovery Protocol) support
      if (SnmpGet(data.snmpVersion, pTransport,
                  _T(".1.0.8802.1.1.2.1.3.2.0"), nullptr, 0, szBuffer, sizeof(szBuffer), 0) == SNMP_ERR_SUCCESS)
      {
         data.flags |= NNF_IS_LLDP;
      }
   }

   bool result = false;

   // Check if we use simple filter instead of script
   if (!_tcsicmp(szFilter, _T("auto")))
   {
      if ((autoFilterFlags & (DFF_ALLOW_AGENT | DFF_ALLOW_SNMP)) == 0)
      {
         result = true;
      }
      else
      {
         if (autoFilterFlags & DFF_ALLOW_AGENT)
         {
            if (data.flags & NNF_IS_AGENT)
               result = true;
         }

         if (autoFilterFlags & DFF_ALLOW_SNMP)
         {
            if (data.flags & NNF_IS_SNMP)
               result = true;
         }
      }
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): auto filter - bResult=%d"), szIpAddr, result);
   }
   else
   {
      NXSL_VM *vm = CreateServerScriptVM(szFilter, shared_ptr<NetObj>());
      if (vm != nullptr)
      {
         nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): Running filter script %s"), szIpAddr, szFilter);

         if (pTransport != nullptr)
         {
            vm->setGlobalVariable("$snmp", vm->createValue(vm->createObject(&g_nxslSnmpTransportClass, pTransport)));
            pTransport = nullptr;   // Transport will be deleted by NXSL object destructor
         }
         // TODO: make agent connection available in script
         vm->setGlobalVariable("$node", vm->createValue(vm->createObject(&g_nxslDiscoveredNodeClass, &data)));

         NXSL_Value *param = vm->createValue(vm->createObject(&g_nxslDiscoveredNodeClass, &data));
         if (vm->run(1, &param))
         {
            result = vm->getResult()->getValueAsBoolean();
            nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): Filter script result: %d"), szIpAddr, result);
         }
         else
         {
            nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): Filter script execution error: %s"), szIpAddr, vm->getErrorText());
            ReportScriptError(SCRIPT_CONTEXT_OBJECT, nullptr, 0, vm->getErrorText(), szFilter);
         }
         delete vm;
      }
      else
      {
         nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("AcceptNewNode(%s): Cannot find filter script %s"), szIpAddr, szFilter);
      }
   }

   // Cleanup
   delete pTransport;

   return result;
}

/**
 * Create discovered node object
 */
static void CreateDiscoveredNode(NewNodeData *newNodeData)
{
   // Double check IP address because parallel discovery may already create that node
   if ((FindNodeByIP(newNodeData->zoneUIN, newNodeData->ipAddr) == nullptr) &&
       (FindSubnetByIP(newNodeData->zoneUIN, newNodeData->ipAddr) == nullptr))
   {
      PollNewNode(newNodeData);
   }
   else
   {
      TCHAR ipAddr[64];
      newNodeData->ipAddr.toString(ipAddr);
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("CreateDiscoveredNode(%s): node already exist in database"), ipAddr);
   }
   delete newNodeData;
}

/**
 * Process discovered address
 */
static void ProcessDiscoveredAddress(DiscoveredAddress *address)
{
   if (!IsShutdownInProgress())
   {
      NewNodeData *newNodeData = new NewNodeData(address->ipAddr);
      newNodeData->zoneUIN = address->zoneUIN;
      newNodeData->origin = NODE_ORIGIN_NETWORK_DISCOVERY;
      newNodeData->doConfPoll = true;

      if (address->ignoreFilter || AcceptNewNode(newNodeData, address->macAddr))
      {
         if (g_discoveryThreadPool != nullptr)
         {
            TCHAR key[32];
            _sntprintf(key, 32, _T("Zone%u"), address->zoneUIN);
            ThreadPoolExecuteSerialized(g_discoveryThreadPool, key, CreateDiscoveredNode, newNodeData);
         }
         else
         {
            CreateDiscoveredNode(newNodeData);
         }
      }
      else
      {
         delete newNodeData;
      }
   }
   s_processingListLock.lock();
   s_processingList.remove(address);
   s_processingListLock.unlock();
   delete address;
}

/**
 * Node poller thread (poll new nodes and put them into the database)
 */
void NodePoller()
{
   TCHAR szIpAddr[64];

   ThreadSetName("NodePoller");
   nxlog_debug(1, _T("Node poller started"));

   while(!IsShutdownInProgress())
   {
      DiscoveredAddress *address = g_nodePollerQueue.getOrBlock();
      if (address == INVALID_POINTER_VALUE)
         break;   // Shutdown indicator received

      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("NodePoller: processing address %s/%d in zone %d (source type %s, source node [%u])"),
               address->ipAddr.toString(szIpAddr), address->ipAddr.getMaskBits(), (int)address->zoneUIN,
               s_discoveredAddrSourceTypeAsText[address->sourceType], address->sourceNodeId);

      s_processingListLock.lock();
      s_processingList.add(address);
      s_processingListLock.unlock();

      if (g_discoveryThreadPool != nullptr)
      {
         if (g_flags & AF_PARALLEL_NETWORK_DISCOVERY)
         {
            ThreadPoolExecute(g_discoveryThreadPool, ProcessDiscoveredAddress, address);
         }
         else
         {
            TCHAR key[32];
            _sntprintf(key, 32, _T("Zone%u"), address->zoneUIN);
            ThreadPoolExecuteSerialized(g_discoveryThreadPool, key, ProcessDiscoveredAddress, address);
         }
      }
      else
      {
         ProcessDiscoveredAddress(address);
      }
   }
   nxlog_debug(1, _T("Node poller thread terminated"));
}

/**
 * Comparator for poller queue elements
 */
static bool PollerQueueElementComparator(const InetAddress *key, const DiscoveredAddress *element)
{
   return key->equals(element->ipAddr);
}

/**
 * Check potential new node from sysog, SNMP trap, or address range scan
 */
void CheckPotentialNode(const InetAddress& ipAddr, int32_t zoneUIN, DiscoveredAddressSourceType sourceType, uint32_t sourceNodeId)
{
   TCHAR buffer[64];
   nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Checking address %s in zone %d (source: %s)"),
            ipAddr.toString(buffer), zoneUIN, s_discoveredAddrSourceTypeAsText[sourceType]);
   if (!ipAddr.isValid() || ipAddr.isBroadcast() || ipAddr.isLoopback() || ipAddr.isMulticast())
   {
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Potential node %s rejected (IP address is not a valid unicast address)"), ipAddr.toString(buffer));
      return;
   }

   shared_ptr<Node> curr = FindNodeByIP(zoneUIN, ipAddr);
   if (curr != nullptr)
   {
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Potential node %s rejected (IP address already known at node %s [%d])"),
               ipAddr.toString(buffer), curr->getName(), curr->getId());
      return;
   }

   if (IsClusterIP(zoneUIN, ipAddr))
   {
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Potential node %s rejected (IP address is known as cluster resource address)"), ipAddr.toString(buffer));
      return;
   }

   if (IsNodePollerActiveAddress(ipAddr) || (g_nodePollerQueue.find(&ipAddr, PollerQueueElementComparator) != nullptr))
   {
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Potential node %s rejected (IP address already queued for polling)"), ipAddr.toString(buffer));
      return;
   }

   shared_ptr<Subnet> subnet = FindSubnetForNode(zoneUIN, ipAddr);
   if (subnet != nullptr)
   {
      if (!subnet->getIpAddress().equals(ipAddr) && !ipAddr.isSubnetBroadcast(subnet->getIpAddress().getMaskBits()))
      {
         DiscoveredAddress *addressInfo = new DiscoveredAddress(ipAddr, zoneUIN, sourceNodeId, sourceType);
         addressInfo->ipAddr.setMaskBits(subnet->getIpAddress().getMaskBits());
         nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 5, _T("New node queued: %s/%d"), addressInfo->ipAddr.toString(buffer), addressInfo->ipAddr.getMaskBits());
         g_nodePollerQueue.put(addressInfo);
      }
      else
      {
         nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Potential node %s rejected (IP address is a base or broadcast address of existing subnet)"), ipAddr.toString(buffer));
      }
   }
   else
   {
      DiscoveredAddress *addressInfo = new DiscoveredAddress(ipAddr, zoneUIN, sourceNodeId, sourceType);
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 5, _T("New node queued: %s/%d"), addressInfo->ipAddr.toString(buffer), addressInfo->ipAddr.getMaskBits());
      g_nodePollerQueue.put(addressInfo);
   }
}

/**
 * Check potential new node from ARP cache or routing table
 */
static void CheckPotentialNode(Node *node, const InetAddress& ipAddr, uint32_t ifIndex, const MacAddress& macAddr, DiscoveredAddressSourceType sourceType, uint32_t sourceNodeId)
{
   TCHAR buffer[64];
   nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Checking potential node %s at %s:%u (source: %s)"),
            ipAddr.toString(buffer), node->getName(), ifIndex, s_discoveredAddrSourceTypeAsText[sourceType]);
   if (!ipAddr.isValidUnicast())
   {
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Potential node %s rejected (IP address is not a valid unicast address)"), ipAddr.toString(buffer));
      return;
   }

   shared_ptr<Node> curr = FindNodeByIP(node->getZoneUIN(), ipAddr);
   if (curr != nullptr)
   {
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Potential node %s rejected (IP address already known at node %s [%u])"), ipAddr.toString(buffer), curr->getName(), curr->getId());

      // Check for duplicate IP address
      shared_ptr<Interface> iface = curr->findInterfaceByIP(ipAddr);
      if ((iface != nullptr) && macAddr.isValid() && !iface->getMacAddr().equals(macAddr))
      {
         MacAddress knownMAC = iface->getMacAddr();
         PostSystemEvent(EVENT_DUPLICATE_IP_ADDRESS, g_dwMgmtNode, "AdssHHdss",
                  &ipAddr, curr->getId(), curr->getName(), iface->getName(),
                  &knownMAC, &macAddr, node->getId(), node->getName(),
                  s_discoveredAddrSourceTypeAsText[sourceType]);
      }
      return;
   }

   if (IsClusterIP(node->getZoneUIN(), ipAddr))
   {
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Potential node %s rejected (IP address is known as cluster resource address)"), ipAddr.toString(buffer));
      return;
   }

   if (IsNodePollerActiveAddress(ipAddr) || (g_nodePollerQueue.find(&ipAddr, PollerQueueElementComparator) != nullptr))
   {
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Potential node %s rejected (IP address already queued for polling)"), ipAddr.toString(buffer));
      return;
   }

   shared_ptr<Interface> iface = node->findInterfaceByIndex(ifIndex);
   if ((iface != nullptr) && !iface->isExcludedFromTopology())
   {
      // Check if given IP address is not configured on source interface itself
      // Some Juniper devices can report addresses from internal interfaces in ARP cache
      if (!iface->getIpAddressList()->hasAddress(ipAddr))
      {
         const InetAddress& interfaceAddress = iface->getIpAddressList()->findSameSubnetAddress(ipAddr);
         if (interfaceAddress.isValidUnicast())
         {
            nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Interface found: %s [%u] addr=%s/%d ifIndex=%u"),
                  iface->getName(), iface->getId(), interfaceAddress.toString(buffer), interfaceAddress.getMaskBits(), iface->getIfIndex());
            if (!ipAddr.isSubnetBroadcast(interfaceAddress.getMaskBits()))
            {
               DiscoveredAddress *addressInfo = new DiscoveredAddress(ipAddr, node->getZoneUIN(), sourceNodeId, sourceType);
               addressInfo->ipAddr.setMaskBits(interfaceAddress.getMaskBits());
               addressInfo->macAddr = macAddr;
               nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 5, _T("New node queued: %s/%d"), addressInfo->ipAddr.toString(buffer), addressInfo->ipAddr.getMaskBits());
               g_nodePollerQueue.put(addressInfo);
            }
            else
            {
               nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Potential node %s rejected - broadcast/multicast address"), ipAddr.toString(buffer));
            }
         }
         else
         {
            nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Interface object found but IP address not found"));
         }
      }
      else
      {
         nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("IP address %s found on local interface %s [%u]"), ipAddr.toString(buffer), iface->getName(), iface->getId());
      }
   }
   else
   {
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Interface with index %u not found or marked as excluded from network topology"), ifIndex);
   }
}

/**
 * Check host route
 * Host will be added if it is directly connected
 */
static void CheckHostRoute(Node *node, const ROUTE *route)
{
   TCHAR buffer[16];
   nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Checking host route %s at %d"), IpToStr(route->dwDestAddr, buffer), route->dwIfIndex);
   shared_ptr<Interface> iface = node->findInterfaceByIndex(route->dwIfIndex);
   if ((iface != nullptr) && iface->getIpAddressList()->findSameSubnetAddress(route->dwDestAddr).isValidUnicast())
   {
      CheckPotentialNode(node, route->dwDestAddr, route->dwIfIndex, MacAddress::NONE, DA_SRC_ROUTING_TABLE, node->getId());
   }
   else
   {
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Interface object not found for host route"));
   }
}

/**
 * Discovery poller
 */
void DiscoveryPoller(PollerInfo *poller)
{
   poller->startExecution();
   int64_t startTime = GetCurrentTimeMs();

   Node *node = static_cast<Node*>(poller->getObject());
   if (node->isDeleteInitiated() || IsShutdownInProgress())
   {
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Discovery poll of node %s (%s) in zone %d aborted"),
                node->getName(), node->getIpAddress().toString().cstr(), node->getZoneUIN());
      node->completeDiscoveryPoll(GetCurrentTimeMs() - startTime);
      delete poller;
      return;
   }

   nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("Starting discovery poll of node %s (%s) in zone %d"),
             node->getName(), node->getIpAddress().toString().cstr(), node->getZoneUIN());

   // Retrieve and analyze node's ARP cache
   shared_ptr<ArpCache> arpCache = node->getArpCache(true);
   if (arpCache != nullptr)
   {
      for(int i = 0; i < arpCache->size(); i++)
      {
         const ArpEntry *e = arpCache->get(i);
         if (!e->macAddr.isBroadcast())   // Ignore broadcast addresses
            CheckPotentialNode(node, e->ipAddr, e->ifIndex, e->macAddr, DA_SRC_ARP_CACHE, node->getId());
      }
   }

   if (node->isDeleteInitiated() || IsShutdownInProgress())
   {
      nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 6, _T("Discovery poll of node %s (%s) in zone %d aborted"),
                node->getName(), (const TCHAR *)node->getIpAddress().toString(), (int)node->getZoneUIN());
      node->completeDiscoveryPoll(GetCurrentTimeMs() - startTime);
      delete poller;
      return;
   }

   // Retrieve and analyze node's routing table
   nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 5, _T("Discovery poll of node %s (%s) - reading routing table"), node->getName(), node->getIpAddress().toString().cstr());
   RoutingTable *rt = node->getRoutingTable();
   if (rt != nullptr)
   {
      for(int i = 0; i < rt->size(); i++)
      {
         ROUTE *route = rt->get(i);
         CheckPotentialNode(node, route->dwNextHop, route->dwIfIndex, MacAddress::NONE, DA_SRC_ROUTING_TABLE, node->getId());
         if ((route->dwDestMask == 0xFFFFFFFF) && (route->dwDestAddr != 0))
            CheckHostRoute(node, route);
      }
      delete rt;
   }

   node->executeHookScript(_T("DiscoveryPoll"));

   nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("Finished discovery poll of node %s (%s)"),
             node->getName(), (const TCHAR *)node->getIpAddress().toString());
   node->completeDiscoveryPoll(GetCurrentTimeMs() - startTime);
   delete poller;
}

/**
 * Callback for address range scan
 */
void RangeScanCallback(const InetAddress& addr, int32_t zoneUIN, const Node *proxy, uint32_t rtt, ServerConsole *console, void *context)
{
   TCHAR ipAddrText[64];
   if (proxy != nullptr)
   {
      ConsoleDebugPrintf(console, DEBUG_TAG_DISCOVERY, 5, _T("Active discovery - node %s responded to ICMP ping via proxy %s [%u]"),
            addr.toString(ipAddrText), proxy->getName(), proxy->getId());
      CheckPotentialNode(addr, zoneUIN, DA_SRC_ACTIVE_DISCOVERY, proxy->getId());
   }
   else
   {
      ConsoleDebugPrintf(console, DEBUG_TAG_DISCOVERY, 5, _T("Active discovery - node %s responded to ICMP ping"), addr.toString(ipAddrText));
      CheckPotentialNode(addr, zoneUIN, DA_SRC_ACTIVE_DISCOVERY, 0);
   }
}

/**
 * Check given address range with ICMP ping for new nodes
 */
void CheckRange(const InetAddressListElement& range, void (*callback)(const InetAddress&, int32_t, const Node *, uint32_t, ServerConsole *, void *), ServerConsole *console, void *context)
{
   if (range.getBaseAddress().getFamily() != AF_INET)
   {
      ConsoleDebugPrintf(console, DEBUG_TAG_DISCOVERY, 4, _T("Active discovery on range %s skipped - only IPv4 ranges supported"), (const TCHAR *)range.toString());
      return;
   }

   uint32_t from = range.getBaseAddress().getAddressV4();
   uint32_t to;
   if (range.getType() == InetAddressListElement_SUBNET)
   {
      from++;
      to = range.getBaseAddress().getSubnetBroadcast().getAddressV4() - 1;
   }
   else
   {
      to = range.getEndAddress().getAddressV4();
   }

   if (from > to)
   {
      ConsoleDebugPrintf(console, DEBUG_TAG_DISCOVERY, 4, _T("Invalid address range %s"), range.toString().cstr());
      return;
   }

   uint32_t blockSize = ConfigReadULong(_T("NetworkDiscovery.ActiveDiscovery.BlockSize"), 1024);
   uint32_t interBlockDelay = ConfigReadULong(_T("NetworkDiscovery.ActiveDiscovery.InterBlockDelay"), 0);

   if ((range.getZoneUIN() != 0) || (range.getProxyId() != 0))
   {
      uint32_t proxyId;
      if (range.getProxyId() != 0)
      {
         proxyId = range.getProxyId();
      }
      else
      {
         shared_ptr<Zone> zone = FindZoneByUIN(range.getZoneUIN());
         if (zone == nullptr)
         {
            ConsoleDebugPrintf(console, DEBUG_TAG_DISCOVERY, 4, _T("Invalid zone UIN for address range %s"), range.toString().cstr());
            return;
         }
         proxyId = zone->getProxyNodeId(nullptr);
      }

      shared_ptr<Node> proxy = static_pointer_cast<Node>(FindObjectById(proxyId, OBJECT_NODE));
      if (proxy == nullptr)
      {
         ConsoleDebugPrintf(console, DEBUG_TAG_DISCOVERY, 4, _T("Cannot find zone proxy node for address range %s"), range.toString().cstr());
         return;
      }

      shared_ptr<AgentConnectionEx> conn = proxy->createAgentConnection();
      if (conn == nullptr)
      {
         ConsoleDebugPrintf(console, DEBUG_TAG_DISCOVERY, 4, _T("Cannot connect to proxy agent for address range %s"), (const TCHAR *)range.toString());
         return;
      }
      conn->setCommandTimeout(10000);

      TCHAR ipAddr1[64], ipAddr2[64], rangeText[128];
      _sntprintf(rangeText, 128, _T("%s - %s"), IpToStr(from, ipAddr1), IpToStr(to, ipAddr2));
      ConsoleDebugPrintf(console, DEBUG_TAG_DISCOVERY, 4, _T("Starting active discovery check on range %s via proxy %s [%u]"),
               rangeText, proxy->getName(), proxy->getId());
      while((from <= to) && !IsShutdownInProgress())
      {
         if (interBlockDelay > 0)
            ThreadSleepMs(interBlockDelay);

         TCHAR request[256];
         _sntprintf(request, 256, _T("ICMP.ScanRange(%s,%s)"), IpToStr(from, ipAddr1), IpToStr(std::min(to, from + blockSize - 1), ipAddr2));
         StringList *list;
         if (conn->getList(request, &list) == ERR_SUCCESS)
         {
            for(int i = 0; i < list->size(); i++)
            {
               ConsoleDebugPrintf(console, DEBUG_TAG_DISCOVERY, 5, _T("Active discovery - node %s responded to ICMP ping via proxy %s [%u]"),
                        list->get(i), proxy->getName(), proxy->getId());
               callback(InetAddress::parse(list->get(i)), range.getZoneUIN(), proxy.get(), 0, console, nullptr);
            }
            delete list;
         }
         from += blockSize;
      }
      ConsoleDebugPrintf(console, DEBUG_TAG_DISCOVERY, 4, _T("Finished active discovery check on range %s via proxy %s [%u]"),
               rangeText, proxy->getName(), proxy->getId());
   }
   else
   {
      TCHAR ipAddr1[16], ipAddr2[16];
      ConsoleDebugPrintf(console, DEBUG_TAG_DISCOVERY, 4, _T("Starting active discovery check on range %s - %s"), IpToStr(from, ipAddr1), IpToStr(to, ipAddr2));
      while((from <= to) && !IsShutdownInProgress())
      {
         if (interBlockDelay > 0)
            ThreadSleepMs(interBlockDelay);
         ScanAddressRange(from, std::min(to, from + blockSize - 1), callback, console, nullptr);
         from += blockSize;
      }
      ConsoleDebugPrintf(console, DEBUG_TAG_DISCOVERY, 4, _T("Finished active discovery check on range %s - %s"), ipAddr1, ipAddr2);
   }
}

/**
 * Active discovery thread wakeup condition
 */
static Condition s_activeDiscoveryWakeup(false);

/**
 * Active discovery poller thread
 */
void ActiveDiscoveryPoller()
{
   ThreadSetName("ActiveDiscovery");

   nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 2, _T("Active discovery thread started"));

   time_t lastRun = 0;
   uint32_t sleepTime = 60000;

   // Main loop
   while(!IsShutdownInProgress())
   {
      s_activeDiscoveryWakeup.wait(sleepTime);
      if (IsShutdownInProgress())
         break;

      if (!(g_flags & AF_ACTIVE_NETWORK_DISCOVERY))
      {
         sleepTime = INFINITE;
         continue;
      }

      time_t now = time(nullptr);

      uint32_t interval = ConfigReadULong(_T("NetworkDiscovery.ActiveDiscovery.Interval"), 7200);
      if (interval != 0)
      {
         if (static_cast<uint32_t>(now - lastRun) < interval)
         {
            sleepTime = (interval - static_cast<uint32_t>(lastRun - now)) * 1000;
            continue;
         }
      }
      else
      {
         TCHAR schedule[256];
         ConfigReadStr(_T("NetworkDiscovery.ActiveDiscovery.Schedule"), schedule, 256, _T(""));
         if (schedule[0] != 0)
         {
#if HAVE_LOCALTIME_R
            struct tm tmbuffer;
            localtime_r(&now, &tmbuffer);
            struct tm *ltm = &tmbuffer;
#else
            struct tm *ltm = localtime(&now);
#endif
            if (!MatchSchedule(schedule, nullptr, ltm, now))
            {
               sleepTime = 60000;
               continue;
            }
         }
         else
         {
            nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 4, _T("Empty active discovery schedule"));
            sleepTime = INFINITE;
            continue;
         }
      }

      ObjectArray<InetAddressListElement> *addressList = LoadServerAddressList(1);
      if (addressList != nullptr)
      {
         for(int i = 0; (i < addressList->size()) && !IsShutdownInProgress(); i++)
         {
            CheckRange(*addressList->get(i), RangeScanCallback, nullptr, nullptr);
         }
         delete addressList;
      }

      interval = ConfigReadInt(_T("NetworkDiscovery.ActiveDiscovery.Interval"), 7200);
      sleepTime = (interval > 0) ? interval * 1000 : 60000;
   }

   nxlog_debug_tag(DEBUG_TAG_DISCOVERY, 2, _T("Active discovery thread terminated"));
}

/**
 * Clear discovery poller queue
 */
static void ClearDiscoveryPollerQueue()
{
   DiscoveredAddress *addressInfo;
   while((addressInfo = g_nodePollerQueue.get()) != nullptr)
   {
      if (addressInfo != INVALID_POINTER_VALUE)
         delete addressInfo;
   }
}

/**
 * Reset discovery poller after configuration change
 */
void ResetDiscoveryPoller()
{
   ClearDiscoveryPollerQueue();

   // Reload discovery parameters
   g_discoveryPollingInterval = ConfigReadInt(_T("NetworkDiscovery.PassiveDiscovery.Interval"), 900);

   switch(ConfigReadInt(_T("NetworkDiscovery.Type"), 0))
   {
      case 0:  // Disabled
         g_flags &= ~(AF_PASSIVE_NETWORK_DISCOVERY | AF_ACTIVE_NETWORK_DISCOVERY);
         break;
      case 1:  // Passive only
         g_flags |= AF_PASSIVE_NETWORK_DISCOVERY;
         g_flags &= ~AF_ACTIVE_NETWORK_DISCOVERY;
         break;
      case 2:  // Active only
         g_flags &= ~AF_PASSIVE_NETWORK_DISCOVERY;
         g_flags |= AF_ACTIVE_NETWORK_DISCOVERY;
         break;
      case 3:  // Active and passive
         g_flags |= AF_PASSIVE_NETWORK_DISCOVERY | AF_ACTIVE_NETWORK_DISCOVERY;
         break;
      default:
         break;
   }

   if (ConfigReadBoolean(_T("NetworkDiscovery.UseSNMPTraps"), false))
      g_flags |= AF_SNMP_TRAP_DISCOVERY;
   else
      g_flags &= ~AF_SNMP_TRAP_DISCOVERY;

   if (ConfigReadBoolean(_T("NetworkDiscovery.UseSyslog"), false))
      g_flags |= AF_SYSLOG_DISCOVERY;
   else
      g_flags &= ~AF_SYSLOG_DISCOVERY;

   s_activeDiscoveryWakeup.set();
}

/**
 * Stop discovery poller
 */
void StopDiscoveryPoller()
{
   ClearDiscoveryPollerQueue();
   g_nodePollerQueue.put(INVALID_POINTER_VALUE);
}

/**
 * Wakeup active discovery thread
 */
void WakeupActiveDiscoveryThread()
{
   s_activeDiscoveryWakeup.set();
}

/**
 * Manual active discovery starter
 */
void StartManualActiveDiscovery(ObjectArray<InetAddressListElement> *addressList)
{
   for(int i = 0; (i < addressList->size()) && !IsShutdownInProgress(); i++)
   {
      CheckRange(*addressList->get(i), RangeScanCallback, nullptr, nullptr);
   }
   delete addressList;
}

/**
 * Get total size of discovery poller queue (all stages)
 */
int64_t GetDiscoveryPollerQueueSize()
{
   int poolQueueSize;
   if (g_discoveryThreadPool != nullptr)
   {
      ThreadPoolInfo info;
      ThreadPoolGetInfo(g_discoveryThreadPool, &info);
      poolQueueSize = ((info.activeRequests > info.curThreads) ? info.activeRequests - info.curThreads : 0) + info.serializedRequests;
   }
   else
   {
      poolQueueSize = 0;
   }
   return g_nodePollerQueue.size() + poolQueueSize;
}