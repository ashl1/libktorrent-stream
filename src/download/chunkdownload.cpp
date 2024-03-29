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
#include "chunkdownload.h"
#include <klocale.h>
#include <algorithm>
#include <util/file.h>
#include <util/log.h>
#include <util/array.h>
#include <util/error.h>
#include <diskio/chunk.h>
#include <diskio/piecedata.h>
#include <download/piece.h>
#include <interfaces/piecedownloader.h>

#include "downloader.h"

namespace bt
{
	DownloadStatus::DownloadStatus() : timeouts(0)
	{}

	DownloadStatus::~DownloadStatus()
	{}

	void DownloadStatus::add(Uint32 p)
	{
		status.insert(p);
	}
		
	void DownloadStatus::remove(Uint32 p)
	{
		status.remove(p);
	}
		
	bool DownloadStatus::contains(Uint32 p)
	{
		return status.contains(p);
	}
	
	void DownloadStatus::clear()
	{
		status.clear();
	}


	////////////////////////////////////////////////////
	
	ChunkDownload::ChunkDownload(Chunk* chunk) : chunk(chunk)
	{
		total_pieces_number = downloaded_pieces_number = 0;
		total_pieces_number = chunk->getSize() / MAX_PIECE_LEN;
		
		if (chunk->getSize() % MAX_PIECE_LEN != 0)
		{
			last_size = chunk->getSize() % MAX_PIECE_LEN;
			total_pieces_number++;
		}
		else
		{
			last_size = MAX_PIECE_LEN;
		}
		
		pieces = BitSet(total_pieces_number);
		pieces.clear();
		piece_data = new PieceData::Ptr[total_pieces_number]; // array of pointers to the piece data
		
		dstatus.setAutoDelete(true);

		num_pieces_in_hash = 0;
		hash_gen.start();
	}

	ChunkDownload::~ChunkDownload()
	{
		delete [] piece_data;
	}

	bool ChunkDownload::pieceReceived(const Piece & p,bool & is_needed)
	{
		is_needed = false;
		timer.update();
			
		Uint32 pp = p.getOffset() / MAX_PIECE_LEN;
		Uint32 len = pp == total_pieces_number - 1 ? last_size : MAX_PIECE_LEN;
		if (pp >= total_pieces_number || pieces.get(pp) || p.getLength() != len)
			return false;
	
		DownloadStatus* ds = dstatus.find(p.getPieceDownloader());
		if (ds)
			ds->remove(pp);
		
		PieceData::Ptr buf = chunk->getPiece(p.getOffset(),p.getLength(),false);
		if (buf && buf->write(p.getData(),p.getLength()) == p.getLength())
		{
			piece_data[pp] = buf;
			is_needed = true;
			pieces.set(pp,true);
			piece_providers.insert(p.getPieceDownloader());
			downloaded_pieces_number++;
			if (pdown.count() > 1)
			{
				endgameCancel(p);
			}
			
			updateHash();
			
			if (downloaded_pieces_number >= total_pieces_number)
			{
				// finalize hash
				hash_gen.end();
				releaseAllPDs();
				return true;
			}
		}
		
		/// TODO: Cancel downloading this piece from all PieceDownloaders
		sendRequests();
		return false;
	}
	
	void ChunkDownload::releaseAllPDs()
	{
		foreach (PieceDownloader* pd,pdown)
		{
			pd->release();
			sendCancels(pd);
			disconnect(pd,SIGNAL(timedout(bt::Request)),this,SLOT(onTimeout(bt::Request)));
			disconnect(pd,SIGNAL(rejected(bt::Request)),this,SLOT(onRejected(bt::Request)));
		}
		dstatus.clear();
		pdown.clear();
	}
	
	bool ChunkDownload::assign(PieceDownloader* pd)
	{
		if (!pd || pdown.contains(pd))
			return false;
			
		pd->grab();
		pdown.append(pd);
		dstatus.insert(pd,new DownloadStatus());
		connect(pd,SIGNAL(timedout(bt::Request)),this,SLOT(onTimeout(bt::Request)));
		connect(pd,SIGNAL(rejected(bt::Request)),this,SLOT(onRejected(bt::Request)));
		sendRequests();
		return true;
	}
	
