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

#include <QtTest>
#include <QObject>
#include <time.h>
#include <util/log.h>
#include <peer/superseeder.h>
#include <interfaces/peerinterface.h>
#include <peer/chunkcounter.h>

using namespace bt;

#define NUM_CHUNKS 10
#define INVALID_CHUNK 0xFFFFFFFF

static int peer_cnt = 0;

class DummyPeer : public PeerInterface
{
public:
	DummyPeer() : PeerInterface(PeerID(),NUM_CHUNKS)
	{
		QString name = QString("%1").arg(peer_cnt++);
		if (name.size() < 20)
			name = name.leftJustified(20,' ');
		peer_id = PeerID(name.toAscii());
		allowed_chunk = INVALID_CHUNK;
		allow_called = false;
	}
	
	virtual ~DummyPeer()
	{}
	
	void reset()
	{
		chunks_availability.setAll(false);
		allowed_chunk = INVALID_CHUNK;
	}
	
	void have(Uint32 chunk)
	{
		chunks_availability.set(chunk,true);
	}
	
	void haveAll()
	{
		chunks_availability.setAll(true);
	}
	
	virtual void kill() 
	{
		killed = true;
	}
	
	virtual Uint32 averageDownloadSpeed() const {return 0;}
	
	virtual void chunkAllowed(Uint32 chunk)
	{
		allowed_chunk = chunk;
		allow_called = true;
	}
	
	virtual void handlePacket(const bt::Uint8* packet, Uint32 size)
	{
		Q_UNUSED(packet);
		Q_UNUSED(size);
	}
	
	Uint32 allowed_chunk;
	bool allow_called;
};


class SuperSeedTest : public QObject
{
	Q_OBJECT
public:
	SuperSeedTest(QObject* parent = 0) : QObject(parent)
	{}
	
	bool allowCalled(PeerInterface* peer) 
	{
		return ((DummyPeer*)peer)->allow_called && ((DummyPeer*)peer)->allowed_chunk != INVALID_CHUNK;
	}
	
private Q_SLOTS:
	void initTestCase()
	{
		bt::InitLog("superseedtest.log");
		allow_called = false;
		qsrand(time(0));
	}
	
	void cleanupTestCase()
	{
	}
	
	void testSuperSeeding()
	{
		Out(SYS_GEN|LOG_DEBUG) << "testSuperSeeding" << endl;
		SuperSeeder ss(NUM_CHUNKS);
		// First test, all tree should get a chunk
		for (int i = 0;i < 3;i++)
		{
			DummyPeer* p = &peer[i];
			ss.peerAdded(p);
			QVERIFY(allowCalled(p));
			p->allow_called = false;
		}
		
		ss.dump();
		
		DummyPeer* uploader = &peer[0];
		DummyPeer* downloader = &peer[1];
		DummyPeer* next = &peer[2];
		// Simulate normal superseeding operation
		for (int i = 0;i < 4;i++)
		{
			Out(SYS_GEN|LOG_DEBUG) << "======================================" << endl;
			Uint32 prev_chunk = uploader->allowed_chunk;
			// Now simulate b downloaded the first chunk from a
			Uint32 chunk = prev_chunk;
			
			Out(SYS_GEN|LOG_DEBUG) << "uploader = " << uploader->getPeerID().toString() << endl;
			Out(SYS_GEN|LOG_DEBUG) << "downloader = " << downloader->getPeerID().toString() << endl;
			Out(SYS_GEN|LOG_DEBUG) << "chunk = " << chunk << endl;
			
			if (uploader->allowed_chunk == downloader->allowed_chunk)
			{
				uploader->have(chunk);
				downloader->have(chunk);
				ss.have(downloader,chunk);
				Out(SYS_GEN|LOG_DEBUG) << "prev_chunk = " << chunk << ", uploader->allowed_chunk = " << uploader->allowed_chunk << endl;
				QVERIFY(uploader->allowed_chunk != prev_chunk && uploader->allowed_chunk != INVALID_CHUNK);
				QVERIFY(allowCalled(uploader));
				uploader->allow_called = false;
				QVERIFY(!allowCalled(downloader));
			}
			else
			{
				uploader->have(chunk);
				downloader->have(chunk);
				ss.have(downloader,chunk);
				Out(SYS_GEN|LOG_DEBUG) << "prev_chunk = " << chunk << ", uploader->allowed_chunk = " << uploader->allowed_chunk << endl;
				QVERIFY(uploader->allowed_chunk != prev_chunk && uploader->allowed_chunk != INVALID_CHUNK);
				QVERIFY(allowCalled(uploader)); // allow should be called again on the uploader
				uploader->allow_called = false;
			}
			
			// Cycle through peers
			DummyPeer* n = uploader;
			uploader = downloader;
			downloader = next;
			next = n;
			
			ss.dump();
		}
	}
	#if 0
	void testSeed()
	{
		Out(SYS_GEN|LOG_DEBUG) << "testSeed" << endl;
		SuperSeeder ss(NUM_CHUNKS,this);
		
		for (int i = 0;i < 4;i++)
			peer[i].reset();
		
		// First test, all tree should get a chunk
		for (int i = 0;i < 3;i++)
		{
			DummyPeer* p = &peer[i];
			ss.peerAdded(p);
			QVERIFY(allowCalled(p));
			QVERIFY(p->allowed_chunk != INVALID_CHUNK);
			target.clear();
		}
		
		ss.dump();
		
		// Now simulate a seed sending a have all message
		peer[3].haveAll();
		ss.haveAll(&peer[3]);
		QVERIFY(allow_called == false);
		target.clear();
		
		// Continue superseeding
		DummyPeer* uploader = &peer[0];
		DummyPeer* downloader = &peer[1];
		DummyPeer* next = &peer[2];
		for (int i = 0;i < 4;i++)
		{
			Out(SYS_GEN|LOG_DEBUG) << "======================================" << endl;
			Uint32 prev_chunk = uploader->allowed_chunk;
			// Now simulate b downloaded the first chunk from a
			Uint32 chunk = prev_chunk;
			
			Out(SYS_GEN|LOG_DEBUG) << "uploader = " << uploader->getPeerID().toString() << endl;
			Out(SYS_GEN|LOG_DEBUG) << "downloader = " << downloader->getPeerID().toString() << endl;
			Out(SYS_GEN|LOG_DEBUG) << "chunk = " << chunk << endl;
			
			uploader->have(chunk);
			downloader->have(chunk);
			ss.have(downloader,chunk);
			QVERIFY(allowCalled(uploader)); // allow should be called again on the uploader
			
			Out(SYS_GEN|LOG_DEBUG) << "prev_chunk = " << chunk << ", uploader->allowed_chunk = " << uploader->allowed_chunk << endl;
			QVERIFY(uploader->allowed_chunk != prev_chunk && uploader->allowed_chunk != INVALID_CHUNK);
			target.clear();
			
			// Cycle through peers
			DummyPeer* n = uploader;
			uploader = downloader;
			downloader = next;
			next = n;
			
			ss.dump();
		}
		
	}
#endif
private:
	DummyPeer peer[4];
	bool allow_called;
};

QTEST_MAIN(SuperSeedTest)

#include "superseedtest.moc"

