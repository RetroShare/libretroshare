/*******************************************************************************
 * libretroshare/src/ft: ftfileprovider.cc                                     *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2008 by Robert Fernie <retroshare@lunamutt.com>                   *
 *                                                                             *
 * This program is free software: you can redistribute it and/or modify        *
 * it under the terms of the GNU Lesser General Public License as              *
 * published by the Free Software Foundation, either version 3 of the          *
 * License, or (at your option) any later version.                             *
 *                                                                             *
 * This program is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                *
 * GNU Lesser General Public License for more details.                         *
 *                                                                             *
 * You should have received a copy of the GNU Lesser General Public License    *
 * along with this program. If not, see <https://www.gnu.org/licenses/>.       *
 *                                                                             *
 *******************************************************************************/

#include <cstdlib>
#include <cstdio>

#include "ftfileprovider.h"
#include "ftchunkmap.h"
#include "util/rstime.h"
#include "util/rsdir.h"
#include "util/largefile_retrocompat.hpp"

#ifdef WINDOWS_SYS
#	include "util/rswin.h"
#endif // WINDOWS_SYS


/********
* #define DEBUG_FT_FILE_PROVIDER 1
* #define DEBUG_TRANSFERS	 1 // TO GET TIMESTAMPS of DATA READING 
********/

#ifdef DEBUG_TRANSFERS
	#include "util/rsprint.h"
	#include <iomanip>
#endif

static const rstime_t UPLOAD_CHUNK_MAPS_TIME = 20 ;	// time to ask for a new chunkmap from uploaders in seconds.

ftFileProvider::ftFileProvider(const std::string& path, uint64_t size, const RsFileHash& hash)
	: mSize(size), hash(hash), file_name(path), fd(NULL), ftcMutex("ftFileProvider"), mReadMtx("ftFileProvider read")
{
	RsStackMutex stack(ftcMutex); /********** STACK LOCKED MTX ******/

#ifdef DEBUG_FT_FILE_PROVIDER
	std::cout << "Creating file provider for " << hash << std::endl ;
#endif
}

ftFileProvider::~ftFileProvider()
{
	RS_STACK_MUTEX(mReadMtx); // no read in progress on fd
	RsStackMutex stack(ftcMutex); /********** STACK LOCKED MTX ******/
#ifdef DEBUG_FT_FILE_PROVIDER
	std::cout << "ftFileProvider::~ftFileProvider(): Destroying file provider for " << hash << std::endl ;
#endif
	if (fd!=NULL) {
		fclose(fd);
		fd = NULL ;
#ifdef DEBUG_FT_FILE_PROVIDER
		std::cout << "ftFileProvider::~ftFileProvider(): closed file: " << hash << std::endl ;
#endif
	}
}

bool	ftFileProvider::fileOk()
{
	RsStackMutex stack(ftcMutex); /********** STACK LOCKED MTX ******/
	return (fd != NULL);
}

RsFileHash ftFileProvider::getHash()
{
        RsStackMutex stack(ftcMutex); /********** STACK LOCKED MTX ******/
	return hash;
}

uint64_t ftFileProvider::getFileSize()
{
        RsStackMutex stack(ftcMutex); /********** STACK LOCKED MTX ******/
	return mSize;
}

bool    ftFileProvider::FileDetails(FileInfo &info)
{
	RsStackMutex stack(ftcMutex); /********** STACK LOCKED MTX ******/
	info.hash = hash;
	info.size = mSize;
	info.path = file_name;
	info.fname = RsDirUtil::getTopDir(file_name);

	info.transfered = 0 ; // unused
	info.lastTS = 0;
	info.downloadStatus = FT_STATE_DOWNLOADING ;

	info.peers.clear() ;
	float total_transfer_rate = 0.0f ;

	for(std::map<RsPeerId,PeerUploadInfo>::const_iterator it(uploading_peers.begin());it!=uploading_peers.end();++it)
	{
		TransferInfo inf ;
		inf.peerId = it->first ;
		inf.status = FT_STATE_DOWNLOADING ;
		inf.name = info.fname ;
		inf.transfered = it->second.req_loc ;

		inf.tfRate = it->second.transfer_rate/1024.0 ;
		total_transfer_rate += it->second.transfer_rate ;
		info.lastTS = std::max(info.lastTS,it->second.lastTS);

		info.peers.push_back(inf) ;
	}
	info.tfRate = total_transfer_rate/1024.0 ;

	/* Use req_loc / req_size to estimate data rate */

	return true;
}

