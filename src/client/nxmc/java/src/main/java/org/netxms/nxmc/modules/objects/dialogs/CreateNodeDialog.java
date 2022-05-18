/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2022 Victor Kirhenshtein
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
package org.netxms.nxmc.modules.objects.dialogs;

import org.eclipse.jface.dialogs.Dialog;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.RowLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Spinner;
import org.netxms.client.NXCObjectCreationData;
import org.netxms.client.NXCSession;
import org.netxms.client.objects.Node;
import org.netxms.client.objects.Zone;
import org.netxms.nxmc.Registry;
import org.netxms.nxmc.base.widgets.LabeledText;
import org.netxms.nxmc.localization.LocalizationHelper;
import org.netxms.nxmc.modules.objects.widgets.ObjectSelector;
import org.netxms.nxmc.modules.objects.widgets.ZoneSelector;
import org.netxms.nxmc.tools.MessageDialogHelper;
import org.netxms.nxmc.tools.WidgetHelper;
import org.xnap.commons.i18n.I18n;

/**
 * Dialog for creating new node
 */
public class CreateNodeDialog extends Dialog
{
   private I18n i18n = LocalizationHelper.getI18n(CreateNodeDialog.class);
	private NXCSession session;
	private LabeledText objectNameField;
	private LabeledText hostNameField;
	private Spinner agentPortField;
	private Spinner snmpPortField;
   private Spinner etherNetIpPortField;
   private Spinner sshPortField;
	private LabeledText sshLoginField;
   private LabeledText sshPasswordField;
	private Button checkUnmanaged;
   private Button checkMaintenanceMode;
   private Button checkAsZoneProxy;
	private Button checkDisableAgent;
	private Button checkDisableSNMP;
   private Button checkDisableSSH;
   private Button checkDisableEtherNetIP;
	private Button checkDisablePing;
	private Button checkCreateAnother;
	private Button checkDisableAutomaticSNMPConfig;
   private Button checkRemoteManagementNode;
	private ObjectSelector agentProxySelector;
	private ObjectSelector snmpProxySelector;
   private ObjectSelector etherNetIpProxySelector;
   private ObjectSelector icmpProxySelector;
   private ObjectSelector sshProxySelector;
	private ObjectSelector webServiceProxySelector;
	private ZoneSelector zoneSelector;

	private String objectName = "";
	private String hostName = "";
	private int creationFlags = 0;
	private long agentProxy = 0;
	private long snmpProxy = 0;
   private long etherNetIpProxy = 0;
   private long icmpProxy = 0;
   private long sshProxy = 0;
	private long webServiceProxy = 0;
	private int zoneUIN = 0;
	private int agentPort = 4700;
	private int snmpPort = 161;
   private int etherNetIpPort = 44818;
   private int sshPort = 22;
	private String sshLogin = "";
	private String sshPassword; 
	private boolean showAgain = false;
	private boolean enableShowAgainFlag = true;

	/**
	 * @param parentShell
	 */
	public CreateNodeDialog(Shell parentShell, CreateNodeDialog prev)
	{
		super(parentShell);
      session = Registry.getSession();
		if (prev != null)
		{
         creationFlags = prev.creationFlags;
         agentProxy = prev.agentProxy;
         snmpProxy = prev.snmpProxy;
         icmpProxy = prev.snmpProxy;
         sshProxy = prev.snmpProxy;
         webServiceProxy = prev.webServiceProxy;
		   zoneUIN = prev.zoneUIN;
		   agentPort = prev.agentPort;
		   snmpPort = prev.snmpPort;
		   sshLogin = prev.sshLogin;
		   sshPassword = prev.sshPassword;
		   sshPort = prev.sshPort;
         showAgain = prev.showAgain;
		}
	}

   /**
    * @see org.eclipse.jface.window.Window#configureShell(org.eclipse.swt.widgets.Shell)
    */
	@Override
	protected void configureShell(Shell newShell)
	{
		super.configureShell(newShell);
		newShell.setText(i18n.tr("Create Node Object"));
	}

