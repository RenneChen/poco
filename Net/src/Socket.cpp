//
// Socket.cpp
//
// $Id: //poco/1.3/Net/src/Socket.cpp#6 $
//
// Library: Net
// Package: Sockets
// Module:  Socket
//
// Copyright (c) 2005-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
// 
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//


#include "Poco/Net/Socket.h"
#include "Poco/Net/StreamSocketImpl.h"
#include "Poco/Timestamp.h"
#include <algorithm>
#include <string.h> // FD_SET needs memset on some platforms, so we can't use <cstring>
#if defined(POCO_HAVE_FD_EPOLL)
#include <sys/epoll.h>
#endif


namespace Poco {
namespace Net {


Socket::Socket():
	_pImpl(new StreamSocketImpl)
{
}


Socket::Socket(SocketImpl* pImpl):
	_pImpl(pImpl)
{
	poco_check_ptr (_pImpl);
}


Socket::Socket(const Socket& socket):
	_pImpl(socket._pImpl)
{
	poco_check_ptr (_pImpl);

	_pImpl->duplicate();
}

	
Socket& Socket::operator = (const Socket& socket)
{
	if (&socket != this)
	{
		if (_pImpl) _pImpl->release();
		_pImpl = socket._pImpl;
		if (_pImpl) _pImpl->duplicate();
	}
	return *this;
}


Socket::~Socket()
{
	_pImpl->release();
}


int Socket::select(SocketList& readList, SocketList& writeList, SocketList& exceptList, const Poco::Timespan& timeout)
{
#if defined(POCO_HAVE_FD_EPOLL)

	int epollSize = readList.size() + writeList.size() + exceptList.size();
	if (epollSize == 0) return 0;

	int epollfd = -1;
	{
		struct epoll_event eventsIn[epollSize];
		memset(eventsIn, 0, sizeof(eventsIn));
		struct epoll_event* eventLast = eventsIn;
		for (SocketList::iterator it = readList.begin(); it != readList.end(); ++it)
		{
			if (it->sockfd() != POCO_INVALID_SOCKET)
			{
				struct epoll_event* e = eventsIn;
				for (; e != eventLast; ++e)
				{
					if (reinterpret_cast<Socket*> (e->data.ptr)->sockfd() == it->sockfd())
						break;
				}
				if (e == eventLast)
				{
					e->data.ptr = &(*it);
					++eventLast;
				}
				e->events |= EPOLLIN;
			}
		}

		for (SocketList::iterator it = writeList.begin(); it != writeList.end(); ++it)
		{
			if (it->sockfd() != POCO_INVALID_SOCKET)
			{
				struct epoll_event* e = eventsIn;
				for (; e != eventLast; ++e)
				{
					if (reinterpret_cast<Socket*> (e->data.ptr)->sockfd() == it->sockfd())
						break;
				}
				if (e == eventLast)
				{
					e->data.ptr = &(*it);
					++eventLast;
				}
				e->events |= EPOLLOUT;
			}
		}

		for (SocketList::iterator it = exceptList.begin(); it != exceptList.end(); ++it)
		{
			if (it->sockfd() != POCO_INVALID_SOCKET)
			{
				struct epoll_event* e = eventsIn;
				for (; e != eventLast; ++e)
				{
					if (reinterpret_cast<Socket*> (e->data.ptr)->sockfd() == it->sockfd())
						break;
				}
				if (e == eventLast)
				{
					e->data.ptr = &(*it);
					++eventLast;
				}
				e->events |= EPOLLERR;
			}
		}

		epollSize = eventLast - eventsIn;
		epollfd = epoll_create(epollSize);
		if (epollfd < 0)
		{
			char buf[1024];
			strerror_r(errno, buf, sizeof(buf));

			SocketImpl::error(std::string("Can't create epoll queue: ") + buf);
		}

		for (struct epoll_event* e = eventsIn; e != eventLast; ++e)
		{
			if (epoll_ctl(epollfd, EPOLL_CTL_ADD, reinterpret_cast<Socket*> (e->data.ptr)->sockfd(), e) < 0)
			{
				char buf[1024];
				strerror_r(errno, buf, sizeof(buf));
				::close(epollfd);
				SocketImpl::error(std::string("Can't insert socket to epoll queue: ") + buf);
			}
		}
	}

	struct epoll_event eventsOut[epollSize];
	memset(eventsOut, 0, sizeof(eventsOut));

	Poco::Timespan remainingTime(timeout);
	int rc;
	do
	{
		Poco::Timestamp start;
		rc = epoll_wait(epollfd, eventsOut, epollSize, remainingTime.totalMilliseconds());
		if (rc < 0 && SocketImpl::lastError() == POCO_EINTR)
		{
 			Poco::Timestamp end;
			Poco::Timespan waited = end - start;
			if (waited < remainingTime)
				remainingTime -= waited;
			else
				remainingTime = 0;
		}
	}
	while (rc < 0 && SocketImpl::lastError() == POCO_EINTR);

	::close(epollfd);
	if (rc < 0) SocketImpl::error();

 	SocketList readyReadList;
	SocketList readyWriteList;
	SocketList readyExceptList;
	for (int n = 0; n < rc; ++n)
	{
		if (eventsOut[n].events & EPOLLERR)
			readyExceptList.push_back(*reinterpret_cast<Socket*>(eventsOut[n].data.ptr));
		if (eventsOut[n].events & EPOLLIN)
			readyReadList.push_back(*reinterpret_cast<Socket*>(eventsOut[n].data.ptr));
		if (eventsOut[n].events & EPOLLOUT)
			readyWriteList.push_back(*reinterpret_cast<Socket*>(eventsOut[n].data.ptr));
	}
	std::swap(readList, readyReadList);
	std::swap(writeList, readyWriteList);
	std::swap(exceptList, readyExceptList);
	return readList.size() + writeList.size() + exceptList.size();

#else

	fd_set fdRead;
	fd_set fdWrite;
	fd_set fdExcept;
	int nfd = 0;
	FD_ZERO(&fdRead);
	for (SocketList::const_iterator it = readList.begin(); it != readList.end(); ++it)
	{
		poco_socket_t fd = it->sockfd();
		if (fd != POCO_INVALID_SOCKET)
		{
			if (int(fd) > nfd)
				nfd = int(fd);
			FD_SET(fd, &fdRead);
		}
	}
	FD_ZERO(&fdWrite);
	for (SocketList::const_iterator it = writeList.begin(); it != writeList.end(); ++it)
	{
		poco_socket_t fd = it->sockfd();
		if (fd != POCO_INVALID_SOCKET)
		{
			if (int(fd) > nfd)
				nfd = int(fd);
			FD_SET(fd, &fdWrite);
		}
	}
	FD_ZERO(&fdExcept);
	for (SocketList::const_iterator it = exceptList.begin(); it != exceptList.end(); ++it)
	{
		poco_socket_t fd = it->sockfd();
		if (fd != POCO_INVALID_SOCKET)
		{
			if (int(fd) > nfd)
				nfd = int(fd);
			FD_SET(fd, &fdExcept);
		}
	}
	if (nfd == 0) return 0;
	Poco::Timespan remainingTime(timeout);
	int rc;
	do
	{
		struct timeval tv;
		tv.tv_sec  = (long) remainingTime.totalSeconds();
		tv.tv_usec = (long) remainingTime.useconds();
		Poco::Timestamp start;
		rc = ::select(nfd + 1, &fdRead, &fdWrite, &fdExcept, &tv);
		if (rc < 0 && SocketImpl::lastError() == POCO_EINTR)
		{
			Poco::Timestamp end;
			Poco::Timespan waited = end - start;
			if (waited < remainingTime)
				remainingTime -= waited;
			else
				remainingTime = 0;
		}
	}
	while (rc < 0 && SocketImpl::lastError() == POCO_EINTR);
	if (rc < 0) SocketImpl::error();
	
	SocketList readyReadList;
	for (SocketList::const_iterator it = readList.begin(); it != readList.end(); ++it)
	{
		poco_socket_t fd = it->sockfd();
		if (fd != POCO_INVALID_SOCKET)
		{
			if (FD_ISSET(fd, &fdRead))
				readyReadList.push_back(*it);
		}
	}
	std::swap(readList, readyReadList);
	SocketList readyWriteList;
	for (SocketList::const_iterator it = writeList.begin(); it != writeList.end(); ++it)
	{
		poco_socket_t fd = it->sockfd();
		if (fd != POCO_INVALID_SOCKET)
		{
			if (FD_ISSET(fd, &fdWrite))
				readyWriteList.push_back(*it);
		}
	}
	std::swap(writeList, readyWriteList);
	SocketList readyExceptList;
	for (SocketList::const_iterator it = exceptList.begin(); it != exceptList.end(); ++it)
	{
		poco_socket_t fd = it->sockfd();
		if (fd != POCO_INVALID_SOCKET)
		{
			if (FD_ISSET(fd, &fdExcept))
				readyExceptList.push_back(*it);
		}
	}
	std::swap(exceptList, readyExceptList);	
	return rc; 

#endif // POCO_HAVE_FD_EPOLL
}


} } // namespace Poco::Net
