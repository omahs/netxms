/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2017 Raden Solutions
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
package org.netxms.nxmc.modules.datacollection.propertypages;

import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.netxms.client.datacollection.DataCollectionItem;
import org.netxms.client.objects.GenericObject;
import org.netxms.nxmc.localization.LocalizationHelper;
import org.netxms.nxmc.modules.datacollection.DataCollectionObjectEditor;
import org.netxms.nxmc.modules.objects.widgets.ObjectSelector;
import org.netxms.nxmc.tools.WidgetHelper;
import org.xnap.commons.i18n.I18n;

/**
 * @author Victor
 *
 */
public class OtherOptions extends AbstractDCIPropertyPage
{
   private static final I18n i18n = LocalizationHelper.getI18n(OtherOptions.class);
   
	private DataCollectionItem dci;
	private Button checkShowOnTooltip;
	private Button checkShowInOverview;
   private Button checkCalculateStatus;
   private Button checkHideOnLastValues;
   private Combo useMultipliers;
   private ObjectSelector relatedObject;
   
   /**
    * Constructor
    * 
    * @param editor
    */
   public OtherOptions(DataCollectionObjectEditor editor)
   {
      super(i18n.tr("Other Options"), editor);
   }

	/* (non-Javadoc)
	 * @see org.eclipse.jface.preference.PreferencePage#createContents(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	protected Control createContents(Composite parent)
	{
	   Composite dialogArea = (Composite)super.createContents(parent);
		dci = editor.getObjectAsItem();
		
		GridLayout layout = new GridLayout();
		layout.verticalSpacing = WidgetHelper.OUTER_SPACING;
		layout.marginWidth = 0;
		layout.marginHeight = 0;
      dialogArea.setLayout(layout);

      checkShowOnTooltip = new Button(dialogArea, SWT.CHECK);
      checkShowOnTooltip.setText(i18n.tr("&Show last value in object tooltips"));
      checkShowOnTooltip.setSelection(dci.isShowOnObjectTooltip());

      checkShowInOverview = new Button(dialogArea, SWT.CHECK);
      checkShowInOverview.setText(i18n.tr("Show last value in object overview"));
      checkShowInOverview.setSelection(dci.isShowInObjectOverview());

      checkCalculateStatus = new Button(dialogArea, SWT.CHECK);
      checkCalculateStatus.setText(i18n.tr("Use this DCI for node status calculation"));
      checkCalculateStatus.setSelection(dci.isUsedForNodeStatusCalculation());

      checkHideOnLastValues = new Button(dialogArea, SWT.CHECK);
      checkHideOnLastValues.setText("Hide value on \"Last Values\" page");
      checkHideOnLastValues.setSelection(dci.isHideOnLastValuesView());      
      
      useMultipliers = WidgetHelper.createLabeledCombo(dialogArea, SWT.READ_ONLY, "Use multipliers", new GridData());
      useMultipliers.add("Default");
      useMultipliers.add("Yes");
      useMultipliers.add("No");
      useMultipliers.select(dci.getMultipliersSelection());

      relatedObject = new ObjectSelector(dialogArea, SWT.NONE, true);
      relatedObject.setLabel("Related object");
      relatedObject.setObjectClass(GenericObject.class);
      relatedObject.setObjectId(dci.getRelatedObject());
      GridData gd = new GridData();
      gd.grabExcessHorizontalSpace = true;
      gd.horizontalAlignment = SWT.FILL;
      relatedObject.setLayoutData(gd);
      
		return dialogArea;
	}

	/**
	 * Apply changes
	 * 
	 * @param isApply true if update operation caused by "Apply" button
	 */
   @Override
	protected boolean applyChanges(final boolean isApply)
	{
		dci.setShowOnObjectTooltip(checkShowOnTooltip.getSelection());
		dci.setShowInObjectOverview(checkShowInOverview.getSelection());
      dci.setUsedForNodeStatusCalculation(checkCalculateStatus.getSelection());
      dci.setHideOnLastValuesView(checkHideOnLastValues.getSelection());
      dci.setMultiplierSelection(useMultipliers.getSelectionIndex());
      dci.setRelatedObject(relatedObject.getObjectId());
		editor.modify();
		return true;
	}

	/* (non-Javadoc)
	 * @see org.eclipse.jface.preference.PreferencePage#performDefaults()
	 */
	@Override
	protected void performDefaults()
	{
		super.performDefaults();
		checkShowOnTooltip.setSelection(false);
		checkShowInOverview.setSelection(false);
		checkCalculateStatus.setSelection(false);
		checkHideOnLastValues.setSelection(false);
		useMultipliers.select(0);
		relatedObject.setObjectId(0);
	}
}