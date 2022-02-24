/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2021 Victor Kirhenshtein
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
package org.netxms.nxmc.modules.datacollection.views;

import java.util.ArrayList;
import java.util.List;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.action.Action;
import org.eclipse.jface.action.IMenuListener;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.action.Separator;
import org.eclipse.jface.preference.PreferenceDialog;
import org.eclipse.jface.preference.PreferenceManager;
import org.eclipse.jface.preference.PreferenceNode;
import org.eclipse.jface.viewers.ArrayContentProvider;
import org.eclipse.jface.viewers.DoubleClickEvent;
import org.eclipse.jface.viewers.IDoubleClickListener;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.window.Window;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.Shell;
import org.netxms.client.NXCSession;
import org.netxms.client.datacollection.ChartDciConfig;
import org.netxms.client.datacollection.DataCollectionItem;
import org.netxms.client.datacollection.DataCollectionObject;
import org.netxms.client.datacollection.DataCollectionTable;
import org.netxms.client.datacollection.DciValue;
import org.netxms.client.objects.DataCollectionTarget;
import org.netxms.nxmc.PreferenceStore;
import org.netxms.nxmc.Registry;
import org.netxms.nxmc.base.actions.ExportToCsvAction;
import org.netxms.nxmc.base.jobs.Job;
import org.netxms.nxmc.base.views.AbstractViewerFilter;
import org.netxms.nxmc.base.widgets.SortableTableViewer;
import org.netxms.nxmc.localization.LocalizationHelper;
import org.netxms.nxmc.modules.datacollection.DataCollectionObjectEditor;
import org.netxms.nxmc.modules.datacollection.propertypages.AccessControl;
import org.netxms.nxmc.modules.datacollection.propertypages.ClusterOptions;
import org.netxms.nxmc.modules.datacollection.propertypages.Comments;
import org.netxms.nxmc.modules.datacollection.propertypages.CustomSchedule;
import org.netxms.nxmc.modules.datacollection.propertypages.GeneralItem;
import org.netxms.nxmc.modules.datacollection.propertypages.GeneralTable;
import org.netxms.nxmc.modules.datacollection.propertypages.InstanceDiscovery;
import org.netxms.nxmc.modules.datacollection.propertypages.OtherOptions;
import org.netxms.nxmc.modules.datacollection.propertypages.OtherOptionsTable;
import org.netxms.nxmc.modules.datacollection.propertypages.PerfTab;
import org.netxms.nxmc.modules.datacollection.propertypages.TableColumns;
import org.netxms.nxmc.modules.datacollection.propertypages.Thresholds;
import org.netxms.nxmc.modules.datacollection.propertypages.Transformation;
import org.netxms.nxmc.modules.datacollection.views.helpers.LastValuesComparator;
import org.netxms.nxmc.modules.datacollection.views.helpers.LastValuesFilter;
import org.netxms.nxmc.modules.datacollection.views.helpers.LastValuesLabelProvider;
import org.netxms.nxmc.modules.objects.views.ObjectView;
import org.netxms.nxmc.resources.ResourceManager;
import org.netxms.nxmc.resources.SharedIcons;
import org.netxms.nxmc.tools.ViewRefreshController;
import org.netxms.nxmc.tools.VisibilityValidator;
import org.netxms.nxmc.tools.WidgetHelper;
import org.xnap.commons.i18n.I18n;

/**
 * "Data Collection" view
 */
public abstract class BaseDataCollectionView extends ObjectView
{
   private static final I18n i18n = LocalizationHelper.getI18n(BaseDataCollectionView.class);

   // Columns for "last values" mode
   public static final int LV_COLUMN_OWNER = 0;
   public static final int LV_COLUMN_ID = 1;
   public static final int LV_COLUMN_DESCRIPTION = 2;
   public static final int LV_COLUMN_VALUE = 3;
   public static final int LV_COLUMN_TIMESTAMP = 4;
   public static final int LV_COLUMN_THRESHOLD = 5;
   
