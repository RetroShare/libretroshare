/*******************************************************************************
 * libretroshare/src/pgp: pgphandler.cc                                        *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2018 Cyril Soler <csoler@users.sourceforge.net>                   *
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
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "pgphandler.h"
#include "retroshare/rsiface.h"		// For rsicontrol.
#include "retroshare/rspeers.h"		// For rsicontrol.
#include "util/rsdir.h"
#include "util/rsdiscspace.h"
#include "util/rsmemory.h"
#include "pgp/pgpkeyutil.h"
#include "util/largefile_retrocompat.hpp"

#ifdef WINDOWS_SYS
#include <io.h>
#include "util/rsstring.h"
#include "util/rswin.h"
#endif

static const uint32_t PGP_CERTIFICATE_LIMIT_MAX_NAME_SIZE   = 64 ;
static const uint32_t PGP_CERTIFICATE_LIMIT_MAX_EMAIL_SIZE  = 64 ;
static const uint32_t PGP_CERTIFICATE_LIMIT_MAX_PASSWD_SIZE = 1024 ;

//#define DEBUG_PGPHANDLER 1
//#define PGPHANDLER_DSA_SUPPORT

PassphraseCallback PGPHandler::_passphrase_callback = NULL ;

void PGPHandler::setPassphraseCallback(PassphraseCallback cb)
{
	_passphrase_callback = cb ;
}

PGPHandler::PGPHandler(const std::string& pubring, const std::string& secring,const std::string& trustdb,const std::string& pgp_lock_filename)
	: pgphandlerMtx(std::string("PGPHandler")), 
	_pubring_path(pubring),
	_secring_path(secring),
	_trustdb_path(trustdb),
	_pgp_lock_filename(pgp_lock_filename),
	_trustdb_changed(false),
	_pubring_changed(false),
    _pubring_last_update_time(time(NULL)),
    _trustdb_last_update_time(0)
{
}

PGPHandler::~PGPHandler()
{
}

bool PGPHandler::printKeys() const
{
#ifdef DEBUG_PGPHANDLER
    RsErr() << "Printing details of all " << std::dec << _public_keyring_map.size() << " keys: " ;
#endif

	for(std::map<RsPgpId,PGPCertificateInfo>::const_iterator it(_public_keyring_map.begin()); it != _public_keyring_map.end(); ++it)
	{
        RsErr() << "PGP Key: " << it->first.toStdString() ;

        RsErr() << "\tName          : " <<  it->second._name ;
        RsErr() << "\tEmail         : " <<  it->second._email ;
        RsErr() << "\tOwnSign       : " << (it->second._flags & PGPCertificateInfo::PGP_CERTIFICATE_FLAG_HAS_OWN_SIGNATURE) ;
        RsErr() << "\tAccept Connect: " << (it->second._flags & PGPCertificateInfo::PGP_CERTIFICATE_FLAG_ACCEPT_CONNEXION) ;
        RsErr() << "\ttrustLvl      : " <<  it->second._trustLvl ;
        RsErr() << "\tvalidLvl      : " <<  it->second._validLvl ;
        RsErr() << "\tUse time stamp: " <<  it->second._time_stamp ;
        RsErr() << "\tfingerprint   : " <<  it->second._fpr.toStdString() ;
        RsErr() << "\tSigners       : " << it->second.signers.size() ;

		std::set<RsPgpId>::const_iterator sit;
		for(sit = it->second.signers.begin(); sit != it->second.signers.end(); ++sit)
		{
            RsErr() << "\t\tSigner ID:" << (*sit).toStdString() << ", Name: " ;
			const PGPCertificateInfo *info = PGPHandler::getCertificateInfo(*sit) ;

			if(info != NULL)
                RsErr() << info->_name ;

            RsErr() << std::endl ;
		}
	}
	return true ;
}

const PGPCertificateInfo *PGPHandler::getCertificateInfo(const RsPgpId& id) const
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

	std::map<RsPgpId,PGPCertificateInfo>::const_iterator it( _public_keyring_map.find(id) ) ;

	if(it != _public_keyring_map.end())
		return &it->second;
	else
		return NULL ;
}

void PGPHandler::updateOwnSignatureFlag(const RsPgpId& own_id) 
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

    if(_public_keyring_map.find(own_id)==_public_keyring_map.end())
    {
        RsErr() << __func__ << ": key with id=" << own_id.toStdString() << " not in keyring." ;
        // return now, because the following operation would add an entry to _public_keyring_map
        return;
    }

	PGPCertificateInfo& own_cert(_public_keyring_map[ own_id ]) ;

	for(std::map<RsPgpId,PGPCertificateInfo>::iterator it=_public_keyring_map.begin();it!=_public_keyring_map.end();++it)
		locked_updateOwnSignatureFlag(it->second,it->first,own_cert,own_id) ;
}
void PGPHandler::updateOwnSignatureFlag(const RsPgpId& cert_id,const RsPgpId& own_id)
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

	std::map<RsPgpId,PGPCertificateInfo>::iterator it( _public_keyring_map.find(cert_id) ) ;

	if(it == _public_keyring_map.end())
	{
        RsErr() << "updateOwnSignatureFlag: Cannot get certificate for string " << cert_id.toStdString() << ". This is probably a bug." ;
		return ;
	}

	PGPCertificateInfo& cert( it->second );

	PGPCertificateInfo& own_cert(_public_keyring_map[ own_id ]) ;

	locked_updateOwnSignatureFlag(cert,cert_id,own_cert,own_id) ;
}
void PGPHandler::locked_updateOwnSignatureFlag(PGPCertificateInfo& cert,const RsPgpId& cert_id,PGPCertificateInfo& own_cert,const RsPgpId& own_id_str)
{
	if(cert.signers.find(own_id_str) != cert.signers.end())
		cert._flags |= PGPCertificateInfo::PGP_CERTIFICATE_FLAG_HAS_OWN_SIGNATURE ;
	else
		cert._flags &= ~PGPCertificateInfo::PGP_CERTIFICATE_FLAG_HAS_OWN_SIGNATURE ;

	if(own_cert.signers.find( cert_id ) != own_cert.signers.end())
		cert._flags |= PGPCertificateInfo::PGP_CERTIFICATE_FLAG_HAS_SIGNED_ME ;
	else
		cert._flags &= ~PGPCertificateInfo::PGP_CERTIFICATE_FLAG_HAS_SIGNED_ME ;
}

/*static*/ RsPgpId PGPHandler::pgpIdFromFingerprint(const RsPgpFingerprint& f)
{
	return RsPgpId::fromBufferUnsafe(
	            f.toByteArray() +
	            RsPgpFingerprint::SIZE_IN_BYTES - RsPgpId::SIZE_IN_BYTES );
}

