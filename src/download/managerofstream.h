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

/**
 * \page ManagerOfStream The documentation of the streaming mechanism
 * \tableofcontents
 * 
 * \section Terminology Terminology
 * \subsection ManagerOfStream_Terminology_RequiredCondition The condition the chunk should require
 * Text0
 * \subsection ManagerOfStream_Terminology_BufferRequired The Buffer Required
 * Text1
 * \subsection ManagerOfStream_Terminology_BufferPreferred The Buffer Preferred
 * Text2
 * 
 */

#ifndef BTMANAGEROFSTREAM_H
#define BTMANAGEROFSTREAM_H

#include <QObject>
#include <QList>
#include <util/constants.h>

class QTimer;

namespace bt {

	class ChunkDownload;
	class Downloader;
	class PieceDownloader;
	
	class ManagerOfStream;
	
	/**
	 * Monitor chunk's downloading in the buffers for playing. In case 
	 *  the time the chunk will be required is less then the time 
	 *  the chunk will have been downloaded reassign peers to the required chunks
	 *  (to fill the buffer)
	 * 
	 * @author Alexey Shildyakov
	 */
	class ManagerOfStream: public QObject
	{
		Q_OBJECT
	public:
		ManagerOfStream(QObject* parent, Downloader* downloader);
		
		void Init();
		
		/**
		 * Determine the chunk from \ref ManagerOfStream_Terminology_BufferPreferred to download at now
		 *  the peer has (we are able to download that chunk)
		 * @param piece_downloader[in] The PieceDownloader to select chunk for
		 * @param chunk_index[out] The index of the chunk recommend to download for piece_downloader
		 * @return True, if have found the chunk; false otherwise.
		 */
		bool selectChunkFromBufferPreferred(const PieceDownloader* piece_downloader, Uint32& chunk_index);
		
		/**
		 * Determine the chunk with minimum index from \ref ManagerOfStream_Terminology_BufferRequired
		 *  is not meet \ref ManagerOfStream_Terminology_RequiredCondition and specified peer has
		 * @param piece_downloader[in] The PieceDownloader to select chunk for
		 * @param chunk_index[out] The index of the chunk recommend to download for piece_downloader
		 * @return True, if have found the chunk; false otherwise.
		 */
		bool selectChunkFromBufferRequiredNotMeetRequirement(const PieceDownloader* piece_downloader, Uint32& chunk_index);
		
	public slots:
		void checkAndMakeBufferRequiredBeInTime();
		
		void chunkAsked(Uint32 chunk_index);
		
		void chunkDownloaded(Uint32 chunk_index);
		
	private:
		class Chunk;
		
		/**
		 * Delete peers from the peers_sorted are downloading the chunk with index less or equal to chunk_index
		 * @param peers_sorted[in,out] The list of peers have been already sorted by index of chunk descending
		 * @param chunk_index[in] The index of the chunk, the peers are downloading the chunks with index less or equal to will be deleted from peers_sorted
		 */
		static void deleteFromListPeersDownloadedMorePrioritizedChunks(QList<PieceDownloader*> &peers_sorted, Uint32 chunk_index);
		
		/**
		 * Move (copy and erase original) the PieceDownloader from the peers_list_move_from into peers_list_move_into which
		 *  are downloading the chunks in the specified chunk range
		 */
		static void movePeersDownloadingChunksInRange(QList<PieceDownloader*>& peers_list_move_from, QList<PieceDownloader*>& peers_list_move_into, Uint32 chunk_index_range_starts_from, Uint32 chunk_index_range_finish_to);
		
		/**
		 * Determine the chunk with minimum index from \ref ManagerOfStream_Terminology_BufferRequired
		 *  is not meet \ref ManagerOfStream_Terminology_RequiredCondition
		 * @param chunk_index[out] The index of the chunk recommend to download for piece_downloader
		 * @return True, if have found the chunk; false otherwise.
		 */
		bool getChunkFromBufferRequiredNotMeetRequirement(Uint32& chunk_index);
		
		Uint32 getCurrentPlayedChunkIndex() const;
		
		Uint32 getSizeOfBufferPreferred() const;
		
