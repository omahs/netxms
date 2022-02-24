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
package org.netxms.nxmc.modules.objects.propertypages.helpers;

import org.eclipse.jface.viewers.ITableColorProvider;
import org.eclipse.jface.viewers.ITableLabelProvider;
import org.eclipse.jface.viewers.LabelProvider;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.graphics.Image;
import org.netxms.client.AccessListElement;
import org.netxms.client.NXCSession;
import org.netxms.client.users.AbstractUserObject;
import org.netxms.client.users.User;
import org.netxms.nxmc.Registry;
import org.netxms.nxmc.resources.SharedIcons;
import org.netxms.nxmc.resources.ThemeEngine;

/**
 * Label provider for NetXMS objects access lists
 */
public class AccessListLabelProvider extends LabelProvider implements ITableLabelProvider, ITableColorProvider
{
   private final NXCSession session = Registry.getSession();
   private final Color inheritedElementColor = ThemeEngine.getForegroundColor("List.DisabledItem");

   /**
    * @see org.eclipse.jface.viewers.ITableLabelProvider#getColumnImage(java.lang.Object, int)
    */
	@Override
	public Image getColumnImage(Object element, int columnIndex)
	{
      if ((columnIndex == 0) && (element instanceof AccessListElement))
      {
         long userId = ((AccessListElement)element).getUserId();
         AbstractUserObject userObject = session.findUserDBObjectById(userId, null);
         if (userObject != null)
         {
            return userObject instanceof User ? SharedIcons.IMG_USER : SharedIcons.IMG_GROUP;
         }
      }
		return null;
	}

   /**
    * @see org.eclipse.jface.viewers.ITableLabelProvider#getColumnText(java.lang.Object, int)
    */
	@Override
	public String getColumnText(Object element, int columnIndex)
	{
		switch(columnIndex)
		{
			case 0:
            long userId = ((AccessListElement)element).getUserId();
            AbstractUserObject userObject = session.findUserDBObjectById(userId, null);
            return (userObject != null) ? userObject.getName() : "[" + userId + "]";
			case 1:
				AccessListElement e = (AccessListElement)element;
				StringBuilder sb = new StringBuilder(16);
				sb.append(e.hasRead() ? 'R' : '-');
				sb.append(e.hasModify() ? 'M' : '-');
				sb.append(e.hasCreate() ? 'C' : '-');
				sb.append(e.hasDelete() ? 'D' : '-');
				sb.append(e.hasControl() ? 'O' : '-');
				sb.append(e.hasSendEvents() ? 'E' : '-');
				sb.append(e.hasReadAlarms() ? 'V' : '-');
				sb.append(e.hasAckAlarms() ? 'K' : '-');
				sb.append(e.hasTerminateAlarms() ? 'T' : '-');
				sb.append(e.hasPushData() ? 'P' : '-');
				sb.append(e.hasAccessControl() ? 'A' : '-');
				return sb.toString();
		}
		return null;
	}

   /**
    * @see org.eclipse.jface.viewers.ITableColorProvider#getForeground(java.lang.Object, int)
    */
   @Override
   public Color getForeground(Object element, int columnIndex)
   {
      return ((AccessListElement)element).isInherited() ? inheritedElementColor : null;
   }

   /**
    * @see org.eclipse.jface.viewers.ITableColorProvider#getBackground(java.lang.Object, int)
    */
   @Override
   public Color getBackground(Object element, int columnIndex)
   {
      return null;
   }
}