/***************************************************************************
 *   Copyright (C) 2005 by Joris Guisson                                   *
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
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ***************************************************************************/
#include "peerdownloader.h"

#include <math.h>
#include <util/functions.h>
#include <util/log.h>
#include "peer.h"
#include <download/piece.h>


namespace bt
{
	TimeStampedRequest::TimeStampedRequest()
	{
		time_stamp = bt::CurrentTime();
	}
			
	TimeStampedRequest::TimeStampedRequest(const Request & r) : req(r)
	{
		time_stamp = bt::CurrentTime();
	}
	
	TimeStampedRequest::TimeStampedRequest(const TimeStampedRequest & t) 
		: req(t.req),time_stamp(t.time_stamp)
	{
	}
	
	TimeStampedRequest::~TimeStampedRequest()
	{}

	bool TimeStampedRequest::operator == (const Request & r)
	{
		return r == req;
	}
	
	bool TimeStampedRequest::operator == (const TimeStampedRequest & r)
	{
		return r.req == req;
	}
	
	TimeStampedRequest & TimeStampedRequest::operator = (const Request & r)
	{
		time_stamp = bt::CurrentTime();
		req = r;
		return *this;
	}
	
	TimeStampedRequest & TimeStampedRequest::operator = (const TimeStampedRequest & r)
	{
		time_stamp = r.time_stamp;
		req = r.req;
		return *this;
	}

	PeerDownloader::PeerDownloader(Peer* peer,Uint32 chunk_size) : peer(peer),pieces_in_chunk(chunk_size / MAX_PIECE_LEN)
	{
		connect(peer,SIGNAL(destroyed()),this,SLOT(peerDestroyed()));
		max_wait_queue_size = 25;
	}


	PeerDownloader::~PeerDownloader() 
	{
	}
	
	QString PeerDownloader::getName() const
	{
		return peer->getPeerID().identifyClient();
	}

	bool PeerDownloader::canAddRequest() const
	{
		return (Uint32)wait_queue.count() < max_wait_queue_size;
	}
	
	bool PeerDownloader::canDownloadChunk() const
	{
		return !isNull() && (getNumGrabbed() < (int)getMaxChunkDownloads() || isNearlyDone()) && canAddRequest();
	}

	Uint32 PeerDownloader::getNumRequests() const 
	{
		return reqs.count() /*+ wait_queue.count() */;
	}
	
	void PeerDownloader::download(const Request & req)
	{
		if (!peer)
			return;
		
		wait_queue.append(req);
		update();
	}
	
	void PeerDownloader::cancel(const Request & req)
	{
		if (!peer)
			return;
		
		if (!wait_queue.removeAll(req))
		{
			reqs.removeAll(req);
			peer->sendCancel(req);
		}
	}
	
	void PeerDownloader::onRejected(const Request & req)
	{
		if (!peer)
			return;

		if (reqs.removeAll(req))
			rejected(req);
	}
	
	void PeerDownloader::cancelAll()
	{
		if (peer)
		{
			QList<TimeStampedRequest>::iterator i = reqs.begin();
			while (i != reqs.end())
			{
				TimeStampedRequest & tr = *i;
				peer->sendCancel(tr.req);
				i++;
			}
		}
	
		wait_queue.clear();
		reqs.clear();
	}

	void PeerDownloader::piece(const Piece & p)
	{
		Request r(p);
		if (!reqs.removeOne(r))
			wait_queue.removeAll(r);
	}
	
	void PeerDownloader::peerDestroyed()
	{
		peer = 0;
	}
	
	bool PeerDownloader::isChoked() const
	{
		if (peer)
			return peer->isChoked();
		else
			return true;
	}
	
	bool PeerDownloader::hasChunk(Uint32 idx) const
	{
		if (peer)
		{
// 			Out(SYS_DIO|LOG_DEBUG) << "\tPeerDownloader::hasChunk: peer " << (Uint64)peer << "\t" <<
// 				peer->getChunksAvailability().get(idx) << " chunk " << idx << endl;
			return peer->getChunksAvailability().get(idx);
		} else {
			return false;
		}
	}

	Uint32 PeerDownloader::getAverageDownloadRate() const
	{
		/// TODO:
		return getDownloadRate();
	}

	Uint32 PeerDownloader::getDownloadRate() const
	{
		if (peer)
			return peer->getDownloadRate();
		else
			return 0;
	}
	
	Uint32 PeerDownloader::getDownloadRate(Uint32 chunk_index) const
	{
		if (!reqs.isEmpty() && reqs.first().req.getChunkIndex() == chunk_index)
			return getDownloadRate();
		return 0;
	}
	
	void PeerDownloader::checkTimeouts()
	{
		TimeStamp now = bt::CurrentTime(); 
		// we use a 60 second interval
		const Uint32 MAX_INTERVAL = 60 * 1000;

		// expire any timed-out requests: the list is sorted with the
		// oldest requests at the front, so we simply pop off requests
		// until we find one that shouldn't be expired
		while (!reqs.isEmpty() && (now - reqs.first().time_stamp > MAX_INTERVAL))
			timedout(reqs.takeFirst().req);
	}

	Uint32 PeerDownloader::getMaxChunkDownloads() const
	{
		// get the download rate in KB/sec
		Uint32 rate_kbs = peer->getDownloadRate();
		rate_kbs = rate_kbs / 1024;
		Uint32 num_extra = rate_kbs / 25;
		
		if (pieces_in_chunk >= 16)
		{
			return 1 + 16 * num_extra / pieces_in_chunk;
		}
		else
		{
			return 1 + (16 / pieces_in_chunk) * num_extra;
		}
	}
	
	void PeerDownloader::choked()
	{
		// when the peers supports the fast extensions, choke does not mean that all
		// requests are rejected, so lets do nothing
		if (peer->getStats().fast_extensions)
			return;

		QList<TimeStampedRequest>::iterator i = reqs.begin();
		while (i != reqs.end())
		{
			TimeStampedRequest & tr = *i;
			rejected(tr.req);
			i++;
		}
		reqs.clear();
		
		QList<Request>::iterator j = wait_queue.begin();
		while (j != wait_queue.end())
		{
			Request & req = *j;
			rejected(req);
			j++;
		}
		wait_queue.clear();
	}
	
	void PeerDownloader::update()
	{ 
		// modify the interval if necessary
		double pieces_per_sec = (double)peer->getDownloadRate() / MAX_PIECE_LEN;	
		int max_reqs = 1 + (int)ceil(10*pieces_per_sec);
		// cap if client has supplied a reqq in extended protocol handshake
		if (max_reqs > peer->getStats().max_request_queue && peer->getStats().max_request_queue != 0)
			max_reqs = peer->getStats().max_request_queue;
		
		while (wait_queue.count() > 0 && reqs.count() < max_reqs)
		{
			// get a request from the wait queue and send that
			Request req = wait_queue.front();
			wait_queue.pop_front();
			TimeStampedRequest r = TimeStampedRequest(req);
			reqs.append(r);
			peer->sendRequest(req);
		}
		
		max_wait_queue_size = 2*max_reqs;
		if (max_wait_queue_size < 10)
			max_wait_queue_size = 10;
	}
}
