/***************************************************************************
 *   Copyright (C) 2008 by Joris Guisson and Ivan Vasic                    *
 *   joris.guisson@gmail.com                                               *
 *   ivasic@gmail.com                                                      *
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
#ifndef BTCHUNKSELECTORINTERFACE_H
#define BTCHUNKSELECTORINTERFACE_H

#include <QObject>
#include <util/constants.h>
#include <ktorrent_export.h>

namespace bt
{
	class BitSet;
	class Downloader;
	class PeerManager;
	class ChunkManager;
	class PieceDownloader;

	/**
	* @author Joris Guisson
	* 
	* Interface class for selecting chunks to download.
	*/
	class KTORRENT_EXPORT ChunkSelectorInterface : public QObject
	{
		Q_OBJECT
	public:
		ChunkSelectorInterface();
		virtual ~ChunkSelectorInterface();
		
		/**
		 * Initialize the chunk selector, will be called automatically when
		 * the ChunkSelector is set.
		 * @param cman The ChunkManager
		 * @param downer The Downloader
		 * @param pman The PeerManager
		 */
		virtual void init(ChunkManager* cman,Downloader* downer,PeerManager* pman);
		
		/**
		 * Select which chunk to download for a PieceDownloader.
		 * @param pd The PieceDownloader
		 * @param chunk[out] Index of chunk gets stored here
		 * @return true upon succes, false otherwise
		 */
		virtual bool select(PieceDownloader* pd,Uint32 & chunk) = 0;
		
		/**
		 * Select a range of chunks to download from a webseeder.
		 * @param from First chunk of the range
		 * @param to Last chunk of the range
		 * @param max_len Maximum length of range
		 * @return true if everything is OK
		 */
		virtual bool selectRange(Uint32 & from,Uint32 & to,Uint32 max_len);
		
		/**
		 * Data has been checked, and these chunks are OK.
		 * @param ok_chunks The ok_chunks
		 * @param from First chunk of check
		 * @param to Last chunk of check
		 */
		virtual void dataChecked(const BitSet & ok_chunks, Uint32 from, Uint32 to) = 0;
		
		/**
		 * A range of chunks has been reincluded. This is called when a user 
		 * reselects a file for download.
		 * @param from The first chunk
		 * @param to The last chunk
		 */
		virtual void reincluded(Uint32 from, Uint32 to)= 0;
		
		/**
		 * Reinsert a chunk. This is called when a chunk is corrupted or downloading it failed (hash doesn't match)
		 * The selector should make sure that this is added again
		 * @param chunk The chunk
		 */
		virtual void reinsert(Uint32 chunk) = 0;
		
	protected:
		ChunkManager* cman;
		Downloader* downer;
		PeerManager* pman;
	};
}

#endif