void PGPHandler::setAcceptConnexion(const RsPgpId& id,bool b)
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

	std::map<RsPgpId,PGPCertificateInfo>::iterator res = _public_keyring_map.find(id) ;

	if(res != _public_keyring_map.end())
	{
		if(b)
			res->second._flags |= PGPCertificateInfo::PGP_CERTIFICATE_FLAG_ACCEPT_CONNEXION ;
		else
			res->second._flags &= ~PGPCertificateInfo::PGP_CERTIFICATE_FLAG_ACCEPT_CONNEXION ;
	}
}

bool PGPHandler::getGPGFilteredList(std::list<RsPgpId>& list,bool (*filter)(const PGPCertificateInfo&)) const
{
	RsStackMutex mtx(pgphandlerMtx) ;	// lock access to PGP directory.
	list.clear() ;

	for(std::map<RsPgpId,PGPCertificateInfo>::const_iterator it(_public_keyring_map.begin());it!=_public_keyring_map.end();++it)
		if( filter == NULL || (*filter)(it->second) )
			list.push_back(RsPgpId(it->first)) ;

	return true ;
}

bool PGPHandler::LoadCertificateFromBinaryData(const unsigned char *data,uint32_t data_len,RsPgpId& id,std::string& error_string)
{
    return LoadCertificate(data,data_len,false,id,error_string);
}

