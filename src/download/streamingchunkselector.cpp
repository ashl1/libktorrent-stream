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

#include "streamingchunkselector.h"

#include <diskio/chunkmanager.h>
#include <interfaces/piecedownloader.h>
#include <util/log.h>
#include "downloader.h"
#include "managerofstream.h"

namespace bt
{
	const Uint32 INVALID_CHUNK = 0xFFFFFFFF;
	const Uint32 CRITICAL_WINDOW_SIZE = 2 * 1024 * 1024;
	
	StreamingChunkSelector::StreamingChunkSelector()
		: range_start(0), range_end(0), cursor(0), critical_window_size(1)
	{}
	
	StreamingChunkSelector::~StreamingChunkSelector()
	{

	}
	
	void StreamingChunkSelector::init(ChunkManager* cman, Downloader* downer, PeerManager* pman)
	{
		bt::ChunkSelector::init(cman, downer, pman);
		range_end = cman->getNumChunks() - 1;
		critical_window_size = CRITICAL_WINDOW_SIZE / cman->getTorrent().getChunkSize();
		if (critical_window_size == 0)
			critical_window_size = 1;
		
		preview_chunks.clear();
		for (Uint32 i = 0;i <= range_end;i++)
			if (cman->getChunk(i)->getPriority() == bt::PREVIEW_PRIORITY)
				preview_chunks.insert(i);
			
		manager_of_stream = new ManagerOfStream(this, downer);
		manager_of_stream->Init();
	}

	
	void StreamingChunkSelector::setCursor(Uint32 chunk)
	{
		if (cursor != chunk)
		{
			cursor = chunk;
			updateRange();
			emit anotherChunkAsked(chunk);
		}
	}
	
	void StreamingChunkSelector::setSequentialRange(Uint32 from, Uint32 to)
	{
		range_start = from;
		range_end = to;
		cursor = from;
		initRange();
	}
	
	void StreamingChunkSelector::initRange()
	{
		// Initialize the range
		const BitSet & bs = cman->getBitSet();
		range.clear();
		for (Uint32 i = cursor;i <= range_end;i++)
		{
			if (!bs.get(i))
				range.push_back(i);
		}
	}

	
	void StreamingChunkSelector::updateRange()
	{
		const BitSet & bs = cman->getBitSet();
		if (range.empty() || cursor < range.front())
			initRange(); // Reinitialize range
		
		std::list<Uint32>::iterator itr = range.begin();
		while (itr != range.end())
		{
			Uint32 i = *itr;
			// if we have the chunk or it falls before the cursor remove it from the list
			if (bs.get(i) || i < cursor)
			{
				std::list<Uint32>::iterator tmp = itr;
				itr++;
				range.erase(tmp);
			}
			else
				itr++;
		}
	}
	
	bool StreamingChunkSelector::selectFromPreview(PieceDownloader* pd, Uint32& chunk)
	{
		const BitSet & bs = cman->getBitSet();
		
		std::list<Uint32> candidates;
		// Gather all preview chunks which lie inside the range
		// and are not yet downloaded
		std::set<Uint32>::iterator i = preview_chunks.begin();
		while (i != preview_chunks.end())
		{
			if (bs.get(*i))
			{
				std::set<Uint32>::iterator j = i;
				i++;
				preview_chunks.erase(j);
			}
			else if (pd->hasChunk(*i) && *i >= range_start && *i <= range_end)
			{
				candidates.push_back(*i);
				i++;
			}
			else
				i++;
		}
		
		if (!candidates.empty())
		{
			chunk = leastPeers(candidates, INVALID_CHUNK, 3);
			return chunk != INVALID_CHUNK;
		}
		else
			return false;
	}