   /**
    * @see org.eclipse.jface.dialogs.Dialog#createDialogArea(org.eclipse.swt.widgets.Composite)
    */
	@Override
	protected Control createDialogArea(Composite parent)
	{
		Composite dialogArea = (Composite)super.createDialogArea(parent);
		
		GridLayout layout = new GridLayout();
		layout.verticalSpacing = WidgetHelper.DIALOG_SPACING;
		layout.horizontalSpacing = WidgetHelper.DIALOG_SPACING;
		layout.marginHeight = WidgetHelper.DIALOG_HEIGHT_MARGIN;
		layout.marginWidth = WidgetHelper.DIALOG_WIDTH_MARGIN;
		layout.numColumns = 2;
		layout.makeColumnsEqualWidth = true;
		dialogArea.setLayout(layout);
		
		objectNameField = new LabeledText(dialogArea, SWT.NONE);
		objectNameField.setLabel(i18n.tr("Object Name"));
		objectNameField.getTextControl().setTextLimit(255);
		GridData gd = new GridData();
		gd.horizontalAlignment = SWT.FILL;
		gd.grabExcessHorizontalSpace = true;
		gd.widthHint = 600;
		gd.horizontalSpan = 2;
		objectNameField.setLayoutData(gd);
		objectNameField.setText(objectName);
		
		final Composite ipAddrGroup = new Composite(dialogArea, SWT.NONE);
		layout = new GridLayout();
		layout.marginHeight = 0;
		layout.marginWidth = 0;
		layout.numColumns = 2;
		ipAddrGroup.setLayout(layout);
		gd = new GridData();
		gd.horizontalAlignment = SWT.FILL;
		gd.grabExcessHorizontalSpace = true;
		gd.horizontalSpan = 2;
		ipAddrGroup.setLayoutData(gd);
		
		hostNameField = new LabeledText(ipAddrGroup, SWT.NONE);
		hostNameField.setLabel(i18n.tr("Primary host name or IP address"));
		hostNameField.getTextControl().setTextLimit(255);
		gd = new GridData();
		gd.horizontalAlignment = SWT.FILL;
		gd.grabExcessHorizontalSpace = true;
		hostNameField.setLayoutData(gd);
		hostNameField.setText(hostName);
		
		agentPortField = WidgetHelper.createLabeledSpinner(dialogArea, SWT.BORDER, i18n.tr("NetXMS agent port"), 1, 65535, WidgetHelper.DEFAULT_LAYOUT_DATA);
		agentPortField.setSelection(agentPort);
		
		snmpPortField = WidgetHelper.createLabeledSpinner(dialogArea, SWT.BORDER, i18n.tr("SNMP agent port"), 1, 65535, WidgetHelper.DEFAULT_LAYOUT_DATA);
		snmpPortField.setSelection(snmpPort);
		
      etherNetIpPortField = WidgetHelper.createLabeledSpinner(dialogArea, SWT.BORDER, i18n.tr("EtherNet/IP port"), 1, 65535, WidgetHelper.DEFAULT_LAYOUT_DATA);
      etherNetIpPortField.setSelection(etherNetIpPort);

      sshPortField = WidgetHelper.createLabeledSpinner(dialogArea, SWT.BORDER, i18n.tr("SSH port"), 1, 65535, WidgetHelper.DEFAULT_LAYOUT_DATA);
      sshPortField.setSelection(sshPort);

		sshLoginField = new LabeledText(dialogArea, SWT.NONE);
		sshLoginField.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true, false));
      sshLoginField.setLabel(i18n.tr("SSH login"));
		sshLoginField.setText(sshLogin);
		
      sshPasswordField = new LabeledText(dialogArea, SWT.NONE);
      sshPasswordField.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true, false));
      sshPasswordField.setLabel(i18n.tr("SSH password"));
      sshPasswordField.setText(sshPassword);
      
		Group optionsGroup = new Group(dialogArea, SWT.NONE);
		optionsGroup.setText(i18n.tr("Options"));
		gd = new GridData();
		gd.horizontalAlignment = SWT.FILL;
		gd.grabExcessHorizontalSpace = true;
		gd.horizontalSpan = 2;
		optionsGroup.setLayoutData(gd);
		optionsGroup.setLayout(new RowLayout(SWT.VERTICAL));
		
		checkRemoteManagementNode = new Button(optionsGroup, SWT.CHECK);
      checkRemoteManagementNode.setText(i18n.tr("Communication through external &gateway"));
      checkRemoteManagementNode.setSelection((creationFlags & NXCObjectCreationData.CF_EXTERNAL_GATEWAY) != 0);
		
		checkUnmanaged = new Button(optionsGroup, SWT.CHECK);
		checkUnmanaged.setText(i18n.tr("Create as &unmanaged object"));
		checkUnmanaged.setSelection((creationFlags & NXCObjectCreationData.CF_CREATE_UNMANAGED) != 0);

      checkMaintenanceMode = new Button(optionsGroup, SWT.CHECK);
      checkMaintenanceMode.setText(i18n.tr("Enter &maintenance mode immediately"));
      checkMaintenanceMode.setSelection((creationFlags & NXCObjectCreationData.CF_ENTER_MAINTENANCE) != 0);

      if (session.isZoningEnabled())
      {
         checkAsZoneProxy = new Button(optionsGroup, SWT.CHECK);
         checkAsZoneProxy.setText(i18n.tr("Create as zone proxy for selected zone"));
         checkAsZoneProxy.setSelection((creationFlags & NXCObjectCreationData.CF_AS_ZONE_PROXY) != 0);
         checkAsZoneProxy.addSelectionListener(new SelectionListener() {
            @Override
            public void widgetSelected(SelectionEvent e)
            {
               if (checkAsZoneProxy.getSelection())
               {
                  checkDisableAgent.setSelection(false);
                  checkDisableAgent.setEnabled(false);
               }
               else
               {
                  checkDisableAgent.setEnabled(true);
               }
            }
            
            @Override
            public void widgetDefaultSelected(SelectionEvent e)
            {
               widgetSelected(e);
            }
         });
      }

		checkDisableAgent = new Button(optionsGroup, SWT.CHECK);
		checkDisableAgent.setText(i18n.tr("Disable usage of NetXMS &agent for all polls"));
		checkDisableAgent.setSelection((creationFlags & NXCObjectCreationData.CF_DISABLE_NXCP) != 0);
		
		checkDisableSNMP = new Button(optionsGroup, SWT.CHECK);
		checkDisableSNMP.setText(i18n.tr("Disable usage of &SNMP for all polls"));
		checkDisableSNMP.setSelection((creationFlags & NXCObjectCreationData.CF_DISABLE_SNMP) != 0);

      checkDisableSSH = new Button(optionsGroup, SWT.CHECK);
      checkDisableSSH.setText("Disable usage of SSH for all polls");
      checkDisableSSH.setSelection((creationFlags & NXCObjectCreationData.CF_DISABLE_SSH) != 0);
		
      checkDisablePing = new Button(optionsGroup, SWT.CHECK);
      checkDisablePing.setText(i18n.tr("Disable usage of &ICMP ping for all polls"));
      checkDisablePing.setSelection((creationFlags & NXCObjectCreationData.CF_DISABLE_ICMP) != 0);

      checkDisableEtherNetIP = new Button(optionsGroup, SWT.CHECK);
      checkDisableEtherNetIP.setText("Disable usage of &EtherNet/IP for all polls");
      checkDisableEtherNetIP.setSelection((creationFlags & NXCObjectCreationData.CF_DISABLE_ETHERNET_IP) != 0);

      checkDisableAutomaticSNMPConfig = new Button(optionsGroup, SWT.CHECK);
      checkDisableAutomaticSNMPConfig.setText(i18n.tr("&Prevent automatic SNMP configuration changes"));
      checkDisableAutomaticSNMPConfig.setSelection((creationFlags & NXCObjectCreationData.CF_SNMP_SETTINGS_LOCKED) != 0);

		agentProxySelector = new ObjectSelector(dialogArea, SWT.NONE, true);
		agentProxySelector.setLabel(i18n.tr("Proxy for NetXMS agents"));
		agentProxySelector.setObjectClass(Node.class);
		agentProxySelector.setObjectId(agentProxy);
		gd = new GridData();
		gd.horizontalAlignment = SWT.FILL;
		gd.grabExcessHorizontalSpace = true;
		agentProxySelector.setLayoutData(gd);

		snmpProxySelector = new ObjectSelector(dialogArea, SWT.NONE, true);
		snmpProxySelector.setLabel(i18n.tr("Proxy for SNMP"));
		snmpProxySelector.setObjectClass(Node.class);
		snmpProxySelector.setObjectId(snmpProxy);
		gd = new GridData();
		gd.horizontalAlignment = SWT.FILL;
		gd.grabExcessHorizontalSpace = true;
		snmpProxySelector.setLayoutData(gd);
		
      etherNetIpProxySelector = new ObjectSelector(dialogArea, SWT.NONE, true);
      etherNetIpProxySelector.setLabel(i18n.tr("Proxy for EtherNet/IP"));
      etherNetIpProxySelector.setObjectClass(Node.class);
      etherNetIpProxySelector.setObjectId(etherNetIpProxy);
      gd = new GridData();
      gd.horizontalAlignment = SWT.FILL;
      gd.grabExcessHorizontalSpace = true;
      etherNetIpProxySelector.setLayoutData(gd);

      icmpProxySelector = new ObjectSelector(dialogArea, SWT.NONE, true);
      icmpProxySelector.setLabel(i18n.tr("Proxy for ICMP"));
      icmpProxySelector.setObjectClass(Node.class);
      icmpProxySelector.setObjectId(icmpProxy);
      gd = new GridData();
      gd.horizontalAlignment = SWT.FILL;
      gd.grabExcessHorizontalSpace = true;
      icmpProxySelector.setLayoutData(gd);

      sshProxySelector = new ObjectSelector(dialogArea, SWT.NONE, true);
      sshProxySelector.setLabel(i18n.tr("Proxy for SSH"));
      sshProxySelector.setEmptySelectionName("<default>");
      sshProxySelector.setObjectClass(Node.class);
      sshProxySelector.setObjectId(sshProxy);
      gd = new GridData();
      gd.horizontalAlignment = SWT.FILL;
      gd.grabExcessHorizontalSpace = true;
      sshProxySelector.setLayoutData(gd);

      webServiceProxySelector = new ObjectSelector(dialogArea, SWT.NONE, true);
      webServiceProxySelector.setLabel(i18n.tr("Proxy for web services"));
      webServiceProxySelector.setEmptySelectionName("<default>");
      webServiceProxySelector.setObjectClass(Node.class);
      webServiceProxySelector.setObjectId(webServiceProxy);
      gd = new GridData();
      gd.horizontalAlignment = SWT.FILL;
      gd.grabExcessHorizontalSpace = true;
      webServiceProxySelector.setLayoutData(gd);

		if (session.isZoningEnabled())
		{
			zoneSelector = new ZoneSelector(dialogArea, SWT.NONE, false);
			zoneSelector.setLabel(i18n.tr("Zone"));
         Zone zone = session.findZone(zoneUIN);
			zoneSelector.setZoneUIN((zone != null) ? zone.getUIN() : -1);
			gd = new GridData();
			gd.horizontalAlignment = SWT.FILL;
			gd.grabExcessHorizontalSpace = true;
			gd.horizontalSpan = 2;
			zoneSelector.setLayoutData(gd);
		}

		if (enableShowAgainFlag)
		{
   		checkCreateAnother = new Button(dialogArea, SWT.CHECK);
   		checkCreateAnother.setText(i18n.tr("Show this dialog again to &create another node"));
   		checkCreateAnother.setSelection(showAgain);
         gd = new GridData();
         gd.horizontalAlignment = SWT.FILL;
         gd.grabExcessHorizontalSpace = true;
         gd.horizontalSpan = 2;
         checkCreateAnother.setLayoutData(gd);
		}		
		return dialogArea;
	}

   /**
	 * @see org.eclipse.jface.dialogs.Dialog#okPressed()
	 */
	@Override
	protected void okPressed()
	{
		// Validate primary name
		hostName = hostNameField.getText().trim();
		if (hostName.isEmpty())
			hostName = objectNameField.getText().trim();
		if (!hostName.matches("^(([A-Za-z0-9_-]+\\.)*[A-Za-z0-9_-]+|[A-Fa-f0-9:]+)$")) //$NON-NLS-1$
		{
			MessageDialogHelper.openWarning(getShell(), i18n.tr("Warning"), 
               String.format(i18n.tr("String \"%s\" is not a valid host name or IP address. Please enter valid host name or IP address as primary host name"), hostName));
			return;
		}

		objectName = objectNameField.getText().trim();
		if (objectName.isEmpty())
			objectName = hostName;

		creationFlags = 0;
		if (checkUnmanaged.getSelection())
			creationFlags |= NXCObjectCreationData.CF_CREATE_UNMANAGED;
      if (checkMaintenanceMode.getSelection())
         creationFlags |= NXCObjectCreationData.CF_ENTER_MAINTENANCE;
      if (session.isZoningEnabled() && checkAsZoneProxy.getSelection())
         creationFlags |= NXCObjectCreationData.CF_AS_ZONE_PROXY;
		if (checkDisableAgent.getSelection())
			creationFlags |= NXCObjectCreationData.CF_DISABLE_NXCP;
		if (checkDisablePing.getSelection())
			creationFlags |= NXCObjectCreationData.CF_DISABLE_ICMP;
		if (checkDisableSNMP.getSelection())
			creationFlags |= NXCObjectCreationData.CF_DISABLE_SNMP;
      if (checkDisableSSH.getSelection())
         creationFlags |= NXCObjectCreationData.CF_DISABLE_SSH;
      if (checkDisableEtherNetIP.getSelection())
         creationFlags |= NXCObjectCreationData.CF_DISABLE_ETHERNET_IP;
      if (checkDisableAutomaticSNMPConfig.getSelection())
         creationFlags |= NXCObjectCreationData.CF_SNMP_SETTINGS_LOCKED;
      if (checkRemoteManagementNode.getSelection())
         creationFlags |= NXCObjectCreationData.CF_EXTERNAL_GATEWAY;

		agentPort = agentPortField.getSelection();
		snmpPort = snmpPortField.getSelection();
      etherNetIpPort = etherNetIpPortField.getSelection();
      sshPort = sshPortField.getSelection();

		sshLogin = sshLoginField.getText().trim();
		sshPassword = sshPasswordField.getText();

		agentProxy = agentProxySelector.getObjectId();
		snmpProxy = snmpProxySelector.getObjectId();
      etherNetIpProxy = etherNetIpProxySelector.getObjectId();
      icmpProxy = icmpProxySelector.getObjectId();
      sshProxy = sshProxySelector.getObjectId();
      webServiceProxy = webServiceProxySelector.getObjectId();

      if (session.isZoningEnabled())
		{
		   zoneUIN = zoneSelector.getZoneUIN();
		}

		showAgain = enableShowAgainFlag ? checkCreateAnother.getSelection() : false;
		super.okPressed();
	}

	/**
	 * @return the name
	 */
	public String getObjectName()
	{
		return objectName;
	}

   /**
    * @param objectName the objectName to set
    */
   public void setObjectName(String objectName)
   {
      this.objectName = objectName;
   }

   /**
	 * @return the hostName
	 */
	public String getHostName()
	{
		return hostName;
	}

	/**
	 * @return the creationFlags
	 */
	public int getCreationFlags()
	{
		return creationFlags;
	}

	/**
	 * @return the agentProxy
	 */
	public long getAgentProxy()
	{
		return agentProxy;
	}

	/**
	 * @return the snmpProxy
	 */
	public long getSnmpProxy()
	{
		return snmpProxy;
	}

	/**
    * @return the etherNetIpProxy
    */
   public long getEtherNetIpProxy()
   {
      return etherNetIpProxy;
   }

   /**
    * @return the sshPort
    */
   public int getSshPort()
   {
      return sshPort;
   }

   /**
    * @return the icmpProxy
    */
   public long getIcmpProxy()
   {
      return icmpProxy;
   }

   /**
    * @return the sshProxy
    */
   public long getSshProxy()
   {
      return sshProxy;
   }

   /**
    * @return the webServiceProxy
    */
   public long getWebServiceProxy()
   {
      return webServiceProxy;
   }

   /**
	 * @return the zoneId
	 */
	public int getZoneUIN()
	{
		return zoneUIN;
	}

	/**
	 * Set default zone UIN before opening dialog
	 * 
    * @param zoneUIN zone UIN
    */
   public void setZoneUIN(int zoneUIN)
   {
      this.zoneUIN = zoneUIN;
   }

   /**
	 * @return the agentPort
	 */
	public int getAgentPort()
	{
		return agentPort;
	}

	/**
	 * @return the snmpPort
	 */
	public int getSnmpPort()
	{
		return snmpPort;
	}

   /**
    * @return the etherNetIpPort
    */
   public int getEtherNetIpPort()
   {
      return etherNetIpPort;
   }

   /**
    * @return the sshLogin
    */
   public String getSshLogin()
   {
      return sshLogin;
   }

   /**
    * @return the sshPassword
    */
   public String getSshPassword()
   {
      return sshPassword;
   }

   /**
    * @return the showAgain
    */
   public boolean isShowAgain()
   {
      return showAgain;
   }

   /**
    * @return the enableShowAgainFlag
    */
   public boolean isEnableShowAgainFlag()
   {
      return enableShowAgainFlag;
   }

   /**
    * @param enableShowAgainFlag the enableShowAgainFlag to set
    */
   public void setEnableShowAgainFlag(boolean enableShowAgainFlag)
   {
      this.enableShowAgainFlag = enableShowAgainFlag;
   }
}