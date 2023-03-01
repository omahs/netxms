/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2023 Victor Kirhenshtein
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
package org.netxms.nxmc.modules.objects.actions;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.action.Action;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.window.Window;
import org.netxms.client.NXCException;
import org.netxms.client.NXCSession;
import org.netxms.client.constants.DataOrigin;
import org.netxms.client.constants.DataType;
import org.netxms.client.constants.RCC;
import org.netxms.client.datacollection.DataCollectionConfiguration;
import org.netxms.client.datacollection.DataCollectionItem;
import org.netxms.client.objects.AbstractNode;
import org.netxms.client.objects.Interface;
import org.netxms.nxmc.Registry;
import org.netxms.nxmc.base.jobs.Job;
import org.netxms.nxmc.base.views.View;
import org.netxms.nxmc.localization.LocalizationHelper;
import org.netxms.nxmc.modules.objects.dialogs.CreateInterfaceDciDialog;
import org.netxms.nxmc.modules.objects.dialogs.helpers.InterfaceDciInfo;
import org.xnap.commons.i18n.I18n;

/**
 * Action for creating interface DCIs
 */
public class CreateInterfaceDciAction extends Action
{
   private static final int IFDCI_IN_BYTES = 0;
   private static final int IFDCI_OUT_BYTES = 1;
   private static final int IFDCI_IN_BITS = 2;
   private static final int IFDCI_OUT_BITS = 3;
   private static final int IFDCI_IN_PACKETS = 4;
   private static final int IFDCI_OUT_PACKETS = 5;
   private static final int IFDCI_IN_ERRORS = 6;
   private static final int IFDCI_OUT_ERRORS = 7;

   private final I18n i18n = LocalizationHelper.getI18n(CreateInterfaceDciAction.class);

   private View view;
   private ISelectionProvider selectionProvider;

   /**
    * Create action for creating interface DCIs.
    *
    * @param text action's text
    * @param view owning view
    * @param selectionProvider associated selection provider
    */
   public CreateInterfaceDciAction(String text, View view, ISelectionProvider selectionProvider)
   {
      super(text);
      this.view = view;
      this.selectionProvider = selectionProvider;
   }

   /**
    * @see org.eclipse.jface.action.Action#run()
    */
   @Override
   public void run()
   {
      final List<Interface> interfaces = new ArrayList<>();
      for(Object o : ((IStructuredSelection)selectionProvider.getSelection()).toList())
      {
         if (o instanceof Interface)
            interfaces.add((Interface)o);
      }
      if (interfaces.isEmpty())
         return;

      final CreateInterfaceDciDialog dlg = new CreateInterfaceDciDialog(view.getWindow().getShell(), (interfaces.size() == 1) ? interfaces.get(0) : null);
      if (dlg.open() != Window.OK)
         return;

      // Get set of nodes
      final Set<AbstractNode> nodes = new HashSet<AbstractNode>();
      for(Interface iface : interfaces)
      {
         AbstractNode node = iface.getParentNode();
         if (node != null)
         {
            nodes.add(node);
         }
      }

      final NXCSession session = Registry.getSession();
      final String taskName = i18n.tr("Creating interface DCIs");
      new Job(i18n.tr("Creating interface DCI"), view) {
         @Override
         protected void run(IProgressMonitor monitor) throws Exception
         {
            InterfaceDciInfo[] dciInfo = dlg.getDciInfo();
            monitor.beginTask(taskName, interfaces.size() * dciInfo.length);
            for(int i = 0; i < interfaces.size(); i++)
            {
               for(int j = 0; j < dciInfo.length; j++)
               {
                  if (dciInfo[j].enabled)
                  {
                     createInterfaceDci(session, interfaces.get(i), j, dciInfo[j], dlg.getPollingScheduleType(), dlg.getPollingInterval(), dlg.getRetentionType(), dlg.getRetentionTime(),
                           interfaces.size() > 1);
                  }
                  monitor.worked(1);
               }
            }
            monitor.done();
         }

         @Override
         protected String getErrorMessage()
         {
            return i18n.tr("Cannot create interface DCI");
         }
      }.start();
   }