		/**
		 * Determine and return the size of \ref ManagerOfStream_Terminology_BufferRequired
		 *  as ceiling number of chunks required to play STREAMING_BUFFER_IN_SECONDS seconds of media.
		 *  This is based on average speed of requesting the data from the stream
		 * @return The number of chunks as the size of \ref ManagerOfStream_Terminology_BufferRequired
		 */
		Uint32 getSizeOfBufferRequired() const;

		/**
		 * In case the last time chunk was played for is less than current chunk have already played, return 0
		 */
		TimeStamp getTimeCurrentChunkFinishPlaying() const;
		
		/**
		 * Try to reassign peers to specified in chunk_reassign_to to meet \ref ManagerOfStream_Terminology_RequiredCondition:
		 * cancel all downloadings in the chunk and assign if the peer has the chunk.
		 * Remove reassigned peers from peers_sorted.
		 * @param peers_sorted[in,out] The list of peers have been already sorted to reassign the peers.
		 *  The peers in the list might not has the specified chunk
		 * @param chunk_reassign_to[in] The index of the chunk the peers will be reassigned to
		 * @return True, if the chunk will meet \ref ManagerOfStream_Terminology_RequiredCondition
		 *  after reassigning. False, if the \ref ManagerOfStream_Terminology_RequiredCondition won't be
		 *  applied for the chunk_reassign_to after all reassigning from peers_sorted
		 */
		bool tryReassignPeers(QList<PieceDownloader*> &peers_sorted, Uint32 chunk_reassign_to);
		
		/**
		 * Update buffer_preferred_starts_from, buffer_preferred_finish_to and buffer_required_starts_from, buffer_required_finish_to
		 *  based on buffers length and current playing chunk
		 */
		void updateBuffersRangeIndexes();
		
		/**
		 * Update peers are now downloading in peers_outside_preferred_buffer, peers_inside_preferred_buffer and
		 * peers_inside_required_buffer in according it sort requirements
		 */
		void updateSortedPeersList();
		
	private:
		/**
		 * The index of the chunk the \ref ManagerOfStream_Terminology_BufferPreferred starts from.
		 * Determined based on current playing chunk and buffer size
		 */
		Uint32 buffer_preferred_finish_to;
		/**
		 * The index of the chunk the \ref ManagerOfStream_Terminology_BufferPreferred finish to (included).
		 * Determined based on current playing chunk and buffer size
		 */
		Uint32 buffer_preferred_starts_from;
		/**
		 * The index of the chunk the \ref ManagerOfStream_Terminology_BufferRequired starts from.
		 * Determined based on current playing chunk and buffer size
		 */
		Uint32 buffer_required_finish_to;
		/**
		 * The index of the chunk the \ref ManagerOfStream_Terminology_BufferRequired finish to (included).
		 * Determined based on current playing chunk and buffer size
		 */
		Uint32 buffer_required_starts_from;
		
		Downloader* downloader;
		
		Uint32 index_chunk_last_time_asked;
		
		/// in milliseconds
		TimeStamp last_time_new_chunk_was_asked;
		
		/**
		 * The list of peers are now downloading only chunks inside \ref ManagerOfStream_Terminology_BufferPreferred
		 *  (but outside \ref ManagerOfStream_Terminology_BufferRequired) sorted by downloading rate descending
		 */ 
		QList<PieceDownloader*> peers_inside_preferred_buffer;
		
		/**
		 * The list of peers are now downloading only chunks inside \ref ManagerOfStream_Terminology_BufferRequired
		 * sorted by chunk index descending and (as second ordering) by downloading rate descending.
		 * Use only one-level list (sorted list grouped by chunk index).
		 */
		QList<PieceDownloader*> peers_inside_required_buffer;
		
		/**
		 * The list of peers are now downloading only chunks outside \ref ManagerOfStream_Terminology_BufferPreferred
		 *  sorted by downloading rate descending
		 */ 
		QList<PieceDownloader*> peers_outside_preferred_buffer;
		
		/**
		 * in milliseconds
		 */
		TimeStamp time_last_chunk_played_for;		
		
		::QTimer* timer;
	};
}

#endif