	bool StreamingChunkSelector::select(bt::PieceDownloader* pd, bt::Uint32& chunk)
	{
		Out(SYS_DIO|LOG_DEBUG) << "\tStreamingChunkSelector::select - will from buffer required" << endl;
		if (manager_of_stream->selectChunkFromBufferRequiredNotMeetRequirement(pd, chunk))
			return true;
		Out(SYS_DIO|LOG_DEBUG) << "\tStreamingChunkSelector::select - will from buffer preferred" << endl;
		if (manager_of_stream->selectChunkFromBufferPreferred(pd, chunk))
			return true;
		
		Out(SYS_DIO|LOG_DEBUG) << "\tStreamingChunkSelector::select - will another algo" << endl;
		
// 		Select by algo:
// 		- normal distribution (calculates each seeking)
// 		- binomial distribution (like Cantor set)
// 		- periodic normal distribution with period 5-10 (calculated once for torrent)
// 		- statistical info based on user seekings in the past grouped by content type (video, audio) (calculate once for torrent)
		
		const BitSet & bs = cman->getBitSet();
		for (Uint32 chunk_index = range_end; chunk_index > cursor; --chunk_index)
		{
			/// TODO: Determne the reason, why pd->hasChunk return fale for all chunks
			Out(SYS_DIO|LOG_DEBUG) << "\tStreamingChunkSelector::select\tchunk( " << chunk_index << " )" << endl <<
				"\t\tdownloaded? " << bs.get(chunk_index) << endl <<
				"\t\tPieceDownloader has chunk? " << pd->hasChunk(chunk_index) << endl;
			if (!bs.get(chunk_index) && pd->hasChunk(chunk_index)) {
				Out(SYS_DIO|LOG_DEBUG) << "\tStreamingChunkSelector::select - finish 1" << endl;
				return true;
			}
		}
		Out(SYS_DIO|LOG_DEBUG) << "\tStreamingChunkSelector::select - finish 2" << endl;
		return false;
	}

// 	bool StreamingChunkSelector::select(bt::PieceDownloader* pd, bt::Uint32& chunk)
// 	{
// 		// Always give precendence to preview chunks
// 		if (selectFromPreview(pd, chunk))
// 			return true;
// 		
// 		const BitSet & bs = cman->getBitSet();
// 		Uint32 critical_chunk = INVALID_CHUNK;
// 		Uint32 critical_chunk_downloaders = INVALID_CHUNK;
// 		Uint32 non_critical_chunk = INVALID_CHUNK;
// 		
// 		std::list<Uint32>::iterator itr = range.begin();
// 		while (itr != range.end())
// 		{
// 			Uint32 chunk_index = *itr;
// 			const Chunk* chunk = cman->getChunk(chunk_index);
// 			
// 			// if we have the chunk remove it from the list
// 			if (bs.get(chunk_index))
// 			{
// 				std::list<Uint32>::iterator tmp = itr;
// 				itr++;
// 				range.erase(tmp);
// 			}
// 			else if (pd->hasChunk(chunk_index) && !chunk->isExcluded() && !chunk->isExcludedForDownloading())
// 			{
// 				if (chunk_index < cursor + critical_window_size)
// 				{
// 					// Attempt to find the critical chunk with the least downloaders
// 					Uint32 nd = downer->numDownloadersForChunk(chunk_index);
// 					if (critical_chunk == INVALID_CHUNK || nd < critical_chunk_downloaders)
// 					{
// 						critical_chunk = chunk_index;
// 						critical_chunk_downloaders = nd;
// 					}
// 				}
// 				else if (!downer->isChunkDownloading(chunk_index))
// 				{
// 					// Stop at the first non critical chunk
// 					non_critical_chunk = chunk_index;
// 					break;
// 				}
// 				
// 				itr++;
// 			}
// 			else
// 				itr++;
// 		}
// 		
// 		if (critical_chunk != INVALID_CHUNK)
// 		{
// 			chunk = critical_chunk;
// 			return true;
// 		}
// 		else if (non_critical_chunk != INVALID_CHUNK)
// 		{
// 			chunk = non_critical_chunk;
// 			return true;
// 		}
// 		else
// 		{
// 			// If we haven't found one, use the default selection algorithm
// 			return bt::ChunkSelector::select(pd, chunk);
// 		}
// 	}

	void StreamingChunkSelector::dataChecked(const bt::BitSet& ok_chunks, Uint32 from, Uint32 to)
	{
		bt::ChunkSelector::dataChecked(ok_chunks, from, to);
		updateRange();
	}

	void StreamingChunkSelector::reincluded(bt::Uint32 from, bt::Uint32 to)
	{
		bt::ChunkSelector::reincluded(from, to);
		initRange();
		
		for (Uint32 chunk = from;chunk <= to;chunk++)
			if (cman->getChunk(chunk)->getPriority() == bt::PREVIEW_PRIORITY)
				preview_chunks.insert(chunk);
	}

	void StreamingChunkSelector::reinsert(bt::Uint32 chunk)
	{
		if (cman->getChunk(chunk)->getPriority() == bt::PREVIEW_PRIORITY)
			preview_chunks.insert(chunk);
		
		bt::ChunkSelector::reinsert(chunk);
		if (chunk >= cursor && chunk <= range_end)
		{
			std::list<Uint32>::iterator itr = range.begin();
			while (itr != range.end())
			{
				Uint32 i = *itr;
				if (i == chunk)
				{
					return;
				}
				if (i > chunk)
				{
					range.insert(itr,chunk);
					return;
				}
				
				itr++;
			}
			
			// Not returned yet, so must be the last chunk in the range
			range.push_back(chunk);
		}
	}

	bool StreamingChunkSelector::selectRange(bt::Uint32& from, bt::Uint32& to, bt::Uint32 max_len)
	{
		return bt::ChunkSelector::selectRange(from, to, max_len);
	}
}
