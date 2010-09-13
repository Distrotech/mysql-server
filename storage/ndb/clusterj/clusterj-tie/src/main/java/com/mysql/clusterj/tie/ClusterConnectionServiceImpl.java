/*
 *  Copyright (C) 2010 Sun Microsystems, Inc.
 *  All rights reserved. Use is subject to license terms.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
 */

package com.mysql.clusterj.tie;

import com.mysql.clusterj.ClusterJFatalUserException;
import com.mysql.clusterj.core.store.ClusterConnection;
import com.mysql.clusterj.core.util.I18NHelper;
import com.mysql.clusterj.core.util.Logger;
import com.mysql.clusterj.core.util.LoggerFactoryService;

/**
 *
 */
public class ClusterConnectionServiceImpl
        implements com.mysql.clusterj.core.store.ClusterConnectionService {

    /** My message translator */
    static final I18NHelper local = I18NHelper.getInstance(ClusterConnectionServiceImpl.class);

    /** My logger */
    static final Logger logger = LoggerFactoryService.getFactory()
            .getInstance(ClusterConnectionServiceImpl.class);

    static {
        LoggerFactoryService.getFactory().registerLogger("com.mysql.clusterj.tie");
    }

    public ClusterConnection create(String connectString) {
        try {
            return new ClusterConnectionImpl(connectString);
        } catch (Exception e) {
            String message = local.message("ERR_Connect", connectString);
            logger.fatal(message);
            throw new ClusterJFatalUserException(message, e);
        }
    }

}
