/***************************************************************************
 *   Copyright (C) 2010 by Joris Guisson                                   *
 *   joris.guisson@gmail.com                                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 ***************************************************************************/
#include <KLocale>
#include "signalcatcher.h"
#include "log.h"


namespace bt
{
	sigjmp_buf sigbus_env;
	
	static void signal_handler(int sig, siginfo_t *siginfo, void *ptr)
	{
		Q_UNUSED(siginfo);
		Q_UNUSED(ptr);
		Q_UNUSED(sig);
		// Jump to error handling code
		siglongjmp(sigbus_env, 1);
	}
	
	bool InstallBusHandler()
	{
		struct sigaction act;
		
		memset(&act, 0, sizeof(act));
		act.sa_sigaction = signal_handler;
		act.sa_flags = SA_SIGINFO;
		
		if (sigaction(SIGBUS, &act, 0) == -1) 
		{
			Out(SYS_GEN|LOG_IMPORTANT) << "Failed to set SIGBUS handler" << endl;
			return false;
		}
		
		return true;
	}
	
	BusError::BusError(bool write_operation) 
		: Error(write_operation ? i18n("Error when writing to disk") : i18n("Error when reading from disk")),
		write_operation(write_operation)
	{

	}
	BusError::~BusError()
	{

	}


}