   /**
    * Create interface DCI
    *
    * @param session client session
    * @param iface interface object
    * @param dciType DCI type
    * @param dciInfo DCI information
    * @param pollingScheduleType polling schedule type
    * @param pollingInterval
    * @param retentionType
    * @param retentionTime
    * @param updateDescription
    * @param lockRequired
    * @throws Exception
    */
   private static void createInterfaceDci(NXCSession session, Interface iface, int dciType, InterfaceDciInfo dciInfo, int pollingScheduleType, int pollingInterval, int retentionType,
         int retentionTime, boolean updateDescription) throws Exception
   {
      AbstractNode node = iface.getParentNode();
      if (node == null)
         throw new NXCException(RCC.INTERNAL_ERROR);

      DataCollectionConfiguration dcc = new DataCollectionConfiguration(session, node.getObjectId());

      final DataCollectionItem dci = new DataCollectionItem(dcc, 0);
      dci.setPollingScheduleType(pollingScheduleType);
      dci.setPollingInterval(Integer.toString(pollingInterval));
      dci.setRetentionType(retentionType);
      dci.setRetentionTime(Integer.toString(retentionTime));
      if (node.hasAgent())
      {
         dci.setOrigin(DataOrigin.AGENT);
         if (node.isAgentIfXCountersSupported())
            dci.setDataType(((dciType != IFDCI_IN_ERRORS) && (dciType != IFDCI_OUT_ERRORS)) ? DataType.COUNTER64 : DataType.COUNTER32);
         else
            dci.setDataType(DataType.COUNTER32);
      }
      else
      {
         dci.setOrigin(DataOrigin.SNMP);
         if (node.isIfXTableSupported())
            dci.setDataType(((dciType != IFDCI_IN_ERRORS) && (dciType != IFDCI_OUT_ERRORS)) ? DataType.COUNTER64 : DataType.COUNTER32);
         else
            dci.setDataType(DataType.COUNTER32);
      }
      dci.setStatus(DataCollectionItem.ACTIVE);
      dci.setDescription(updateDescription ? dciInfo.description.replaceAll("@@ifName@@", iface.getObjectName()) : dciInfo.description); //$NON-NLS-1$
      dci.setDeltaCalculation(dciInfo.delta ? DataCollectionItem.DELTA_AVERAGE_PER_SECOND : DataCollectionItem.DELTA_NONE);
      dci.setRelatedObject(iface.getObjectId());

      if (dci.getOrigin() == DataOrigin.AGENT)
      {
         switch(dciType)
         {
            case IFDCI_IN_BYTES:
            case IFDCI_IN_BITS:
               dci.setName((node.isAgentIfXCountersSupported() ? "Net.Interface.BytesIn64(" : "Net.Interface.BytesIn(") + iface.getIfIndex() + ")"); //$NON-NLS-1$ //$NON-NLS-2$ //$NON-NLS-3$
               break;
            case IFDCI_OUT_BYTES:
            case IFDCI_OUT_BITS:
               dci.setName((node.isAgentIfXCountersSupported() ? "Net.Interface.BytesOut64(" : "Net.Interface.BytesOut(") + iface.getIfIndex() + ")"); //$NON-NLS-1$ //$NON-NLS-2$ //$NON-NLS-3$
               break;
            case IFDCI_IN_PACKETS:
               dci.setName((node.isAgentIfXCountersSupported() ? "Net.Interface.PacketsIn64(" : "Net.Interface.PacketsIn(") + iface.getIfIndex() + ")"); //$NON-NLS-1$ //$NON-NLS-2$ //$NON-NLS-3$
               break;
            case IFDCI_OUT_PACKETS:
               dci.setName((node.isAgentIfXCountersSupported() ? "Net.Interface.PacketsOut64(" : "Net.Interface.PacketsOut(") + iface.getIfIndex() + ")"); //$NON-NLS-1$ //$NON-NLS-2$ //$NON-NLS-3$
               break;
            case IFDCI_IN_ERRORS:
               dci.setName("Net.Interface.InErrors(" + iface.getIfIndex() + ")"); //$NON-NLS-1$ //$NON-NLS-2$
               break;
            case IFDCI_OUT_ERRORS:
               dci.setName("Net.Interface.OutErrors(" + iface.getIfIndex() + ")"); //$NON-NLS-1$ //$NON-NLS-2$
               break;
         }
      }
      else
      {
         switch(dciType)
         {
            case IFDCI_IN_BYTES:
            case IFDCI_IN_BITS:
               dci.setName((node.isIfXTableSupported() ? ".1.3.6.1.2.1.31.1.1.1.6" : ".1.3.6.1.2.1.2.2.1.10") + getInterfaceInstance(iface)); //$NON-NLS-1$ //$NON-NLS-2$
               break;
            case IFDCI_OUT_BYTES:
            case IFDCI_OUT_BITS:
               dci.setName((node.isIfXTableSupported() ? ".1.3.6.1.2.1.31.1.1.1.10" : ".1.3.6.1.2.1.2.2.1.16") + getInterfaceInstance(iface)); //$NON-NLS-1$ //$NON-NLS-2$
               break;
            case IFDCI_IN_PACKETS:
               dci.setName((node.isIfXTableSupported() ? ".1.3.6.1.2.1.31.1.1.1.7" : ".1.3.6.1.2.1.2.2.1.11") + getInterfaceInstance(iface)); //$NON-NLS-1$ //$NON-NLS-2$
               break;
            case IFDCI_OUT_PACKETS:
               dci.setName((node.isIfXTableSupported() ? ".1.3.6.1.2.1.31.1.1.1.11" : ".1.3.6.1.2.1.2.2.1.17") + getInterfaceInstance(iface)); //$NON-NLS-1$ //$NON-NLS-2$
               break;
            case IFDCI_IN_ERRORS:
               dci.setName(".1.3.6.1.2.1.2.2.1.14" + getInterfaceInstance(iface)); //$NON-NLS-1$
               break;
            case IFDCI_OUT_ERRORS:
               dci.setName(".1.3.6.1.2.1.2.2.1.20" + getInterfaceInstance(iface)); //$NON-NLS-1$
               break;
         }
      }

      switch(dciType)
      {
         case IFDCI_OUT_BYTES:
         case IFDCI_IN_BYTES:
            dci.setUnitName(dciInfo.delta ? "B/s" : "B (Metric)"); //$NON-NLS-1$ //$NON-NLS-2$
            break;
         case IFDCI_OUT_BITS:
         case IFDCI_IN_BITS:
            dci.setUnitName(dciInfo.delta ? "b/s" : "b (Metric)"); //$NON-NLS-1$ //$NON-NLS-2$
            break;
         case IFDCI_OUT_PACKETS:
         case IFDCI_IN_PACKETS:
            dci.setUnitName(dciInfo.delta ? "packets/s" : "packets"); //$NON-NLS-1$ //$NON-NLS-2$
            break;
         case IFDCI_OUT_ERRORS:
         case IFDCI_IN_ERRORS:
            dci.setUnitName(dciInfo.delta ? "errors/s" : "errors"); //$NON-NLS-1$ //$NON-NLS-2$
            break;
      }

      if ((dciType == IFDCI_IN_BITS) || (dciType == IFDCI_OUT_BITS))
      {
         dci.setTransformationScript("return $1 * 8;"); //$NON-NLS-1$
      }

      dcc.modifyObject(dci);
   }

   /**
    * @param iface interface object
    * @return interface instance
    */
   private static String getInterfaceInstance(Interface iface)
   {
      return (iface.getIfTableSuffix().getLength() > 0) ? iface.getIfTableSuffix().toString() : "." + iface.getIfIndex();
   }
}