bool PGPHandler::LoadCertificateFromString(const std::string& pgp_cert,RsPgpId& id,std::string& error_string)
{
    return LoadCertificate((unsigned char*)(pgp_cert.c_str()),pgp_cert.length(),true,id,error_string);
}


bool PGPHandler::availableGPGCertificatesWithPrivateKeys(std::list<RsPgpId>& ids)
{
    RsStackMutex mtx(pgphandlerMtx) ;	// lock access to PGP memory structures.

    ids.clear();

    for(auto it:_secret_keyring_map)
        ids.push_back(it.first);

    return !ids.empty();
}


bool PGPHandler::isPgpPubKeyAvailable(const RsPgpId &id)
{ return _public_keyring_map.find(id) != _public_keyring_map.end(); }

bool PGPHandler::isGPGId(const RsPgpId &id)
{
	return _public_keyring_map.find(id) != _public_keyring_map.end() ;
}

bool PGPHandler::isGPGSigned(const RsPgpId &id)
{
	std::map<RsPgpId,PGPCertificateInfo>::const_iterator res = _public_keyring_map.find(id) ;
	return res != _public_keyring_map.end() && (res->second._flags & PGPCertificateInfo::PGP_CERTIFICATE_FLAG_HAS_OWN_SIGNATURE) ;
}

bool PGPHandler::isGPGAccepted(const RsPgpId &id)
{
	std::map<RsPgpId,PGPCertificateInfo>::const_iterator res = _public_keyring_map.find(id) ;
	return (res != _public_keyring_map.end()) && (res->second._flags & PGPCertificateInfo::PGP_CERTIFICATE_FLAG_ACCEPT_CONNEXION) ;
}

bool PGPHandler::parseSignature(unsigned char *sign, unsigned int signlen,RsPgpId& issuer_id) 
{
	PGPSignatureInfo info ;
    
    if(!PGPKeyManagement::parseSignature(sign,signlen,info))
        return false ;
    
    unsigned char bytes[8] ;
    for(int i=0;i<8;++i)
    {
        bytes[7-i] = info.issuer & 0xff ;
        info.issuer >>= 8 ;
    }
    issuer_id = RsPgpId(bytes) ;
    
    return true ;     
}

bool PGPHandler::privateTrustCertificate(const RsPgpId& id,int trustlvl)
{
	if(trustlvl < 0 || trustlvl >= 6 || trustlvl == 1)
	{
        RsErr() << "Invalid trust level " << trustlvl << " passed to privateTrustCertificate." ;
		return false ;
	}

	std::map<RsPgpId,PGPCertificateInfo>::iterator it = _public_keyring_map.find(id);

	if(it == _public_keyring_map.end())
	{
        RsErr() << "(EE) Key id " << id.toStdString() << " not in the keyring. Can't setup trust level." ;
		return false ;
	}

	if( (int)it->second._trustLvl != trustlvl )
		_trustdb_changed = true ;

	it->second._trustLvl = trustlvl ;

	return true ;
}

struct PrivateTrustPacket
{
	/// pgp id in unsigned char format.
	unsigned char user_id[RsPgpId::SIZE_IN_BYTES];
	uint8_t trust_level ;						// trust level. From 0 to 6.
	uint32_t time_stamp ;						// last time the cert was ever used, in seconds since the epoch. 0 means not initialized.
};

