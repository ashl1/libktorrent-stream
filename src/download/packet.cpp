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
#include "packet.h"
#include <qstring.h>
#include <string.h>
#include <net/socketdevice.h>
#include <util/log.h>
#include <util/bitset.h>
#include <util/functions.h>
#include <diskio/chunk.h>
#include <peer/peer.h>
#include "request.h"

namespace bt
{

	static Uint8* AllocPacket(Uint32 size,Uint8 type)
	{
		Uint8* data = new Uint8[size];
		WriteUint32(data,0,size - 4);
		data[4] = type;
		return data;
	}


	Packet::Packet(Uint8 type) : type(type),data(0),size(0),written(0)
	{
		size = 5;
		data = AllocPacket(size,type);
	}
	
	Packet::Packet(Uint16 port) : type(PORT),data(0),size(0),written(0)
	{
		size = 7;
		data = AllocPacket(size,PORT);
		WriteUint16(data,5,port);
		
	}
	
	Packet::Packet(Uint32 chunk,Uint8 type) : type(type),data(0),size(0),written(0)
	{
		size = 9;
		data = AllocPacket(size,type);
		WriteUint32(data,5,chunk);
	}
	
	Packet::Packet(const BitSet & bs) : type(BITFIELD),data(0),size(0),written(0)
	{
		size = 5 + bs.getNumBytes();
		data = AllocPacket(size,BITFIELD);
		memcpy(data+5,bs.getData(),bs.getNumBytes());
	}
	
	Packet::Packet(const Request & r,Uint8 type) : type(type),data(0),size(0),written(0)
	{
		size = 17;
		data = AllocPacket(size,type);
		WriteUint32(data,5,r.getChunkIndex());
		WriteUint32(data,9,r.getOffset());
		WriteUint32(data,13,r.getLength());
	}
	
	Packet::Packet(Uint32 index,Uint32 begin,Uint32 len,Chunk* ch) : type(PIECE),data(0),size(0),written(0)
	{
		size = 13 + len;
		data = AllocPacket(size,PIECE);
		WriteUint32(data,5,index);
		WriteUint32(data,9,begin);
		ch->readPiece(begin,len,data + 13);
	}

	Packet::Packet(Uint8 ext_id,const QByteArray & ext_data) :  type(EXTENDED),data(0),size(0),written(0)
	{
		size = 6 + ext_data.size();
		data = AllocPacket(size,EXTENDED);
		data[5] = ext_id;
		memcpy(data + 6,ext_data.data(),ext_data.size());
	}

	Packet::~Packet()
	{
		delete [] data;
	}
	
	bool Packet::isPiece(const Request & req) const
	{
		if (data[4] == PIECE)
		{
			if (ReadUint32(data,5) != req.getChunkIndex())
				return false;
			
			if (ReadUint32(data,9) != req.getOffset())
				return false; 
			
			if (size - 13 != req.getLength())
				return false;
			
			return true;
		}
		return false;
	}
	
	Packet* Packet::makeRejectOfPiece()
	{
		if (getType() != PIECE)
			return 0;
		
		Uint32 idx = bt::ReadUint32(data,5);
		Uint32 off = bt::ReadUint32(data,9);
		Uint32 len = size - 13;
		
	//	Out(SYS_CON|LOG_DEBUG) << "Packet::makeRejectOfPiece " << idx << " " << off << " " << len << endl;
		return new Packet(Request(idx,off,len,0),bt::REJECT_REQUEST);
	}
	
	/*
	QString Packet::debugString() const
	{
		if (!data)
			return QString::null;
		
		switch (data[4])
		{
			case CHOKE : return QString("CHOKE %1 %2").arg(hdr_length).arg(data_length);
			case UNCHOKE : return QString("UNCHOKE %1 %2").arg(hdr_length).arg(data_length);
			case INTERESTED : return QString("INTERESTED %1 %2").arg(hdr_length).arg(data_length);
			case NOT_INTERESTED : return QString("NOT_INTERESTED %1 %2").arg(hdr_length).arg(data_length);
			case HAVE : return QString("HAVE %1 %2").arg(hdr_length).arg(data_length);
			case BITFIELD : return QString("BITFIELD %1 %2").arg(hdr_length).arg(data_length);
			case PIECE : return QString("PIECE %1 %2").arg(hdr_length).arg(data_length);
			case REQUEST : return QString("REQUEST %1 %2").arg(hdr_length).arg(data_length);
			case CANCEL : return QString("CANCEL %1 %2").arg(hdr_length).arg(data_length);
			default: return QString("UNKNOWN %1 %2").arg(hdr_length).arg(data_length);
		}
	}
	*/
	bool Packet::isOK() const
	{
		if (!data)
			return false;

		return true;
	}

	int Packet::send(net::SocketDevice* sock, Uint32 max_to_send)
	{
		Uint32 bw = size - written;
		if (!bw) // nothing to write
			return 0;
		
		if (bw > max_to_send && max_to_send > 0)
			bw = max_to_send;
		int ret = sock->send(data + written, bw);
		if (ret > 0)
			written += ret;
		return ret;
	}

}