bool ftFileProvider::purgeOldPeers(rstime_t now,uint32_t max_duration)
{
	RsStackMutex stack(ftcMutex); /********** STACK LOCKED MTX ******/

#ifdef DEBUG_FT_FILE_PROVIDER
	std::cerr << "ftFileProvider::purgeOldPeers(): " << (void*)this << ": examining peers." << std::endl ;
#endif
	bool ret = true ;
	for(std::map<RsPeerId,PeerUploadInfo>::iterator it(uploading_peers.begin());it!=uploading_peers.end();)
		if( (*it).second.lastTS+max_duration < (uint32_t)now)
		{
#ifdef DEBUG_FT_FILE_PROVIDER
			std::cerr << "ftFileProvider::purgeOldPeers(): " << (void*)this << ": peer " << it->first << " is too old. Removing." << std::endl ;
#endif
			std::map<RsPeerId,PeerUploadInfo>::iterator tmp = it ;
			++tmp ;
			uploading_peers.erase(it) ;
			it=tmp ;
		}
		else
		{
#ifdef DEBUG_FT_FILE_PROVIDER
			std::cerr << "ftFileProvider::purgeOldPeers(): " << (void*)this << ": peer " << it->first << " will be kept." << std::endl ;
#endif
			ret = false ;
			++it ;
		}
	return ret ;
}

void ftFileProvider::getAvailabilityMap(CompressedChunkMap& cmap) 
{
	// We are here because the file we deal with is complete. So we return a plain map.
	//
	ChunkMap::buildPlainMap(mSize,cmap) ;
}


bool ftFileProvider::getFileData(const RsPeerId& peer_id,uint64_t offset, uint32_t &chunk_size, void *data, bool /*allow_unverified*/)
{
	/* dodgey checking outside of mutex...
	 * much check again inside FileAttrs().
	 */
	if (fd == NULL)
		if (!initializeFileAttrs())
			return false;

	/* The disk read of a plain provider is done without ftcMutex: on a slow or
	 * busy disk it lasts seconds, and FileDetails(), purgeOldPeers() and the
	 * chunk map calls, which the GUI and the main loop reach through
	 * ftDataMultiplex, must not wait for it. A file that is also written
	 * through fd (ftFileCreator) keeps its reads under ftcMutex. */

	const bool read_under_ftc_mutex = fileIsWrittenTo();
	uint32_t data_size = 0;
	FILE *read_fd = NULL;

	{
		RsStackMutex stack(ftcMutex); /********** STACK LOCKED MTX ******/

		if(offset >= mSize)
		{
			std::cerr << "ftFileProvider::getFileData(): request (" << offset << ") exceeds file size (" << mSize << "! " << std::endl;
			return false ;
		}

		data_size = chunk_size;

		if (offset + data_size > mSize)
		{
			data_size = mSize - offset;
			chunk_size = mSize - offset;
			std::cerr <<"Chunk Size greater than total file size, adjusting chunk size " << data_size << std::endl;
		}

		if(data_size == 0 || data == NULL)
		{
			std::cerr << "No data to read, or NULL buffer used" << std::endl;
			return false;
		}

		if(read_under_ftc_mutex)
		{
			if(!locked_readFile(fd, offset, data_size, data))
				return false;

			// This creates the peer info, and updates it (for GUI display).
			uploading_peers[peer_id].updateStatus(offset,data_size,time(NULL)) ;
			return true;
		}

		read_fd = fd; // set once by initializeFileAttrs(), closed by the destructor only
	}

	{
		RS_STACK_MUTEX(mReadMtx);

		if(!locked_readFile(read_fd, offset, data_size, data))
			return false;
	}

	{
		RsStackMutex stack(ftcMutex); /********** STACK LOCKED MTX ******/

		// This creates the peer info, and updates it (for GUI display).
		uploading_peers[peer_id].updateStatus(offset,data_size,time(NULL)) ;
	}

#ifdef DEBUG_TRANSFERS
	std::cerr << "ftFileProvider::getFileData() ";
	std::cerr << " at " << RsUtil::AccurateTimeString();
	std::cerr << " hash: " << hash;
	std::cerr << " for peerId: " << peer_id;
	std::cerr << " offset: " << offset;
	std::cerr << " chunkSize: " << chunk_size;
	std::cerr << std::endl;
#endif

	return true;
}