void PGPHandler::locked_readPrivateTrustDatabase()
{
	FILE *fdb = RsDirUtil::rs_fopen(_trustdb_path.c_str(),"rb") ;
#ifdef DEBUG_PGPHANDLER
    RsErr() << "PGPHandler:  Reading private trust database." ;
#endif

	if(fdb == NULL)
	{
        RsErr() << "  private trust database not found. No trust info loaded." << std::endl ;
		return ;
	}
	std::map<RsPgpId,PGPCertificateInfo>::iterator it ;
	PrivateTrustPacket trustpacket;
	int n_packets = 0 ;

	while(fread((void*)&trustpacket,sizeof(PrivateTrustPacket),1,fdb) == 1)
	{
		it = _public_keyring_map.find(RsPgpId(trustpacket.user_id)) ;

		if(it == _public_keyring_map.end())
		{
            RsErr() << "  (WW) Trust packet found for unknown key id " << RsPgpId(trustpacket.user_id).toStdString() ;
			continue ;
		}
		if(trustpacket.trust_level > 6)
		{
            RsErr() << "  (WW) Trust packet found with unexpected trust level " << trustpacket.trust_level ;
			continue ;
		}
		
		++n_packets ;
		it->second._trustLvl = trustpacket.trust_level ;

		if(trustpacket.time_stamp > it->second._time_stamp)	// only update time stamp if the loaded time stamp is newer
		   it->second._time_stamp = trustpacket.time_stamp ;
	}

	fclose(fdb) ;

    RsErr() << "PGPHandler: Successfully read " << n_packets << " trust packets." ;
}

bool PGPHandler::locked_writePrivateTrustDatabase()
{
	FILE *fdb = RsDirUtil::rs_fopen((_trustdb_path+".tmp").c_str(),"wb") ;
#ifdef DEBUG_PGPHANDLER
    RsErr() << "PGPHandler:  Reading private trust database." ;
#endif

	if(fdb == NULL)
	{
        RsErr() << "  (EE) Can't open private trust database file " << _trustdb_path << " for write. Giving up!" << std::endl ;
		return false;
	}
	PrivateTrustPacket trustpacket ;
	/* Clear PrivateTrustPacket struct to suppress valgrind warnings due to the compiler extra padding*/
	memset(&trustpacket, 0, sizeof(PrivateTrustPacket));

	for( std::map<RsPgpId,PGPCertificateInfo>::iterator it =
	     _public_keyring_map.begin(); it!=_public_keyring_map.end(); ++it )
	{
		memcpy( trustpacket.user_id,
		        it->first.toByteArray(),
		        RsPgpId::SIZE_IN_BYTES );
		trustpacket.trust_level = it->second._trustLvl ;
		trustpacket.time_stamp = it->second._time_stamp ;

		if(fwrite((void*)&trustpacket,sizeof(PrivateTrustPacket),1,fdb) != 1)
		{
            RsErr() << "  (EE) Cannot write to trust database " << _trustdb_path << ". Disc full, or quota exceeded ? Leaving database untouched." ;
			fclose(fdb) ;
			return false;
		}
	}

	fclose(fdb) ;

	if(!RsDirUtil::renameFile(_trustdb_path+".tmp",_trustdb_path))
	{
        RsErr() << "  (EE) Cannot move temp file " << _trustdb_path+".tmp" << ". Bad write permissions?" ;
		return false ;
	}
	else
		return true ;
}

bool PGPHandler::locked_syncTrustDatabase()
{
	struct stat64 buf ;
#ifdef WINDOWS_SYS
	std::wstring wfullname;
	librs::util::ConvertUtf8ToUtf16(_trustdb_path, wfullname);
	if(-1 == _wstati64(wfullname.c_str(), &buf))
#else
    if(-1 == stat64(_trustdb_path.c_str(), &buf))
#endif
    {
        RsErr() << "PGPHandler::syncDatabase(): can't stat file " << _trustdb_path << ". Will force write it." ;
        _trustdb_changed = true ;	// we force write of trust database if it does not exist.
        buf.st_mtime = 0;
    }

	if(_trustdb_last_update_time < buf.st_mtime)
	{
        RsErr() << "Detected change on disk of trust database. " << std::endl ;

		locked_readPrivateTrustDatabase();
		_trustdb_last_update_time = time(NULL) ;
	}

	if(_trustdb_changed)
	{
#ifdef DEBUG_PGPHANDLER
        RsDbg() << "Local changes in trust database. Writing to disk..." ;
#endif
		if(!locked_writePrivateTrustDatabase())
            RsErr() << "Cannot write trust database. Disk full? Disk quota exceeded?" ;
		else
		{
#ifdef DEBUG_PGPHANDLER
            RsDbg() << "Done." ;
#endif
			_trustdb_last_update_time = time(NULL) ;
			_trustdb_changed = false ;
		}
	}
	return true ;
}

