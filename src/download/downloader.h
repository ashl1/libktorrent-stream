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
#ifndef BTDOWNLOADER_H
#define BTDOWNLOADER_H

#include <qobject.h>
#include <util/ptrmap.h>
#include <util/constants.h>
#include <ktorrent_export.h>
#include "download/webseed.h"
#include "download/chunkdownload.h"
#include "peer/peermanager.h"

class KUrl;

namespace bt
{
	class BitSet;
	class Torrent;
	class ChunkManager;
	class PeerManager;
	class Peer;
	class Chunk;
	class Piece;
	class ChunkSelectorInterface;
	class PieceDownloader;
	class MonitorInterface;
	class WebSeedChunkDownload;

	typedef PtrMap<Uint32,ChunkDownload>::iterator CurChunkItr;
	typedef PtrMap<Uint32,ChunkDownload>::const_iterator CurChunkConstItr;
	
	#define CURRENT_CHUNK_MAGIC 0xABCDEF00
	
	struct CurrentChunksHeader
	{
		Uint32 magic; // CURRENT_CHUNK_MAGIC
		Uint32 major;
		Uint32 minor;
		Uint32 num_chunks;
	};
	
	/**
	 * @author Joris Guisson
	 * @brief Manages the downloading
	 *
	 * This class manages the downloading of the file. It should
	 * regurarly be updated.
	*/
	class KTORRENT_EXPORT Downloader : public QObject,public PieceHandler
	{
		Q_OBJECT
		
	public:
		/**
		 * Constructor.
		 * @param tor The Torrent
		 * @param pman The PeerManager
		 * @param cman The ChunkManager
		 */
		Downloader(Torrent & tor,PeerManager & pman,ChunkManager & cman);
		virtual ~Downloader();
		
		/**
		 * Set the group ID's of the webseed (for speed limits)
		 * @param up Upload group id
		 * @param down Download group id
		 */
		void setGroupIDs(Uint32 up,Uint32 down);

		/// Get the number of webseeds
		Uint32 getNumWebSeeds() const {return webseeds.count();}
		
		/// Get a webseed
		const WebSeed* getWebSeed(Uint32 i) const {return i < (Uint32)webseeds.count() ? webseeds[i] : 0;}
		
		/// Get a webseed
		WebSeed* getWebSeed(Uint32 i) {return i < (Uint32)webseeds.count() ? webseeds[i] : 0;}
		
		/// Add a webseed
		WebSeed* addWebSeed(const KUrl & url);
		
		/// Remove a webseed
		bool removeWebSeed(const KUrl & url);
		
		/// Remove all webseeds
		void removeAllWebSeeds();
		
		/// Save the user created webseeds
		void saveWebSeeds(const QString & file);
		
		/// Add the user created webseeds
		void loadWebSeeds(const QString & file);
		
		/// Get the number of bytes we have downloaded
		Uint64 bytesDownloaded() const {return bytes_downloaded + curr_chunks_downloaded;}

		/// Get the current dowload rate
		Uint32 downloadRate() const;

		/// Get the number of chunks we are dowloading
		Uint32 numActiveDownloads() const {return downloading_chunks.count() + active_webseed_downloads;}

		/// See if the download is finished.
		bool isFinished() const;
		
		/**
		 * Clear all downloads. Deletes all active downloads.
		 */
		void clearDownloads();
		
		/**
		 * Pause the download
		 */
		void pause();
		
		CurChunkConstItr beginDownloads() const {return downloading_chunks.begin();}
		CurChunkConstItr endDownloads() const {return downloading_chunks.end();}
				
		/**
		 * See if we are downloading a Chunk
		 * @param chunk ID of Chunk
		 * @return true if we are, false if not
		 */
		bool isChunkDownloading(Uint32 chunk) const;
		
		/**
		 * Can we download a chunk from a webseed.
		 * @param chunk ID of Chunk
		 * @return true if we can
		*/
		bool canDownloadFromWebSeed(Uint32 chunk) const;

		/**
		 * Get the number of downloaders assigned to a chunk
		 * @param chunk ID of Chunk
		 * @return the number of downloaders for that chunk
		 */
		Uint32 numDownloadersForChunk(Uint32 chunk) const;
		
		/// Are we in endgame mode
		bool endgameMode() const;
		
		/**
		 * Save the current downloads.
		 * @param file The file to save to
		 */
		void saveDownloads(const QString & file);

		/**
		 * Load the current downloads.
		 * @param file The file to load from
		 */
		void loadDownloads(const QString & file);

		/**
		 * Get have been downloading ChunkDownload (or downloading now) for the specified chunk
		 * @return Pointer to the ChunkDownload with specified chunk_index, or
		 *  0 in case the chunk have never been downloaded or already downloaded
		 */
		ChunkDownload* getChunkDownload(Uint32 chunk_index);