	void ChunkDownload::release(PieceDownloader* pd)
	{
		if (!pdown.contains(pd))
			return;
		
		pd->release();
		sendCancels(pd);
		disconnect(pd,SIGNAL(timedout(bt::Request)),this,SLOT(onTimeout(bt::Request)));
		disconnect(pd,SIGNAL(rejected(bt::Request)),this,SLOT(onRejected(bt::Request)));
		dstatus.erase(pd);
		pdown.removeAll(pd);
	}

	
	void ChunkDownload::notDownloaded(const Request & r,bool reject)
	{
		// find the peer 
		DownloadStatus* ds = dstatus.find(r.getPieceDownloader());
		if (ds)
		{
			//	Out(SYS_DIO|LOG_DEBUG) << "ds != 0"  << endl;
			Uint32 p  = r.getOffset() / MAX_PIECE_LEN;
			ds->remove(p);
			
			PieceDownloader* pd = r.getPieceDownloader();
			if (reject)
			{
				// reject, so release the PieceDownloader
				release(pd);
			}
			else
			{
				pd->cancel(r); // cancel request
				ds->timeout();
				// if we have more then one PieceDownloader and there are timeouts, release it
				if (ds->numTimeouts() > 0 && pdown.count() > 0)
					release(pd);
			}
		}
			
		sendRequests();
	}
	
	void ChunkDownload::onRejected(const Request & r)
	{
		if (chunk->getIndex() == r.getChunkIndex())
		{
			notDownloaded(r,true);
		}
	}
	
	void ChunkDownload::onTimeout(const Request & r)
	{
		// see if we are dealing with a piece of ours
		if (chunk->getIndex() == r.getChunkIndex())
		{
			Out(SYS_CON|LOG_DEBUG) << QString("Request timed out %1 %2 %3 %4").arg(r.getChunkIndex()).arg(r.getOffset()).arg(r.getLength()).arg(r.getPieceDownloader()->getName()) << endl;
		
			notDownloaded(r,false);
		}
	}
	
	Uint32 ChunkDownload::bestPiece(PieceDownloader* pd)
	{
		Uint32 best = total_pieces_number;
		Uint32 best_count = 0;
		// select the piece which is being downloaded the least
		for (Uint32 i = 0;i < total_pieces_number;i++)
		{
			if (pieces.get(i))
				continue;
			
			DownloadStatus* ds = dstatus.find(pd);
			if (ds && ds->contains(i))
				continue;
			
			Uint32 times_downloading = 0;
			PtrMap<PieceDownloader*,DownloadStatus>::iterator j = dstatus.begin();
			while (j != dstatus.end())
			{
				if (j->first != pd && j->second->contains(i))
					times_downloading++;
				j++;
			}
			
			// nobody is downloading this piece, so return it
			if (times_downloading == 0)
				return i;
			
			// check if the piece is better then the current best
			if (best == total_pieces_number || best_count > times_downloading)
			{
				best_count = times_downloading;
				best = i;
			}
		}
		
		return best;
	}
	
	void ChunkDownload::sendRequests()
	{
		timer.update();
		QList<PieceDownloader*> tmp = pdown;
		
		while (tmp.count() > 0)
		{
			for (QList<PieceDownloader*>::iterator i = tmp.begin();i != tmp.end();)
			{
				PieceDownloader* pd = *i;
				if (!pd->isChoked() && pd->canAddRequest() && sendRequest(pd))
					i++;
				else
					i = tmp.erase(i);
			}
		}
	}
	
	bool ChunkDownload::sendRequest(PieceDownloader* pd)
	{
		DownloadStatus* ds = dstatus.find(pd);
		if (!ds || pd->isChoked())
			return false;
		
		// get the best piece to download
		Uint32 bp = bestPiece(pd);
		if (bp >= total_pieces_number)
			return false;
		
		pd->download(Request(chunk->getIndex(),bp*MAX_PIECE_LEN,bp+1<total_pieces_number ? MAX_PIECE_LEN : last_size,pd));
		ds->add(bp);
		
		if (nearlyDone())
			pd->setNearlyDone(true);
		
		return true;
	}	
	