   protected NXCSession session;
   protected SortableTableViewer viewer;

   private LastValuesLabelProvider labelProvider;
   private LastValuesComparator comparator;
   private LastValuesFilter lvFilter;
   private boolean autoRefreshEnabled = false;
   private int autoRefreshInterval = 30;  // in seconds
   private ViewRefreshController refreshController;
   
   protected Action actionLineChart;
   protected Action actionRawLineChart;
   protected Action actionUseMultipliers;
   protected Action actionShowErrors;
   protected Action actionShowDisabled;
   protected Action actionShowUnsupported;
   protected Action actionShowHidden;
   protected Action actionExportToCsv;
   protected Action actionExportAllToCsv;
   protected Action actionCopyToClipboard;
   protected Action actionCopyDciName;
   protected Action actionShowHistoryData;

   /**
    * @param name
    * @param image
    */
   public BaseDataCollectionView(String id, boolean hasFilter)
   {
      super(i18n.tr("Data Collection"), ResourceManager.getImageDescriptor("icons/object-views/last_values.png"), id, hasFilter); 
   }

   /**
    * @see org.netxms.nxmc.base.views.View#createContent(org.eclipse.swt.widgets.Composite)
    */
   @Override
   protected void createContent(Composite parent)
   {
      session = Registry.getSession();

      VisibilityValidator validator = new VisibilityValidator() { 
         @Override
         public boolean isVisible()
         {
            return BaseDataCollectionView.this.isActive();
         }
      };

      createLastValuesViewer(parent, validator);
      createActions();
   }
   
   /**
    * Create last values view
    */
   protected void createLastValuesViewer(Composite parent, VisibilityValidator validator)
   {
      String configPrefix = "LastValues";
      
      parent.setLayout(new FillLayout());
      
      // Setup table columns
      final String[] names = { i18n.tr("Owner"),  i18n.tr("ID"), i18n.tr("Description"), i18n.tr("Value"), i18n.tr("Timestamp"), i18n.tr("Threshold") };
      final int[] widths = { 250, 70, 250, 150, 120, 150 };
      viewer = new SortableTableViewer(parent, names, widths, 0, SWT.DOWN, SortableTableViewer.DEFAULT_STYLE);
   
      labelProvider = new LastValuesLabelProvider(viewer);
      comparator = new LastValuesComparator();
      lvFilter = new LastValuesFilter();
      viewer.setLabelProvider(labelProvider);
      viewer.setContentProvider(new ArrayContentProvider());
      viewer.setComparator(comparator);
      viewer.addFilter(lvFilter);
      setFilterClient(viewer, lvFilter); 
      if (isHideOwner())
         viewer.removeColumnById(LV_COLUMN_OWNER);
      
      postLastValueViewCreation(configPrefix, validator);
   }
   
   /**
    * If owner column should be hidden
    * 
    * @return true if should be hidden
    */
   protected boolean isHideOwner()
   {
      return true;
   }

   /**
    * Create pop-up menu
    */
   protected void createPopupMenu()
   {
      // Create menu manager.
      MenuManager menuMgr = new MenuManager();
      menuMgr.setRemoveAllWhenShown(true);
      menuMgr.addMenuListener(new IMenuListener() {
         public void menuAboutToShow(IMenuManager mgr)
         {
            fillContextMenu(mgr);
         }
      });

      // Create menu.
      Menu menu = menuMgr.createContextMenu(viewer.getControl());
      viewer.getControl().setMenu(menu);
   }

   /**
    * Fill context menu
    * 
    * @param mgr Menu manager
    */
   protected void fillContextMenu(final IMenuManager manager)
   {
      manager.add(actionLineChart);
      manager.add(actionRawLineChart);
      manager.add(actionShowHistoryData); 
      manager.add(new Separator());
      manager.add(actionCopyToClipboard);
      manager.add(actionCopyDciName);
      manager.add(actionExportToCsv);
      manager.add(actionExportAllToCsv);
      manager.add(new Separator());
      manager.add(actionUseMultipliers);
      manager.add(actionShowErrors);
      manager.add(actionShowDisabled);
      manager.add(actionShowUnsupported);
      manager.add(actionShowHidden);
   }