bool PGPHandler::syncDatabase()
{
    RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.
    RsStackFileLock flck(_pgp_lock_filename) ;	// lock access to PGP directory.

#ifdef DEBUG_PGPHANDLER
    RsErr() << "Sync-ing keyrings." ;
#endif
    locked_syncPublicKeyring() ;
    //locked_syncSecretKeyring() ;

    // Now sync the trust database as well.
    //
    locked_syncTrustDatabase() ;

#ifdef DEBUG_PGPHANDLER
    RsErr() << "Done. " ;
#endif
    return true ;
}

bool PGPHandler::locked_syncPublicKeyring()
{
    struct stat64 buf ;
#ifdef WINDOWS_SYS
    std::wstring wfullname;
    librs::util::ConvertUtf8ToUtf16(_pubring_path, wfullname);
    if(-1 == _wstati64(wfullname.c_str(), &buf))
#else
    if(-1 == stat64(_pubring_path.c_str(), &buf))
#endif
    {
        RsErr() << "PGPHandler::syncPublicKeyring(): can't stat file " << _pubring_path << ". Can't sync public keyring." ;
        buf.st_mtime = 0;
    }

    if(_pubring_last_update_time < buf.st_mtime)
    {
        RsErr() << "Detected change on disk of public keyring. Merging!" << std::endl ;

        locked_updateKeyringFromDisk(false,_pubring_path) ;
        _pubring_last_update_time = buf.st_mtime ;
    }

    // Now check if the pubring was locally modified, which needs saving it again
    if(_pubring_changed && RsDiscSpace::checkForDiscSpace(RS_PGP_DIRECTORY))
    {
        std::string tmp_keyring_file = _pubring_path + ".tmp" ;

#ifdef DEBUG_PGPHANDLER
        RsErr() << "Local changes in public keyring. Writing to disk..." ;
#endif
        if(!locked_writeKeyringToDisk(false,tmp_keyring_file.c_str()))
        {
            RsErr() << "Cannot write public keyring tmp file. Disk full? Disk quota exceeded?" ;
            return false ;
        }
        if(!RsDirUtil::renameFile(tmp_keyring_file,_pubring_path))
        {
            RsErr() << "Cannot rename tmp pubring file " << tmp_keyring_file << " into actual pubring file " << _pubring_path << ". Check writing permissions?!?" ;
            return false ;
        }

#ifdef DEBUG_PGPHANDLER
        RsErr() << "Done." ;
#endif
        _pubring_last_update_time = time(NULL) ;	// should we get this value from the disk instead??
        _pubring_changed = false ;
    }
    return true ;
}

bool PGPHandler::extract_name_and_comment(const char *uid,std::string& name,std::string& comment,std::string& email)
{
    if(!uid)
    {
        RS_ERR("Missing uid! No valid string supplied");
        return false;
    }

    name ="";
    const std::string namestring(uid);

    uint32_t i=0;
    while(i < namestring.length() && namestring[i] != '(' && namestring[i] != '<') { name += namestring[i] ; ++i ;}

    // trim right spaces
    std::string::size_type found = name.find_last_not_of(' ');
    if (found != std::string::npos)
        name.erase(found + 1);
    else
        name.clear(); // all whitespace

    std::string& next = (namestring[i] == '(')?comment:email ;
    ++i ;
    next = "" ;
    while(i < namestring.length() && namestring[i] != ')' && namestring[i] != '>') { next += namestring[i] ; ++i ;}

    while(i < namestring.length() && namestring[i] != '(' && namestring[i] != '<') { next += namestring[i] ; ++i ;}

    if(i< namestring.length())
    {
        std::string& next2 = (namestring[i] == '(')?comment:email ;
        ++i ;
        next2 = "" ;
        while(i < namestring.length() && namestring[i] != ')' && namestring[i] != '>') { next2 += namestring[i] ; ++i ;}
    }

    return true;
}


