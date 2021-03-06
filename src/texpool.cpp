/*
** texpool.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2013 Jonas Kulla <Nyocurio@gmail.com>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "texpool.h"
#include "exception.h"
#include "sharedstate.h"
#include "glstate.h"

#include <QHash>
#include <QQueue>
#include <QList>
#include <QLinkedList>
#include <QPair>

#include <QDebug>

typedef QPair<uint16_t, uint16_t> Size;
typedef QQueue<TEXFBO> ObjList;

static uint32_t byteCount(Size &s)
{
	return s.first * s.second * 4;
}

struct CacheObject
{
	TEXFBO obj;
	QLinkedList<TEXFBO>::iterator prioIter;

	bool operator==(const CacheObject &o)
	{
		return obj == o.obj;
	}
};

typedef QList<CacheObject> CObjList;

struct TexPoolPrivate
{
	/* Contains all cached TexFBOs, grouped by size */
	QHash<Size, CObjList> poolHash;

	/* Contains all cached TexFBOs, sorted by release time */
	QLinkedList<TEXFBO> priorityQueue;

	/* Maximal allowed cache memory */
	const uint32_t maxMemSize;

	/* Current amound of memory consumed by the cache */
	uint32_t memSize;

	/* Current amount of TexFBOs cached */
	uint16_t objCount;

	/* Has this pool been disabled? */
	bool disabled;

	TexPoolPrivate(uint32_t maxMemSize)
	    : maxMemSize(maxMemSize),
	      memSize(0),
	      objCount(0),
	      disabled(false)
	{}
};

TexPool::TexPool(uint32_t maxMemSize)
{
	p = new TexPoolPrivate(maxMemSize);
}

TexPool::~TexPool()
{
	while (!p->priorityQueue.isEmpty())
	{
		TEXFBO obj = p->priorityQueue.takeFirst();
		TEXFBO::fini(obj);
		--p->objCount;
	}

	Q_ASSERT(p->objCount == 0);

	delete p;
}

TEXFBO TexPool::request(int width, int height)
{
	CacheObject cobj;
	Size size(width, height);

	/* See if we can statisfy request from cache */
	CObjList &bucket = p->poolHash[size];

	if (!bucket.isEmpty())
	{
		/* Found one! */
		cobj = bucket.takeLast();

		p->priorityQueue.erase(cobj.prioIter);

		p->memSize -= byteCount(size);
		--p->objCount;

//		qDebug() << "TexPool: <?+> (" << width << height << ")";

		return cobj.obj;
	}

	int maxSize = glState.caps.maxTexSize;
	if (width > maxSize || height > maxSize)
		throw Exception(Exception::MKXPError,
		                "Texture dimensions [%s, %s] exceed hardware capabilities",
		                QByteArray::number(width), QByteArray::number(height));

	/* Nope, create it instead */
	TEXFBO::init(cobj.obj);
	TEXFBO::allocEmpty(cobj.obj, width, height);
	TEXFBO::linkFBO(cobj.obj);

//	qDebug() << "TexPool: <?-> (" << width << height << ")";

	return cobj.obj;
}

void TexPool::release(TEXFBO &obj)
{
	if (obj.tex == TEX::ID(0) || obj.fbo == FBO::ID(0))
	{
		TEXFBO::fini(obj);
		return;
	}

	if (p->disabled)
	{
		/* If we're disabled, delete without caching */
//		qDebug() << "TexPool: <!#> (" << obj.width << obj.height << ")";
		TEXFBO::fini(obj);
		return;
	}

	Size size(obj.width, obj.height);

	uint32_t newMemSize = p->memSize + byteCount(size);

	/* If caching this object would spill over the allowed memory budget,
	 * delete least used objects until we're good again */
	while (newMemSize > p->maxMemSize)
	{
		if (p->objCount == 0)
			break;

//		qDebug() << "TexPool: <!~> Size:" << p->memSize;

		/* Retrieve object with lowest priority for deletion */
		CacheObject last;
		last.obj = p->priorityQueue.last();
		Size removedSize(last.obj.width, last.obj.height);

		CObjList &bucket = p->poolHash[removedSize];
		Q_ASSERT(bucket.contains(last));
		bucket.removeOne(last);
		p->priorityQueue.removeLast();

		TEXFBO::fini(last.obj);

		uint32_t removedMem = byteCount(removedSize);
		newMemSize -= removedMem;
		p->memSize -= removedMem;
		--p->objCount;

//		qDebug() << "TexPool: <!-> (" << last.obj.tex << last.obj.fbo << ")";
	}

	/* Retain object */
	p->priorityQueue.prepend(obj);
	CacheObject cobj;
	cobj.obj = obj;
	cobj.prioIter = p->priorityQueue.begin();
	CObjList &bucket = p->poolHash[size];
	bucket.append(cobj);

	p->memSize += byteCount(size);
	++p->objCount;

//	qDebug() << "TexPool: <!+> (" << obj.width << obj.height << ") Current size:" << p->memSize;
}

void TexPool::disable()
{
	p->disabled = true;
}