   /**
    * Create actions
    */
   protected void createActions()
   {
      actionExportToCsv = new ExportToCsvAction(this, viewer, true); 
      actionExportAllToCsv = new ExportToCsvAction(this, viewer, false);
      
      /*last values */
      
      actionUseMultipliers = new Action(i18n.tr("Use &multipliers"), Action.AS_CHECK_BOX) {
         @Override
         public void run()
         {
            setUseMultipliers(actionUseMultipliers.isChecked());
         }
      };
      actionUseMultipliers.setChecked(areMultipliersUsed());

      actionShowErrors = new Action(i18n.tr("Show collection &errors"), Action.AS_CHECK_BOX) {
         @Override
         public void run()
         {
            setShowErrors(actionShowErrors.isChecked());
         }
      };
      actionShowErrors.setChecked(isShowErrors());

      actionShowUnsupported = new Action(i18n.tr("Show &unsupported"), Action.AS_CHECK_BOX) {
         @Override
         public void run()
         {
            setShowUnsupported(actionShowUnsupported.isChecked());
         }
      };
      actionShowUnsupported.setChecked(isShowUnsupported());
      
      actionShowHidden = new Action(i18n.tr("Show &hidden"), Action.AS_CHECK_BOX) {
         @Override
         public void run()
         {
            setShowHidden(actionShowHidden.isChecked());
         }
      };
      actionShowHidden.setChecked(isShowHidden());
      
      actionShowDisabled = new Action(i18n.tr("Show disabled"), Action.AS_CHECK_BOX) {
         @Override
         public void run()
         {
            setShowDisabled(actionShowDisabled.isChecked());
         }
      };
      actionShowDisabled.setChecked(isShowDisabled());      
      
      actionCopyToClipboard = new Action(i18n.tr("&Copy to clipboard"), SharedIcons.COPY) {
         @Override
         public void run()
         {
            copySelection();
         }
      };
      
      actionCopyDciName = new Action(i18n.tr("Copy DCI name"), SharedIcons.COPY) {
         @Override
         public void run()
         {
            copySelectionDciName();
         }
      };

      actionLineChart = new Action(i18n.tr("&Line chart"), ResourceManager.getImageDescriptor("icons/chart_line.png")) {
         @Override
         public void run()
         {
            showLineChart(false);
         }
      };

      actionRawLineChart = new Action(i18n.tr("&Raw Data Line chart"), ResourceManager.getImageDescriptor("icons/chart_line.png")) {
         @Override
         public void run()
         {
            showLineChart(true);
         }
      };
      
      actionShowHistoryData = new Action(i18n.tr("History data"), SharedIcons.EDIT) {
         @Override
         public void run()
         {
            showHistoryData();
         }
      };
   }

   /**
    * Refresh DCI list
    */
   @Override
   public void refresh()
   {
      getDataFromServer();
   }
   
   /**
    * Get DCI id
    */
   protected long getDciId(Object dci)
   {
      return ((DciValue)dci).getId();
   }
   
   /**
    * Get object id
    */
   protected long getObjectId(Object dci)
   {
      return ((DciValue)dci).getNodeId();
   }