/* Reads data_size bytes at offset into data, which the caller allocated and
 * frees. Called with the mutex that protects the position of the file held:
 * mReadMtx for a plain provider, ftcMutex for a file creator. */
bool ftFileProvider::locked_readFile(FILE *file, uint64_t offset, uint32_t data_size, void *data)
{
	if(file == NULL)
		return false;

	if(fseeko64(file, offset, SEEK_SET) == -1)
	{
#ifdef DEBUG_FT_FILE_PROVIDER
		std::cerr << "ftFileProvider::getFileData() Failed to seek. Data_size=" << data_size << ", base_loc=" << offset << " !" << std::endl;
#endif
		return false;
	}

	if (1 != fread(data, data_size, 1, file))
	{
#ifdef DEBUG_FT_FILE_PROVIDER
		std::cerr << "ftFileProvider::getFileData() Failed to get data. Data_size=" << data_size << ", base_loc=" << offset << " !" << std::endl;
#endif
		return false;
	}
	return true;
}

void ftFileProvider::PeerUploadInfo::updateStatus(uint64_t offset,uint32_t data_size,rstime_t now)
{
	lastTS = now ;
	long int diff = (long int)now - (long int)lastTS_t ;	// in bytes/s. Average over multiple samples

#ifdef DEBUG_FT_FILE_PROVIDER
	std::cout << "diff = " << diff << std::endl ;
#endif

	if(diff > 3)
	{
		transfer_rate = total_size / (float)diff ;
#ifdef DEBUG_FT_FILE_PROVIDER
		std::cout << "updated TR = " << transfer_rate << ", total_size=" << total_size << std::endl ;
#endif
		lastTS_t = now ;
		total_size = 0 ;
	}

	req_loc = offset;
	req_size = data_size;
	total_size += req_size ;
}

void ftFileProvider::setClientMap(const RsPeerId& peer_id,const CompressedChunkMap& cmap)
{
	RsStackMutex stack(ftcMutex); /********** STACK LOCKED MTX ******/

	// Create by default.
	uploading_peers[peer_id].client_chunk_map = cmap ;
	uploading_peers[peer_id].client_chunk_map_stamp = time(NULL) ;
}

void ftFileProvider::getClientMap(const RsPeerId& peer_id,CompressedChunkMap& cmap,bool& map_is_too_old)
{
	RsStackMutex stack(ftcMutex); /********** STACK LOCKED MTX ******/

	PeerUploadInfo& pui(uploading_peers[peer_id]) ;

	rstime_t now = time(NULL) ;

	if(now - pui.client_chunk_map_stamp > UPLOAD_CHUNK_MAPS_TIME)
	{
		map_is_too_old = true ;
		pui.client_chunk_map_stamp = now ;	// to avoid re-asking before the TTL
	}
	else
		map_is_too_old = false ;

	cmap = pui.client_chunk_map;
}

int ftFileProvider::initializeFileAttrs()
{
#ifdef DEBUG_FT_FILE_PROVIDER
	std::cerr << "ftFileProvider::initializeFileAttrs() Filename: " << file_name << std::endl;
#endif

	RsStackMutex stack(ftcMutex); /********** STACK LOCKED MTX ******/
	if (fd)
		return 1;

	/* 
	 * check if the file exists 
	 */

	{
#ifdef DEBUG_FT_FILE_PROVIDER
		std::cerr << "ftFileProvider::initializeFileAttrs() trying (r+b) " << std::endl;
#endif
	}

	/* 
	 * attempt to open file 
	 */

	fd = RsDirUtil::rs_fopen(file_name.c_str(), "r+b");
	if (!fd)
	{
		std::cerr << "ftFileProvider::initializeFileAttrs() Failed to open (r+b): ";
		std::cerr << file_name << std::endl;

		/* try opening read only */
		fd = RsDirUtil::rs_fopen(file_name.c_str(), "rb");
		if (!fd)
		{
			std::cerr << "ftFileProvider::initializeFileAttrs() Failed to open (rb): ";
			std::cerr << file_name << std::endl;

			/* try opening read only */
			return 0;
		}
	}
#ifdef DEBUG_FT_FILE_PROVIDER
	std::cerr << "ftFileProvider:: openned file " << file_name << std::endl ;
#endif

	return 1;
}