	void ChunkDownload::update()
	{
		// go over all PD's and do requets again
		sendRequests();
	}
	
bool ChunkDownload::nearlyDone() const
	{
		Uint32 left = total_pieces_number - downloaded_pieces_number;
		return (left < 2) && (left > 0);
	}

	
	void ChunkDownload::sendCancels(PieceDownloader* pd)
	{
		DownloadStatus* ds = dstatus.find(pd);
		
		if (!ds)
		{
			Out(SYS_DIO|LOG_DEBUG) << "\tChunkDownload::sendCancels WON'T SEND CANCELS: DownloadStatus is undefned" << endl;
			return;
		}
		
		DownloadStatus::iterator itr = ds->begin();
		while (itr != ds->end())
		{
			Uint32 i = *itr;
			pd->cancel(
					Request(
						chunk->getIndex(),
						i*MAX_PIECE_LEN,
						i+1<total_pieces_number ? MAX_PIECE_LEN : last_size,0));
			itr++;
		}
		ds->clear();
		timer.update();
		
		Out(SYS_DIO|LOG_DEBUG) << "\tChunkDownload::sendCancels Cancels for chunk " << getChunkIndex() << " was sent" << endl;
	}
	
	void ChunkDownload::endgameCancel(const Piece & p)
	{
		QList<PieceDownloader*>::iterator i = pdown.begin();
		while (i != pdown.end())
		{
			PieceDownloader* pd = *i;
			DownloadStatus* ds = dstatus.find(pd);
			Uint32 pp = p.getOffset() / MAX_PIECE_LEN;
			if (ds && ds->contains(pp))
			{
				pd->cancel(Request(p));
				ds->remove(pp);
			}
			i++;
		}
	}

	Uint64 ChunkDownload::getAverageDownloadSpeed() const
	{
		/// TODO:
		return getDownloadSpeed();
	}

	
	Uint32 ChunkDownload::getChunkIndex() const
	{
		return chunk->getIndex();
	}

	QString ChunkDownload::getPieceDownloaderName() const
	{
		if (pdown.count() == 0)
		{
			return QString();
		}
		else if (pdown.count() == 1)
		{
			return pdown.first()->getName();
		}
		else
		{
			return i18np("1 peer","%1 peers",pdown.count());
		}
	}

	Uint32 ChunkDownload::getDownloadSpeed() const
	{
		Uint32 r = 0;
		foreach (PieceDownloader* pd,pdown)
			r += pd->getDownloadRate(getChunkIndex());
		
		return r;
	}

	void ChunkDownload::save(File & file)
	{	
		ChunkDownloadHeader hdr;
		hdr.index = chunk->getIndex();
		hdr.num_bits = pieces.getNumBits();
		hdr.buffered = true; // unused 
		// save the chunk header
		file.write(&hdr,sizeof(ChunkDownloadHeader));
		// save the bitset
		file.write(pieces.getData(),pieces.getNumBytes());
		
		// save how many PieceHeader structs are to be written
		Uint32 num_pieces_to_follow = 0;
		for (Uint32 i = 0;i < hdr.num_bits;i++)
			if (piece_data[i] && piece_data[i]->ok())
				num_pieces_to_follow++;
		
		file.write(&num_pieces_to_follow,sizeof(Uint32));
			
		// save all buffered pieces
		for (Uint32 i = 0;i < hdr.num_bits;i++)
		{
			if (!piece_data[i] || !piece_data[i]->ok())
				continue;
			
			PieceData::Ptr pd = piece_data[i];
			PieceHeader phdr;
			phdr.piece = i;
			phdr.size = pd->length();
			phdr.mapped = pd->mapped() ? 1 : 0;
			file.write(&phdr,sizeof(PieceHeader));
			if (!pd->mapped()) // buffered pieces need to be saved
			{
				pd->writeToFile(file,pd->length());
			}
		}
	}
		