   /**
    * Show DCI properties dialog
    * 
    * @param object Data collection object
    * @return true if OK was pressed
    */
   protected boolean showDCIPropertyPages(final DataCollectionObject object)
   {
      DataCollectionObjectEditor dce = new DataCollectionObjectEditor(object);
      PreferenceManager pm = new PreferenceManager();  
      if (dce.getObject() instanceof DataCollectionItem)
         pm.addToRoot(new PreferenceNode("general", new GeneralItem(dce)));             
      else
         pm.addToRoot(new PreferenceNode("general", new GeneralTable(dce)));   
      pm.addToRoot(new PreferenceNode("clisterOptions", new ClusterOptions(dce)));    
      pm.addToRoot(new PreferenceNode("customSchedulte", new CustomSchedule(dce)));    
      if (dce.getObject() instanceof DataCollectionTable)
         pm.addToRoot(new PreferenceNode("columns", new TableColumns(dce)));    
      pm.addToRoot(new PreferenceNode("transformation", new Transformation(dce)));             
      pm.addToRoot(new PreferenceNode("thresholds", new Thresholds(dce)));        
      pm.addToRoot(new PreferenceNode("instanceDiscovery", new InstanceDiscovery(dce)));
      if (dce.getObject() instanceof DataCollectionItem)
         pm.addToRoot(new PreferenceNode("performanceTab", new PerfTab(dce)));
      pm.addToRoot(new PreferenceNode("accessControl", new AccessControl(dce)));
      if (dce.getObject() instanceof DataCollectionItem)
         pm.addToRoot(new PreferenceNode("otherOptions", new OtherOptions(dce)));             
      else
         pm.addToRoot(new PreferenceNode("otherOptions", new OtherOptionsTable(dce)));   
      pm.addToRoot(new PreferenceNode("comments", new Comments(dce)));  

      PreferenceDialog dlg = new PreferenceDialog(getWindow().getShell(), pm) {
         @Override
         protected void configureShell(Shell newShell)
         {
            super.configureShell(newShell);
            newShell.setText(String.format(i18n.tr("Properties for %s"), object.getName()));
         }
      };
      dlg.setBlockOnOpen(true);
      return dlg.open() == Window.OK;
   }
   
   /**
    * Create new filter for objects
    * @return
    */
   protected AbstractViewerFilter createFilter()
   {
      return new LastValuesFilter();
   }
   
   /**
    * Actions to make after last values view was created
    * 
    * @param configPrefix
    * @param validator
    */
   protected void postLastValueViewCreation(String configPrefix, VisibilityValidator validator)
   {
      WidgetHelper.restoreTableViewerSettings(viewer, configPrefix);

      final PreferenceStore ds = PreferenceStore.getInstance();

      refreshController = new ViewRefreshController(this, -1, new Runnable() {
         @Override
         public void run()
         {
            if (viewer.getTable().isDisposed())
               return;
            
            getDataFromServer();
         }
      }, validator);
      viewer.getTable().addDisposeListener(new DisposeListener() {
         @Override
         public void widgetDisposed(DisposeEvent e)
         {
            refreshController.dispose();
         }
      });
      
      viewer.addDoubleClickListener(new IDoubleClickListener() {
         @Override
         public void doubleClick(DoubleClickEvent event)
         {
            showLineChart(false);
         }
      });

      viewer.getTable().addDisposeListener(new DisposeListener() {
         @Override
         public void widgetDisposed(DisposeEvent e)
         {
            WidgetHelper.saveTableViewerSettings(viewer, configPrefix);
            ds.set(configPrefix + ".autoRefresh", autoRefreshEnabled);
            ds.set(configPrefix + ".autoRefreshInterval", autoRefreshEnabled);
            ds.set(configPrefix + ".useMultipliers", labelProvider.areMultipliersUsed());
            ds.set(configPrefix + ".showErrors", isShowErrors());
            ds.set(configPrefix + ".showDisabled", isShowDisabled());
            ds.set(configPrefix + ".showUnsupported", isShowUnsupported());
            ds.set(configPrefix + ".showHidden", isShowHidden());
         }
      });      
      
      autoRefreshInterval = ds.getAsInteger(configPrefix + ".autoRefreshInterval", autoRefreshInterval);
      setAutoRefreshEnabled(ds.getAsBoolean(configPrefix + ".autoRefresh", true));
      labelProvider.setUseMultipliers(ds.getAsBoolean(configPrefix + ".useMultipliers", true));

      boolean showErrors = ds.getAsBoolean(configPrefix + ".showErrors", true);
      labelProvider.setShowErrors(showErrors);
      comparator.setShowErrors(showErrors);

      lvFilter.setShowDisabled(ds.getAsBoolean(configPrefix + ".showDisabled", false));
      lvFilter.setShowUnsupported(ds.getAsBoolean(configPrefix + ".showUnsupported", false));
      lvFilter.setShowHidden(ds.getAsBoolean(configPrefix + ".showHidden", false));
      
      createPopupMenu();

      if ((validator == null) || validator.isVisible())
         getDataFromServer();
   }

