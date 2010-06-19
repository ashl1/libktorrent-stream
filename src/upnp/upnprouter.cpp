/***************************************************************************
 *   Copyright (C) 2005-2007 by Joris Guisson                              *
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
#include <stdlib.h>
#include <QDir>
#include <klocale.h>
#include <kglobal.h>
#include <kstandarddirs.h>
#include <qstringlist.h>
#include <kio/netaccess.h>
#include <kio/job.h>
#include <torrent/globals.h>
#include <util/log.h>
#include <util/array.h>
#include <util/error.h>
#include <util/functions.h>
#include <util/fileops.h>
#include <util/waitjob.h>
#include <version.h>
#include "upnprouter.h"
#include "upnpdescriptionparser.h"
#include "soap.h"
#include "httprequest.h"

using namespace net;

namespace bt
{
	struct Forwarding 
	{
		net::Port port;
		HTTPRequest* pending_req;
		const UPnPService* service;
	};
	
	class UPnPRouter::UPnPRouterPrivate
	{
	public:
		UPnPRouterPrivate(const QString & server,const KUrl & location,bool verbose,UPnPRouter* parent); 
		~UPnPRouterPrivate();
		
		HTTPRequest* sendSoapQuery(const QString & query,const QString & soapact,const QString & controlurl,bool at_exit = false);
		void forward(const UPnPService* srv,const net::Port & port);
		void undoForward(const UPnPService* srv,const net::Port & port,bt::WaitJob* waitjob);
		void httpRequestDone(HTTPRequest* r,bool erase_fwd);
		
	public:
		QString server;
		KUrl location;
		UPnPDeviceDescription desc;
		QList<UPnPService> services;
		QList<Forwarding> fwds;
		QList<HTTPRequest*> active_reqs;
		QString error;
		bool verbose;
		UPnPRouter* parent;
	};
	
	////////////////////////////////////
	
	UPnPService::UPnPService()
	{
	}
	
	UPnPService::UPnPService(const UPnPService & s)
	{
		this->servicetype = s.servicetype;
		this->controlurl = s.controlurl;
		this->eventsuburl = s.eventsuburl;
		this->serviceid = s.serviceid;
		this->scpdurl = s.scpdurl;
	}

	void UPnPService::setProperty(const QString & name,const QString & value)
	{
		if (name == "serviceType")
			servicetype = value;
		else if (name == "controlURL")
			controlurl = value;
		else if (name == "eventSubURL")
			eventsuburl = value;
		else if (name == "SCPDURL")
			scpdurl = value;
		else if (name == "serviceId")
			serviceid = value;
	}
	
	void UPnPService::clear()
	{
		servicetype = controlurl = eventsuburl = scpdurl = serviceid = "";
	}

	UPnPService & UPnPService::operator = (const UPnPService & s)
	{
		this->servicetype = s.servicetype;
		this->controlurl = s.controlurl;
		this->eventsuburl = s.eventsuburl;
		this->serviceid = s.serviceid;
		this->scpdurl = s.scpdurl;
		return *this;
	}
	
	///////////////////////////////////////
	
	void UPnPDeviceDescription::setProperty(const QString & name,const QString & value)
	{
		if (name == "friendlyName")
			friendlyName = value;
		else if (name == "manufacturer")
			manufacturer = value;
		else if (name == "modelDescription")
			modelDescription = value;
		else if (name == "modelName")
			modelName = value;
		else if (name == "modelNumber")
			modelNumber == value;
	}
	
	///////////////////////////////////////
	
	
	
	UPnPRouter::UPnPRouter(const QString & server,const KUrl & location,bool verbose) 
		: d(new UPnPRouterPrivate(server,location,verbose,this))
	{
	}
	
	
	UPnPRouter::~UPnPRouter()
	{
		delete d;
	}
	
	void UPnPRouter::addService(const UPnPService & s)
	{
		foreach (const UPnPService & os,d->services)
		{
			if (s.servicetype == os.servicetype)
				return;
		}
		d->services.append(s);
	}
	
	void UPnPRouter::downloadFinished(KJob* j)
	{
		if (j->error())
		{
			d->error = i18n("Failed to download %1 : %2",d->location.prettyUrl(),j->errorString());
			Out(SYS_PNP|LOG_IMPORTANT) << d->error << endl;
			return;
		}
		
		KIO::StoredTransferJob* st = (KIO::StoredTransferJob*)j;
		// load in the file (target is always local)
		UPnPDescriptionParser desc_parse;
		bool ret = desc_parse.parse(st->data(),this);
		if (!ret)
		{
			d->error = i18n("Error parsing router description.");
		}
	
		xmlFileDownloaded(this,ret);
	}
	
	void UPnPRouter::downloadXMLFile()
	{
		d->error = QString();
		// downlaod XML description into a temporary file in /tmp
		Out(SYS_PNP|LOG_DEBUG) << "Downloading XML file " << d->location << endl;
		KIO::Job* job = KIO::storedGet(d->location,KIO::NoReload, KIO::Overwrite | KIO::HideProgressInfo);
		connect(job,SIGNAL(result(KJob *)),this,SLOT(downloadFinished( KJob* )));
	}

	void UPnPRouter::forward(const net::Port & port)
	{
		if (!d->error.isEmpty())
		{
			d->error = QString();
			stateChanged();
		}
		
		bool found = false;
		Out(SYS_PNP|LOG_NOTICE) << "Forwarding port " << port.number << " (" << (port.proto == UDP ? "UDP" : "TCP") << ")" << endl;
		// first find the right service
		foreach (const UPnPService & s,d->services)
		{
			if (s.servicetype.contains("WANIPConnection") || s.servicetype.contains("WANPPPConnection"))
			{
				d->forward(&s,port);
				found = true;
			}
		}
		
		if (!found)
		{
			d->error = i18n("Forwarding failed: \nDevice does not have a WANIPConnection or WANPPPConnection.");
			Out(SYS_PNP|LOG_IMPORTANT) << d->error << endl;
			stateChanged();
		}
	}
	

	
	
	void UPnPRouter::undoForward(const net::Port & port,bt::WaitJob* waitjob)
	{
		Out(SYS_PNP|LOG_NOTICE) << "Undoing forward of port " << port.number 
				<< " (" << (port.proto == UDP ? "UDP" : "TCP") << ")" << endl;
		
		QList<Forwarding>::iterator itr = d->fwds.begin();
		while (itr != d->fwds.end())
		{
			Forwarding & wd = *itr;
			if (wd.port == port)
			{
				d->undoForward(wd.service,wd.port,waitjob);
				itr = d->fwds.erase(itr);
			}
			else
			{
				itr++;
			}
		}
		
		stateChanged();
	}
	

	
	void UPnPRouter::onReplyOK(HTTPRequest* r,const QString &)
	{
		if (d->verbose)
			Out(SYS_PNP|LOG_NOTICE) << "UPnPRouter : OK" << endl;
		
		if (!d->error.isEmpty())
			d->error = QString();
		
		d->httpRequestDone(r,false);
	}
	
	void UPnPRouter::onReplyError(HTTPRequest* r,const QString &)
	{
		if (d->verbose)
			Out(SYS_PNP|LOG_IMPORTANT) << "UPnPRouter : Error" << endl;
		
		d->httpRequestDone(r,true);
		
	}
	
	void UPnPRouter::onError(HTTPRequest* r,const QString & err)
	{
		d->httpRequestDone(r,true);
		if (d->fwds.count() == 0)
		{
			d->error = err;
			stateChanged();
		}
	}
	
#if 0

	void UPnPRouter::getExternalIP()
	{
		// first find the right service
		QList<UPnPService>::iterator i = findPortForwardingService();
		if (i == services.end())
			throw Error(i18n("Cannot find port forwarding service in the device's description."));
		
		UPnPService & s = *i;
		QString action = "GetExternalIPAddress";
		QString comm = SOAP::createCommand(action,s.servicetype);
		sendSoapQuery(comm,s.servicetype + "#" + action,s.controlurl);
	}
	
	void UPnPRouter::isPortForwarded(const net::Port & port)
	{
		// first find the right service
		QList<UPnPService>::iterator i = findPortForwardingService();
		if (i == services.end())
			throw Error(i18n("Cannot find port forwarding service in the device's description."));
		
		// add all the arguments for the command
		QList<SOAP::Arg> args;
		SOAP::Arg a;
		a.element = "NewRemoteHost";
		args.append(a);
		
		// the external port
		a.element = "NewExternalPort";
		a.value = QString::number(port.number);
		args.append(a);
		
		// the protocol
		a.element = "NewProtocol";
		a.value = port.proto == TCP ? "TCP" : "UDP";
		args.append(a);
		
		UPnPService & s = *i;
		QString action = "GetSpecificPortMappingEntry";
		QString comm = SOAP::createCommand(action,s.servicetype,args);
		sendSoapQuery(comm,s.servicetype + "#" + action,s.controlurl);
	}
#endif

	
	void UPnPRouter::setVerbose(bool v) 
	{
		d->verbose = v;
	}

	
	QString UPnPRouter::getServer() const 
	{
		return d->server;
	}

	KUrl UPnPRouter::getLocation() const 
	{
		return d->location;
	}
	
	UPnPDeviceDescription & UPnPRouter::getDescription()
	{
		return d->desc;
	}

	const UPnPDeviceDescription & UPnPRouter::getDescription() const 
	{
		return d->desc;
	}
	
	QString UPnPRouter::getError() const
	{
		return d->error;
	}

	void UPnPRouter::visit(UPnPRouter::Visitor* visitor) const
	{
		foreach (const Forwarding & fwd,d->fwds)
		{
			visitor->forwarding(fwd.port,fwd.pending_req != 0,fwd.service);
		}
	}


	////////////////////////////////////

	UPnPRouter::UPnPRouterPrivate::UPnPRouterPrivate(const QString& server, const KUrl& location,bool verbose,UPnPRouter* parent)
		: server(server),location(location),verbose(verbose),parent(parent)
	{
	}
	
	UPnPRouter::UPnPRouterPrivate::~UPnPRouterPrivate()
	{
		qDeleteAll(active_reqs);
	}

	HTTPRequest* UPnPRouter::UPnPRouterPrivate::sendSoapQuery(const QString & query,const QString & soapact,const QString & controlurl,bool at_exit)
	{
		// if port is not set, 0 will be returned 
		// thanks to Diego R. Brogna for spotting this bug
		if (location.port()<=0)
			location.setPort(80);
		
		QString http_hdr = QString(
				"POST %1 HTTP/1.1\r\n"
				"Host: %2:%3\r\n"
				"User-Agent: %5\r\n"
				"Content-length: $CONTENT_LENGTH\r\n"
				"Content-Type: text/xml\r\n"
				"SOAPAction: \"%4\"\r\n"
				"\r\n").arg(controlurl).arg(location.host()).arg(location.port()).arg(soapact).arg(bt::GetVersionString());
		
		HTTPRequest* r = new HTTPRequest(http_hdr,query,location.host(),location.port(),verbose);
		connect(r,SIGNAL(replyError(HTTPRequest* ,const QString& )),
				parent,SLOT(onReplyError(HTTPRequest* ,const QString& )));
		connect(r,SIGNAL(replyOK(HTTPRequest* ,const QString& )),
				parent,SLOT(onReplyOK(HTTPRequest* ,const QString& )));
		connect(r,SIGNAL(error(HTTPRequest*, const QString & )),
				parent,SLOT(onError(HTTPRequest*, const QString & )));
		r->start();
		if (!at_exit)
			active_reqs.append(r);
		return r;
	}
	
	void UPnPRouter::UPnPRouterPrivate::forward(const UPnPService* srv,const net::Port & port)
	{
		// add all the arguments for the command
		QList<SOAP::Arg> args;
		SOAP::Arg a;
		a.element = "NewRemoteHost";
		args.append(a);
		
		// the external port
		a.element = "NewExternalPort";
		a.value = QString::number(port.number);
		args.append(a);
		
		// the protocol
		a.element = "NewProtocol";
		a.value = port.proto == net::TCP ? "TCP" : "UDP";
		args.append(a);
		
		// the local port
		a.element = "NewInternalPort";
		a.value = QString::number(port.number);
		args.append(a);
		
		// the local IP address
		a.element = "NewInternalClient";
		a.value = "$LOCAL_IP";// will be replaced by our local ip in HTTPRequest
		args.append(a);
		
		a.element = "NewEnabled";
		a.value = "1";
		args.append(a);
		
		a.element = "NewPortMappingDescription";
		static Uint32 cnt = 0;
		a.value = QString("KTorrent UPNP %1").arg(cnt++);	// TODO: change this
		args.append(a);
		
		a.element = "NewLeaseDuration";
		a.value = "0";
		args.append(a);
		
		QString action = "AddPortMapping";
		QString comm = SOAP::createCommand(action,srv->servicetype,args);
		
		Forwarding fw = {port,0,srv};
		// erase old forwarding if one exists
		QList<Forwarding>::iterator itr = fwds.begin();
		while (itr != fwds.end())
		{
			Forwarding & fwo = *itr;
			if (fwo.port == port && fwo.service == srv)
				itr = fwds.erase(itr);
			else
				itr++;
		}
		
		fw.pending_req = sendSoapQuery(comm,srv->servicetype + "#" + action,srv->controlurl);
		fwds.append(fw);
	}
	
	void UPnPRouter::UPnPRouterPrivate::undoForward(const UPnPService* srv,const net::Port & port,bt::WaitJob* waitjob)
	{
		// add all the arguments for the command
		QList<SOAP::Arg> args;
		SOAP::Arg a;
		a.element = "NewRemoteHost";
		args.append(a);
		
		// the external port
		a.element = "NewExternalPort";
		a.value = QString::number(port.number);
		args.append(a);
		
		// the protocol
		a.element = "NewProtocol";
		a.value = port.proto == net::TCP ? "TCP" : "UDP";
		args.append(a);
		
		
		QString action = "DeletePortMapping";
		QString comm = SOAP::createCommand(action,srv->servicetype,args);
		HTTPRequest* r = sendSoapQuery(comm,srv->servicetype + "#" + action,srv->controlurl,waitjob != 0);
		
		if (waitjob)
			waitjob->addExitOperation(r);
	}
	
	void UPnPRouter::UPnPRouterPrivate::httpRequestDone(HTTPRequest* r,bool erase_fwd)
	{
		QList<Forwarding>::iterator i = fwds.begin();
		while (i != fwds.end())
		{
			Forwarding & fw = *i;
			if (fw.pending_req == r)
			{
				fw.pending_req = 0;
				if (erase_fwd)
					fwds.erase(i);
				break;
			}
			i++;
		}
		
		active_reqs.removeAll(r);
	}
}

#include "upnprouter.moc"