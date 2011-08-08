
// Environment_VX.cpp
//
// $Id: //poco/1.4/Foundation/src/Environment_VX.cpp#2 $
//
// Library: Foundation
// Package: Core
// Module:  Environment
//
// Copyright (c) 2004-2011, Applied Informatics Software Engineering GmbH.
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


#include "Poco/Environment_VX.h"
#include "Poco/Exception.h"
#include "Poco/Buffer.h"
#include <VxWorks.h>
#include <envLib.h>
#include <hostLib.h>
#include <ifLib.h>
#include <sockLib.h>
#include <ioLib.h>
#include <version.h>
#include <cstring>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/if_ether.h>
#include <ifLib.h>
#include <unistd.h>


namespace Poco {


EnvironmentImpl::StringMap EnvironmentImpl::_map;
FastMutex EnvironmentImpl::_mutex;


std::string EnvironmentImpl::getImpl(const std::string& name)
{
	FastMutex::ScopedLock lock(_mutex);
	
	const char* val = getenv(name.c_str());
	if (val)
		return std::string(val);
	else
		throw NotFoundException(name);
}


bool EnvironmentImpl::hasImpl(const std::string& name)
{
	FastMutex::ScopedLock lock(_mutex);

	return getenv(name.c_str()) != 0;
}


void EnvironmentImpl::setImpl(const std::string& name, const std::string& value)
{
	FastMutex::ScopedLock lock(_mutex);
	
	std::string var = name;
	var.append("=");
	var.append(value);
	_map[name] = var;
	if (putenv((char*) _map[name].c_str()))
	{
		std::string msg = "cannot set environment variable: ";
		msg.append(name);
		throw SystemException(msg);
	}
}


std::string EnvironmentImpl::osNameImpl()
{
	return runtimeName;
}


std::string EnvironmentImpl::osVersionImpl()
{
	return runtimeVersion;
}


std::string EnvironmentImpl::osArchitectureImpl()
{
#if POCO_ARCH == POCO_ARCH_IA32
	return "i386";
#elif POCO_ARCH == POCO_ARCH_MIPS
	return "mips";
#elif POCO_ARCH == POCO_ARCH_PPC
	return "ppc";
#elif POCO_ARCH == POCO_ARCH_ARM
	return "arm";
#elif POCO_ARCH == POCO_ARCH_SH
	return "sh";
#else
	return "unknown";
#endif	
}


std::string EnvironmentImpl::nodeNameImpl()
{
	char buffer[64];
	if (gethostname(buffer, sizeof(buffer)) == OK)
		return buffer;
	else
		return "unknown";
}


unsigned EnvironmentImpl::processorCountImpl()
{
	return 1;
}


void EnvironmentImpl::nodeIdImpl(NodeId& id)
{
	std::memset(&id, 0, sizeof(id));

	int ifIndex = 1;
	char ifName[32];
	for (;;)
	{
		if (ifIndexToIfName(ifIndex, ifName) == OK)
		{
			struct ifnet* pIf = ifunit(ifName);
			if (pIf)
			{
				std::memcpy(&id, ((struct arpcom *) pIf)->ac_enaddr, sizeof(id));
				return;
			}
		}
		else break;	
		++ifIndex;
	}
	throw SystemException("cannot get Ethernet hardware address");
}


} // namespace Poco