   /**
    * Get data from server
    */
   protected void getDataFromServer()
   {
      if (getObject() == null)
      {
         viewer.setInput(new DciValue[0]);
         return;
      }

      final DataCollectionTarget jobTarget = (DataCollectionTarget)getObject();
      Job job = new Job(String.format(i18n.tr("Get DCI values for node %s"), jobTarget.getObjectName()), this) {
         @Override
         protected void run(IProgressMonitor monitor) throws Exception
         {
            final DciValue[] data = session.getLastValues(jobTarget.getObjectId());
            runInUIThread(new Runnable() {
               @Override
               public void run()
               {
                  if (!viewer.getTable().isDisposed() && (getObject() != null) && (getObject().getObjectId() == jobTarget.getObjectId()))
                  {
                     viewer.setInput(data);
                     clearMessages();
                  }
               }
            });
         }

         @Override
         protected String getErrorMessage()
         {
            return String.format(i18n.tr("Cannot get DCI values for node %s"), jobTarget.getObjectName());
         }
      };
      job.setUser(false);
      job.start();
   }

   /**
    * @return the autoRefreshEnabled
    */
   public boolean isAutoRefreshEnabled()
   {
      return autoRefreshEnabled;
   }

   /**
    * @param autoRefreshEnabled the autoRefreshEnabled to set
    */
   public void setAutoRefreshEnabled(boolean autoRefreshEnabled)
   {
      this.autoRefreshEnabled = autoRefreshEnabled;
      refreshController.setInterval(autoRefreshEnabled ? autoRefreshInterval : -1);
   }

   /**
    * @return the autoRefreshInterval
    */
   public int getAutoRefreshInterval()
   {
      return autoRefreshInterval;
   }

   /**
    * @param autoRefreshInterval the autoRefreshInterval to set
    */
   public void setAutoRefreshInterval(int autoRefreshInterval)
   {
      this.autoRefreshInterval = autoRefreshInterval;
   }
   
   /**
    * @return the useMultipliers
    */
   public boolean areMultipliersUsed()
   {
      return (labelProvider != null) ? labelProvider.areMultipliersUsed() : false;
   }

   /**
    * @param useMultipliers the useMultipliers to set
    */
   public void setUseMultipliers(boolean useMultipliers)
   {
      if (labelProvider != null)
      {
         labelProvider.setUseMultipliers(useMultipliers);
         if (viewer != null)
         {
            viewer.refresh(true);
         }
      }
   }
   
   /**
    * @return
    */
   public boolean isShowErrors()
   {
      return (labelProvider != null) ? labelProvider.isShowErrors() : false;
   }

   /**
    * @param show
    */
   public void setShowErrors(boolean show)
   {
      labelProvider.setShowErrors(show);
      comparator.setShowErrors(show);
      if (viewer != null)
      {
         viewer.refresh(true);
      }
   }
   
   /**
    * @return
    */
   public boolean isShowDisabled()
   {
      return (lvFilter != null) ? lvFilter.isShowDisabled() : false;
   }

   /**
    * @param show
    */
   public void setShowDisabled(boolean show)
   {
      lvFilter.setShowDisabled(show);
      if (viewer != null)
      {
         viewer.refresh(true);
      }
   }

   /**
    * @return
    */
   public boolean isShowUnsupported()
   {
      return (lvFilter != null) ? lvFilter.isShowUnsupported() : false;
   }