	bool ChunkDownload::load(File & file,ChunkDownloadHeader & hdr,bool update_hash)
	{
		// read pieces
		if (hdr.num_bits != total_pieces_number)
			return false; 
		
		pieces = BitSet(hdr.num_bits);
		file.read(pieces.getData(),pieces.getNumBytes());
		pieces.updateNumOnBits();
		
		downloaded_pieces_number = pieces.numOnBits();
		Uint32 num_pieces_to_follow = 0;
		if (file.read(&num_pieces_to_follow,sizeof(Uint32)) != sizeof(Uint32) || num_pieces_to_follow > total_pieces_number)
			return false;
		
		for (Uint32 i = 0;i < num_pieces_to_follow;i++)
		{
			PieceHeader phdr;
			if (file.read(&phdr,sizeof(PieceHeader)) != sizeof(PieceHeader))
				return false;
			
			if (phdr.piece >= total_pieces_number)
				return false;
			
			PieceData::Ptr p = chunk->getPiece(phdr.piece * MAX_PIECE_LEN,phdr.size,false);
			if (!p)
				return false;
			
			if (!phdr.mapped)
			{
				if (p->readFromFile(file,p->length()) != p->length())
				{
					return false;
				}
			}
			piece_data[phdr.piece] = p;
		}
		
		// initialize hash
		if (update_hash)
		{
			num_pieces_in_hash = 0;
			updateHash();
		}
		
		// add a 0 downloader, so that pieces downloaded
		// in a previous session cannot get a peer banned in this session
		if (downloaded_pieces_number) 
			piece_providers.insert(0);

		return true;
	}

	Uint64 ChunkDownload::bytesDownloaded() const
	{
		Uint64 num_bytes = 0;
		for (Uint32 i = 0;i < total_pieces_number;i++)
		{
			if (pieces.get(i))
			{
				num_bytes += i == total_pieces_number-1 ? last_size : MAX_PIECE_LEN;
			}
		}
		return num_bytes;
	}

	Uint64 ChunkDownload::bytesLeft() const
	{
		return chunk->getSize() - bytesDownloaded();
	}

	
	void ChunkDownload::cancelAll()
	{
		QList<PieceDownloader*>::iterator i = pdown.begin();
		while (i != pdown.end())
		{
			sendCancels(*i);
			i++;
		}
	}

	PieceDownloader* ChunkDownload::getOnlyDownloader()
	{
		if (piece_providers.size() == 1)
		{
			return *piece_providers.begin();
		}
		else
		{
			return 0;
		}
	}

	void ChunkDownload::getStats(Stats & s)
	{
		s.chunk_index = chunk->getIndex();
		s.current_peer_id = getPieceDownloaderName();
		s.download_speed = getDownloadSpeed();
		s.num_downloaders = getNumDownloaders();
		s.pieces_downloaded = downloaded_pieces_number;
		s.total_pieces = total_pieces_number;
	}
	
	bool ChunkDownload::isChoked() const
	{
		QList<PieceDownloader*>::const_iterator i = pdown.begin();
		while (i != pdown.end())
		{
			const PieceDownloader* pd = *i;
			// if there is one which isn't choked 
			if (!pd->isChoked())
				return false;
			i++;
		}
		return true;
	}
	
	void ChunkDownload::updateHash()
	{
		// update the hash until where we can
		Uint32 nn = num_pieces_in_hash;
		while (nn < total_pieces_number && pieces.get(nn))
			nn++;
		
		for (Uint32 i = num_pieces_in_hash;i < nn;i++)
		{
			PieceData::Ptr piece = piece_data[i];
			Uint32 len = i == total_pieces_number - 1 ? last_size : MAX_PIECE_LEN;
			if (!piece)
				piece = chunk->getPiece(i*MAX_PIECE_LEN,len,true);
			
			if (piece && piece->ok())
			{
				piece->updateHash(hash_gen);
				chunk->savePiece(piece);
			}
		}
		num_pieces_in_hash = nn;
	}
	
}
#include "chunkdownload.moc"
