/***************************************************************************
 *   Copyright (C) 2012 by Alexey Shildyakov                               *
 *   ashl1future@gmail.com                                                 *
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

#include "managerofstream.h"

#include <assert.h>
#include <algorithm>
#include <QtCore/qmath.h>
#include <QTimer>
#include <boost/concept_check.hpp>
#include <limits>
#include <interfaces/piecedownloader.h>
#include <util/functions.h>
#include <util/log.h>
#include <diskio/chunk.h>
#include <diskio/chunkmanager.h>
#include "downloader.h"
#include "chunkdownload.h"
#include "streamingchunkselector.h"

namespace bt {
	
	ManagerOfStream::ManagerOfStream(QObject* parent, Downloader* downloader):QObject(parent), buffer_preferred_finish_to(0), buffer_preferred_starts_from(0), buffer_required_finish_to(0), buffer_required_starts_from(0), downloader(downloader), index_chunk_last_time_asked(0) {
		time_last_chunk_played_for = INITIAL_TIME_CHUNK_PLAYED_FOR;
		
		connect(dynamic_cast<StreamingChunkSelector*>(downloader->getChunkSelector()), SIGNAL(anotherChunkAsked(Uint32)), this, SLOT(chunkAsked(Uint32)));
		connect(downloader, SIGNAL(chunkDownloaded(Uint32)), this, SLOT(chunkDownloaded(Uint32)));
		
		timer = new QTimer(this);
		connect(timer, SIGNAL(timeout()), this, SLOT(checkAndMakeBufferRequiredBeInTime()));
	};
	
	/**
	 * Like pattern Decorator or, maybe, Proxy and Ghost (supports lazy load).
	 * The representation of the chunk - unite several methods implemented in different classes
	 *  (Chunk and ChunkDownload) to work inside ManagerOfStream
	 */
	class ManagerOfStream::Chunk {
	public:
		Chunk(ManagerOfStream* manager_of_stream, Uint32 chunk_index): chunk_index(chunk_index), chunk_download(0), chunk(0), manager_of_stream(manager_of_stream){};
		
		/**
		 * Determine if the chunk meets \ref \ManagerOfStream_Terminology_RequiredCondition
		 * @return True, if the chunk meets \ref ManagerOfStream_Terminology_RequiredCondition, false otherwise
		 */
		bool doesMeetRequirement();
		
		/**
		 * Determine the number of bytes left to download
		 */
		Uint64 bytesLeft();
		
		const bt::Chunk* getChunk();

		/**
		 * @return The pointer to the corresponded ChunkDownload whis have been downloading in the past or now,
		 *		or 0, if the chunk hasn't never been downloading.
		 * \sa Downloader::getChunkDownload()
		 */
		ChunkDownload* getChunkDownload();

		
		Uint32 getIndex() const;
		
		/**
		 * Determine the approximately (assumed) time based on downloading rates of the peers
		 *  this chunk is downloading by. If now it's not downloading the infinity will be returned.
		 * @return The approximately (assumed) time in milliseconds the chunk will be downloaded.
		 *  Return 0 in case the chunk is already downloaded;
		 *  Return infinity (TimeStamp max) in case the chunk is not downloading now
		 */			
		TimeStamp getTimeDownloadFinish();
		
		/**
		 * @return The approximately (assumed) time in milliseconds
		 *  the chunk will be required for playing. Return 0 if the chunk not required to play
		 *  (already played or in case the chunk was skipped, for example, by seeking media) or
		 *  is playing now. This function don't check the range of downloading (may return 
		 *  wrong big value for the chunks greater than the downloading range)
		 */
		TimeStamp getTimeUntilRequired();
		
		/**
		 * Check if the chunk has been already downloaded
		 */
		bool isDownloaded();
		
		bool isDownloading();
		
		/**
		 * Check if the downloading of the chunk will be finished soon.
		 * @return True if is now doanloading and the downloading nearly done.
		 *  False, in case the chunk has been already downloaded or never downloaded.
		 */
		bool isNearlyDone();
				
	private:	
		Uint32 chunk_index;
		ChunkDownload* chunk_download;
		const bt::Chunk* chunk;
		ManagerOfStream* manager_of_stream;
	};	
	
	Uint64 ManagerOfStream::Chunk::bytesLeft()
	{
		if (getChunkDownload())
			return chunk_download->bytesLeft();
		
		return getChunk()->getSize();
	}

	bool ManagerOfStream::Chunk::doesMeetRequirement()
	{
		Out(SYS_DIO|LOG_DEBUG) << "\tChunk " << chunk_index << " required for/download\t" <<
		  getTimeUntilRequired() << "\t" << getTimeDownloadFinish() << endl;
	  
		// may be equal when download finish for awaiting to play chunk (until required = 0 and download finish at 0 = already downloaded)
		return getTimeUntilRequired() >= getTimeDownloadFinish();
	}
	
	const bt::Chunk* ManagerOfStream::Chunk::getChunk()
	{
		if (!chunk)
			chunk = manager_of_stream->downloader->getChunkManager()->getChunk(chunk_index);
		return chunk;
	}

	ChunkDownload* ManagerOfStream::Chunk::getChunkDownload()
	{
		if (!chunk_download)
			chunk_download = manager_of_stream->downloader->getChunkDownload(chunk_index);
		// might be NULL
		return chunk_download;
	}

	Uint32 ManagerOfStream::Chunk::getIndex() const
	{
		return chunk_index;
	}
	
	TimeStamp ManagerOfStream::Chunk::getTimeDownloadFinish()
	{
// 		Out(SYS_DIO|LOG_DEBUG) << "Chunk::getTimeDownloadFinish - maybe 0?" << endl;
		if (isDownloaded())
			return 0;
		
// 		Out(SYS_DIO|LOG_DEBUG) << "Chunk::getTimeDownloadFinish - no calculate the speed" << endl;
		
		if (getChunkDownload())
		{
			Uint64 download_speed = chunk_download->getAverageDownloadSpeed();
			if (download_speed != 0)
				return SecondsToMSeconds(bytesLeft() / download_speed);
		}
		
// 		Out(SYS_DIO|LOG_DEBUG) << "Chunk::getTimeDownloadFinish - infinity" << endl;
		return std::numeric_limits<TimeStamp>::max();
	}
	
	TimeStamp ManagerOfStream::Chunk::getTimeUntilRequired()
	{
		const Uint32 chunksBeetweenPlayedAndCurrent = chunk_index - manager_of_stream->getCurrentPlayedChunkIndex();
// 		Out(SYS_DIO|LOG_DEBUG) << "\tManagerOfStream::Chunk::getTimeUntilRequired  chunk: " << chunk_index <<
// 		  ";    current chunk: " << manager_of_stream->getCurrentPlayedChunkIndex() << endl <<
// 		  "\t\tcurrent chunk finish playing in: " << manager_of_stream->getTimeCurrentChunkFinishPlaying() << " milliseconds" << endl <<
// 		  "\t\tchunk was played for " << manager_of_stream->time_last_chunk_played_for << " milliseconds" << endl << endl;
		return manager_of_stream->getTimeCurrentChunkFinishPlaying() + chunksBeetweenPlayedAndCurrent * manager_of_stream->time_last_chunk_played_for;
	}
	
	bool ManagerOfStream::Chunk::isDownloaded()
	{
		return getChunk()->getStatus() == bt::Chunk::ON_DISK;
	}

	bool ManagerOfStream::Chunk::isDownloading()
	{
		return manager_of_stream->downloader->isChunkDownloading(chunk_index);
	}
	
	bool ManagerOfStream::Chunk::isNearlyDone()
	{
		if (getChunkDownload())
			return chunk_download->nearlyDone();
		return false;
	}

	/**
	 * TODO: see the path, the sizeOfBufferRequired is using and make calculate this once per path
	 * May we should use member variable instead of recalculation every function
	 * Also search for using of updateBuffersRangeIndexes - the same situation
	 */
	
	void ManagerOfStream::checkAndMakeBufferRequiredBeInTime()
	{	
		Out(SYS_DIO|LOG_DEBUG) << endl;
		Out(SYS_DIO|LOG_DEBUG) <<
			"\t--------------------------------------------" << endl;
		Out(SYS_DIO|LOG_DEBUG) << 
			"\tManagerOfStream::checkAndMakeBufferRequiredBeInTime" << endl;
		Out(SYS_DIO|LOG_DEBUG) <<
			"\t--------------------------------------------" << endl;
		Uint32 chunk_index = 0;
		
		Out(SYS_DIO|LOG_DEBUG) << "\tBuffer required: " << buffer_required_starts_from << " - " << buffer_required_finish_to << endl;
		if (getChunkFromBufferRequiredNotMeetRequirement(chunk_index))
		{
			updateSortedPeersList();
// 			Out(SYS_DIO|LOG_DEBUG) << "\tManagerOfStream::checkAndMakeBufferRequiredBeInTime: Got chunk(" << chunk_index << ") doesn't meet" << endl;
			
			// continue from first found chunk doesn't meet RequiredCondition and for all in ReqiredBuffer
			for (; chunk_index <= buffer_required_finish_to; ++chunk_index)
			{
				if (! Chunk(this, chunk_index).doesMeetRequirement())
				{
					Out(SYS_DIO|LOG_DEBUG) << "\tReassign chunks from\toutside BufferPreferred\t to chunk\t" << chunk_index << endl;
					if (tryReassignPeers(peers_outside_preferred_buffer, chunk_index))
						continue;
					
					Out(SYS_DIO|LOG_DEBUG) << "\tReassign chunks from\tBufferPreferred\t to chunk\t" << chunk_index << endl;
					if (tryReassignPeers(peers_inside_preferred_buffer, chunk_index))
						continue;
					
					deletePeersDownloadedMorePrioritizedChunksFromList(peers_inside_required_buffer, chunk_index);
					
					Out(SYS_DIO|LOG_DEBUG) << "\tReassign chunks from\tBufferRequired\t to chunk\t" << chunk_index << endl;
					tryReassignPeers(peers_inside_required_buffer, chunk_index);
					
					if (peers_inside_required_buffer.size() == 0) {
						Out(SYS_DIO|LOG_DEBUG) << "\tDoesn't have any free peers to reassign" << endl;
						break;
					}
// 					Out(SYS_DIO|LOG_DEBUG) << "\tChunk" << chunk_index << " has been reassigned to" << endl;
				}
			}			
		}
		Out(SYS_DIO|LOG_DEBUG) << 
			"\t--------------------------------------------" << endl;
		Out(SYS_DIO|LOG_DEBUG) << 
			"\tManagerOfStream::checkAndMakeBufferRequiredBeInTime" << endl;
		Out(SYS_DIO|LOG_DEBUG) <<
			"\t++++++++++++++++++++++++++++++++++++++++++++" << endl;
		Out(SYS_DIO|LOG_DEBUG) << endl;

	}
	
	void ManagerOfStream::chunkAsked(Uint32 chunk_index)
	{
		Out(SYS_DIO|LOG_DEBUG) << "\tManagerOfStream::chunkAsked( " << chunk_index << " )" << endl;
	  
		TimeStamp now = Now();
		
		if (chunk_index == index_chunk_last_time_asked + 1)
		{
			// continue playing
			assert(now > last_time_new_chunk_was_asked);
			time_last_chunk_played_for = now - last_time_new_chunk_was_asked;
		} else {
			// seeking at another place
		}
		
		Out(SYS_DIO|LOG_DEBUG) << "\tManagerOfStream::chunkAsked" << endl;
		Out(SYS_DIO|LOG_DEBUG) <<
			"\t\tlast_time_new_chunk_was_asked = " << last_time_new_chunk_was_asked << endl;
		Out(SYS_DIO|LOG_DEBUG) <<
			"\t\tnow = " << now << endl;
		Out(SYS_DIO|LOG_DEBUG) <<
			"\t\ttime_last_chunk_played_for = " << time_last_chunk_played_for << endl;
		Out(SYS_DIO|LOG_DEBUG) << endl;
		
		last_time_new_chunk_was_asked = now;
		index_chunk_last_time_asked = chunk_index;
		
		checkAndMakeBufferRequiredBeInTime();
	}

	void ManagerOfStream::chunkDownloaded(Uint32 chunk_index)
	{
		Q_UNUSED(chunk_index)
		checkAndMakeBufferRequiredBeInTime();
	}
	
	/* static */ bool ManagerOfStream::cmpPeersInsideBufferPreferred(const PieceDownloader* first, const PieceDownloader* second)
	{
		return first->getDownloadRate() > second->getDownloadRate();
	}

	bool ManagerOfStream::CmpPeersInsideBufferRequired::operator()(const PieceDownloader* first, const PieceDownloader* second) const
	{
		Uint32 minimum_index_first = downloader->getMinimalIndexDownloadingChunk(first);
		Uint32 minimum_index_second = downloader->getMinimalIndexDownloadingChunk(second);
		if (minimum_index_first > minimum_index_second)
			return true;
		if (minimum_index_first == minimum_index_second)
			return first->getDownloadRate() > second->getDownloadRate();
		return false;
	}

	/* static */ bool ManagerOfStream::cmpPeersOutsideBufferPreferred(const PieceDownloader* first, const PieceDownloader* second)
	{
		return first->getDownloadRate() > second->getDownloadRate();
	}

	void ManagerOfStream::deletePeersDownloadedMorePrioritizedChunksFromList(QList<PieceDownloader*>& peers_sorted, Uint32 chunk_index)
	{
		while (!peers_sorted.isEmpty() && downloader->getMinimalIndexDownloadingChunk(peers_sorted.back()) <= chunk_index)
		{
			peers_sorted.pop_back();
		}
	}
	
	bool ManagerOfStream::getChunkFromBufferRequiredNotMeetRequirement(Uint32& chunk_index)
	{
		updateBuffersRangeIndexes();
		for (chunk_index = buffer_required_starts_from; chunk_index <= buffer_required_finish_to; ++chunk_index)
		{
			if (!Chunk(this, chunk_index).doesMeetRequirement())
			{
				return true;
			}
		}
		
		return false;
	}
	
	Uint32 ManagerOfStream::getCurrentPlayedChunkIndex() const
	{
		return dynamic_cast<StreamingChunkSelector*>(downloader->getChunkSelector())->getIndexChunkAskedLast();
	}

	TimeStamp ManagerOfStream::getTimeCurrentChunkFinishPlaying() const
	{
		TimeStamp timePlayed = Now() - last_time_new_chunk_was_asked;
		if (time_last_chunk_played_for > timePlayed)
			return time_last_chunk_played_for - timePlayed;
		return 0;
	}

	Uint32 ManagerOfStream::getSizeOfBufferPreferred() const
	{
		/// TODO:
		return 10;
	}
	
	Uint32 ManagerOfStream::getSizeOfBufferRequired() const
	{
		Out(SYS_DIO|LOG_DEBUG) << "\tChunk played for " << MSecondsToSeconds(time_last_chunk_played_for) << " seconds" << endl;
		Uint32 required_by_time = qCeil((qreal) SecondsToMSeconds(SECONDS_IN_BUFFER_REQUIRED) / time_last_chunk_played_for);
		return qMax(required_by_time, MIN_CHUNKS_STREAMING_BUFFER_REQUIRED);
	}
	
	void ManagerOfStream::Init()
	{
		last_time_new_chunk_was_asked = Now();
		timer->start(MSECONDS_MANAGER_OF_STREAM_UPDATED);
	}

	void ManagerOfStream::movePeersAssignedForChunksInRange(QList< PieceDownloader * >& peers_list_move_from, QList< PieceDownloader * >& peers_list_move_into, Uint32 chunk_index_range_starts_from, Uint32 chunk_index_range_finish_to)
	{
		bool assigned = false;
		ChunkDownload* chunk_download = NULL;
		for (QList<PieceDownloader*>::iterator peer = peers_list_move_from.begin(); peer != peers_list_move_from.end(); )
		{
			assigned = false;
			for (Uint32 chunk_index = chunk_index_range_starts_from; chunk_index <= chunk_index_range_finish_to; ++chunk_index)
			{
				chunk_download = downloader->getChunkDownload(chunk_index);
				if (chunk_download && chunk_download->containsPeer(*peer))
					assigned = true;
			}
			
			if (assigned)
			{
				peers_list_move_into.push_back(*peer);
				peer = peers_list_move_from.erase(peer);
			} else {
				++peer;
			}
		}
	}
	
	bool ManagerOfStream::selectChunkFromBufferPreferred(const bt::PieceDownloader* piece_downloader, Uint32& chunk_index)
	{
		updateBuffersRangeIndexes();
		
		Out(SYS_DIO|LOG_DEBUG) << "\tManagerOfStream::selectChunkFromBufferPreferred " << buffer_preferred_starts_from << " - " << buffer_preferred_finish_to << endl;
		
		// first try to find the not downloading chunk
		TimeStamp maximum_time_download = 0;
		Uint32 result_chunk_index = buffer_preferred_starts_from;
		TimeStamp time_download = 0;

		for (chunk_index = buffer_preferred_starts_from; chunk_index <= buffer_preferred_finish_to; ++chunk_index)
		{
			ManagerOfStream::Chunk chunk(this, chunk_index);
			if (!chunk.isDownloaded() && piece_downloader->hasChunk(chunk_index))
			{
				// use this to minimize the race condition by speed on the same chunk
				if (!chunk.isDownloading())
					return true;
				
				if (!chunk.doesMeetRequirement())
					return true;
		
				// then find the chunk with maximim download time
				if (time_download > maximum_time_download){
					maximum_time_download = time_download;
					result_chunk_index = chunk_index;
				}
			}
		}
		
		if (maximum_time_download != 0)
		{
			chunk_index = result_chunk_index;
			return true;
		}
		
		// if all chunks have already been downloaded
		return false;
	}
	
	bool ManagerOfStream::selectChunkFromBufferRequiredNotMeetRequirement(const bt::PieceDownloader* piece_downloader, Uint32& chunk_index)
	{
		updateBuffersRangeIndexes();
		Out(SYS_DIO|LOG_DEBUG) << "\tManagerOfStream::selectChunkFromBufferRequiredNotMeetRequirement " << buffer_required_starts_from << " - " << buffer_required_finish_to << endl;
		
		if (getChunkFromBufferRequiredNotMeetRequirement(chunk_index))
		{
			for (; chunk_index <= buffer_required_finish_to; ++chunk_index)
			{
				if (piece_downloader->hasChunk(chunk_index))
					return true;
			}
		}
		
		return false;
	}
	
	bool ManagerOfStream::tryReassignPeers(QList<PieceDownloader*> &peers_sorted, Uint32 chunk_reassign_to)
	{
// 		Out(SYS_DIO|LOG_DEBUG) << endl << "\tManagerOfStream::tryReassignPeers\twill proceed " << peers_sorted.size() << " peers" << endl;
		ManagerOfStream::Chunk chunk(this, chunk_reassign_to);
		// the rate in bytes/second
		Uint64 required_addition_download_rate;
		TimeStamp time_until_chunk_required = MSecondsToSeconds(chunk.getTimeUntilRequired());
		
		if (time_until_chunk_required == 0)
		{
			// if the downloading have not been started yet will not reassign all peers
			//  to not produce race condition on the Piece's. Wait for 1 second to download
			required_addition_download_rate = chunk.bytesLeft() / 1;
		} else {
			required_addition_download_rate = chunk.bytesLeft() / time_until_chunk_required + STREAMING_SPEED_RESERVE;
		}
		
		Uint64 actual_peer_download_rate = 0;
		
		for (QList<PieceDownloader*>::iterator peer = peers_sorted.begin(); peer != peers_sorted.end(); )
		{
			if ((*peer)->hasChunk(chunk.getIndex()))
			{
				downloader->stopAndReassignPieceDownloader((*peer), chunk.getIndex());
				actual_peer_download_rate = qMin<Uint64>((*peer)->getDownloadRate(), (*peer)->getAverageDownloadRate());
				peer = peers_sorted.erase(peer);

				if (required_addition_download_rate < actual_peer_download_rate)
					return true;
				required_addition_download_rate -= actual_peer_download_rate;
// 				Out(SYS_DIO|LOG_DEBUG) << "\tManagerOfStream::tryReassignPeers\t" << peers_sorted.size() << " peers left" << endl;
			} else {
				++peer;
			}
		}
		return false;
	}
	
	void ManagerOfStream::updateBuffersRangeIndexes()
	{
		const Uint32 range_end = dynamic_cast<StreamingChunkSelector*>(downloader->getChunkSelector())->range_end;
		const Uint32 current_played_chunk_index = getCurrentPlayedChunkIndex();
		buffer_required_starts_from = current_played_chunk_index;
		if (Chunk(this, current_played_chunk_index).isDownloaded() && buffer_required_starts_from != range_end)
		{
			 buffer_required_starts_from += 1;
		}
		
		buffer_required_finish_to = qMin(buffer_required_starts_from + getSizeOfBufferRequired() - 1, range_end);
		buffer_preferred_starts_from = qMin(buffer_required_finish_to + 1, range_end);
		buffer_preferred_finish_to = qMin(buffer_required_finish_to + getSizeOfBufferPreferred() - 1, range_end);
	}

	void ManagerOfStream::updateSortedPeersList()
	{
		updateBuffersRangeIndexes();
		peers_outside_preferred_buffer = downloader->getPieceDownloaders();
		
		peers_inside_required_buffer.clear();
		peers_inside_preferred_buffer.clear();
		
		Out(SYS_DIO|LOG_DEBUG) << "\t updateSortedPeersList: " << peers_outside_preferred_buffer.size() << " peers should divide" << endl;
		movePeersAssignedForChunksInRange(peers_outside_preferred_buffer, peers_inside_required_buffer, buffer_required_starts_from, buffer_required_finish_to);
		Out(SYS_DIO|LOG_DEBUG) << "\t updateSortedPeersList: " << peers_inside_required_buffer.size() << " peers in BufferRequired" << endl;
		movePeersAssignedForChunksInRange(peers_outside_preferred_buffer, peers_inside_preferred_buffer, buffer_preferred_starts_from, buffer_preferred_finish_to);
		Out(SYS_DIO|LOG_DEBUG) << "\t updateSortedPeersList: " << peers_inside_preferred_buffer.size() << " peers in BufferPreferred" << endl;
		
		// now peers_outside_preferred_buffer contain only the PieceDownloaders are now downloading
		//  the chunks outside buffer_preffered and buffer_required
		Out(SYS_DIO|LOG_DEBUG) << "\t updateSortedPeersList: " << peers_outside_preferred_buffer.size() << " peers outside BufferRequired" << endl;

		CmpPeersInsideBufferRequired cmpPeersInsideBufferRequired(downloader);
		std::sort(peers_inside_preferred_buffer.begin(), peers_inside_preferred_buffer.end(), cmpPeersInsideBufferPreferred);
		std::sort(peers_inside_required_buffer.begin(), peers_inside_required_buffer.end(), cmpPeersInsideBufferRequired);
		std::sort(peers_outside_preferred_buffer.begin(), peers_outside_preferred_buffer.end(), cmpPeersOutsideBufferPreferred);
	}
}