   /**
    * @param show
    */
   public void setShowUnsupported(boolean show)
   {
      lvFilter.setShowUnsupported(show);
      if (viewer != null)
      {
         viewer.refresh(true);
      }
   }

   /**
    * @return
    */
   public boolean isShowHidden()
   {
      return (lvFilter != null) ? lvFilter.isShowHidden() : false;
   }

   /**
    * @param show
    */
   public void setShowHidden(boolean show)
   {
      lvFilter.setShowHidden(show);
      if (viewer != null)
      {
         viewer.refresh(true);
      }
   }

   /**
    * Show line chart for selected items
    */
   private void showLineChart(boolean useRawValues)
   {
      IStructuredSelection selection = viewer.getStructuredSelection();
      if (selection.isEmpty())
         return;

      List<ChartDciConfig> items = new ArrayList<ChartDciConfig>(selection.size());
      for(Object o : selection.toList())
      {
         ChartDciConfig config = getConfigFromObject(o);
         config.useRawValues = useRawValues;
         items.add(config);
      }

      openView(new HistoricalGraphView(getObject(), items));
   }
   
   /**
    * Get chart configuration form object
    * @param o object
    * @return chart configuration
    */
   protected ChartDciConfig getConfigFromObject(Object o)
   {
      return new ChartDciConfig((DciValue)o);
   }

   /**
    * Show line chart for selected items
    */
   private void showHistoryData()
   {
      IStructuredSelection selection = viewer.getStructuredSelection();
      if (selection.isEmpty())
         return;

      for(Object dcObject : selection.toList())
      {
         if ((dcObject instanceof DataCollectionTable) || ((dcObject instanceof DciValue) &&
               ((DciValue)dcObject).getDcObjectType() == DataCollectionObject.DCO_TYPE_TABLE))
         {
            openView(new TableLastValuesView(getObject(), getObjectId(dcObject), getDciId(dcObject)));
         }
         else 
         {
            openView(new HistoricalDataView(getObject(), getObjectId(dcObject), getDciId(dcObject), null, null, null));
         }
      }
   }

   /**
    * Copy selection to clipboard
    */
   private void copySelection()
   {
      IStructuredSelection selection = viewer.getStructuredSelection();
      if (selection.isEmpty())
         return;

      final String nl = WidgetHelper.getNewLineCharacters();
      StringBuilder sb = new StringBuilder();
      for(Object o : selection.toList())
      {
         if (sb.length() > 0)
            sb.append(nl);
         DciValue v = (DciValue)o;
         sb.append(v.getDescription());
         sb.append(" = "); //$NON-NLS-1$
         sb.append(v.getValue());
      }
      if (selection.size() > 1)
         sb.append(nl);
      WidgetHelper.copyToClipboard(sb.toString());
   }
   
   /**
    * Copy DCI name of selection
    */
   private void copySelectionDciName()
   {
      IStructuredSelection selection = viewer.getStructuredSelection();
      if (selection.isEmpty())
         return;
      
      StringBuilder dciName = new StringBuilder();
      for(Object o : selection.toList())
      {
         if (dciName.length() > 0)
            dciName.append(' ');
         DciValue v = (DciValue)o;
         dciName.append(v.getName());
      }
      WidgetHelper.copyToClipboard(dciName.toString());
   }

   /**
    * @see org.netxms.nxmc.base.views.View#activate()
    */
   @Override
   public void activate()
   {
      super.activate();
      refresh();
   }

   /**
    * @see org.netxms.nxmc.modules.objects.views.ObjectView#isValidForContext(java.lang.Object)
    */
   @Override
   public boolean isValidForContext(Object context)
   {
      if (context == null)
         return false;
      
      if (context instanceof DataCollectionTarget)
         return true;
      
      return false;
   }

   /**
    * @see org.netxms.nxmc.base.views.View#getPriority()
    */
   @Override
   public int getPriority()
   {
      return 30;
   }
}