		/**
		 * Get have been downloading ChunkDownload (or downloading now) for the specified chunk
		 * @return Pointer to the ChunkDownload with specified chunk_index, or
		 *  0 in case the chunk have never been downloaded or already downloaded
		 */
		const ChunkDownload* getChunkDownload(Uint32 chunk_index) const;
		
		const ChunkManager* getChunkManager() const;
		
		ChunkSelectorInterface* getChunkSelector();
		
		/**
		 * Get the number of bytes already downloaded in the current_chunks file.
		 * @param file The path of the current_chunks file
		 * @return The bytes already downloading
		 */
		Uint32 getDownloadedBytesOfCurrentChunksFile(const QString & file);
		
		Uint32 getMinimalIndexDownloadingChunk(const PieceDownloader* piece_downloader) const;
		
		QList<PieceDownloader*> getPieceDownloaders() const;
		
		/**
		 * A corrupted chunk has been detected, make sure we redownload it.
		 * @param chunk The chunk
		 */
		void corrupted(Uint32 chunk);
		
		/// Set the ChunkSelector, 0 means KT will reset to the default selector
		void setChunkSelector(ChunkSelectorInterface* csel);
		
		/**
		 * We got a new connection.
		 * @param peer The PieceDownloader
		 */
		void addPieceDownloader(PieceDownloader* peer);
		
		/**
		 * Remove a piece downloader.
		 * @param peer The PieceDownloader
		 */
		void removePieceDownloader(PieceDownloader* peer);
		
		/// Enable or disable the use of webseeds
		static void setUseWebSeeds(bool on);
		
		void stopAndReassignPieceDownloader(PieceDownloader* piece_downloader, Uint32 chunk_index);
	public slots:
		/**
		 * Update the downloader.
		 */
		void update();
		
		/**
		 * Set the TorrentMonitor.
		 * @param tmo 
		 */
		void setMonitor(MonitorInterface* tmo);
		
		/**
		 * Data has been checked, and these chunks are OK.
		 * @param ok_chunks The ok_chunks
		 * @param from First chunk of the check
		 * @param to Last chunk of the check
		 */
		void dataChecked(const BitSet & ok_chunks, bt::Uint32 from, bt::Uint32 to);
		
		/**
		 * Recalculate the number of bytes downloaded.
		 */
		void recalcDownloaded();
		
	private slots:
		virtual void pieceReceived(const bt::Piece & p);
		bool finished(ChunkDownload* c);
		
		/**
		 * Kill all ChunkDownload's which have been excluded.
		 * @param from First chunk of range
		 * @param to Last chunk of range
		 */
		void onExcluded(Uint32 from,Uint32 to);
		
		/**
		 * Make sure chunk selector is back OK, when chunks are included back again.
		 * @param from First chunk
		 * @param to Last chunk
		 */
		void onIncluded(Uint32 from,Uint32 to);
		
		/**
		 * A WebSeed has finished a Chunk
		 * @param c The chunk
		 */
		void onChunkReady(Chunk* c);
		
		void chunkDownloadStarted(WebSeedChunkDownload* cd,Uint32 chunk);
		void chunkDownloadFinished(WebSeedChunkDownload* cd,Uint32 chunk);
		
	signals:
		/**
		 * An error occurred while we we're writing or reading from disk.
		 * @param msg Message
		 */
		void ioError(const QString & msg);
		
		/**
		 * Emitted when a chunk has been downloaded.
		 * @param chunk The chunk
		 */
		void chunkDownloaded(Uint32 chunk);
		
	private:
		bool assignPieceDownloaderToChunk(PieceDownloader* piece_downloader, Uint32 chunk_index);
		
		bool downloadFrom(PieceDownloader* pd);
		void downloadFrom(WebSeed* ws);
		void normalUpdate();
		ChunkDownload* selectWorst(PieceDownloader* pd);
		
	private:
		Torrent & tor;
		PeerManager & pman;
		ChunkManager & cman;
		Uint64 bytes_downloaded;
		Uint64 curr_chunks_downloaded;
		Uint64 total_unnecessary_data;
		Uint64 unnecessary_data_chunk_finished;
		/**
		 * @brief The list of current downloading ChunkDownload's or selected to download
		 * The list of current downloading ChunkDownload's or selected to download.
		 * The ChunkDownload may not downloading in specified time (for example, peer close
		 * connections) but it should be downloaded because it's part has already been downloaded
		 */
		PtrMap<Uint32,ChunkDownload> downloading_chunks;
		QList<PieceDownloader*> piece_downloaders;
		MonitorInterface* tmon;
		ChunkSelectorInterface* chunk_selector;
		QList<WebSeed*> webseeds;
		PtrMap<Uint32,WebSeed> webseeds_chunks;
		Uint32 active_webseed_downloads;
		bool webseeds_on;
		Uint32 webseed_range_size;
		bool webseed_endgame_mode;
		
		static bool use_webseeds;
	};
	


}

#endif
