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

#include "util/largefile_retrocompat.hpp"

#ifdef WINDOWS_SYS
#include <io.h>
#include "util/rsstring.h"
#include "util/rswin.h"
#endif

#include "util/rsdir.h"		
#include "util/rsprint.h"
#include "util/rsdiscspace.h"
#include "util/rsmemory.h"		
#include "pgp/pgpkeyutil.h"
#include "retroshare/rspeers.h"

#include "rnppgphandler.h"
#include "rnp/rnp_err.h"

static const uint32_t PGP_CERTIFICATE_LIMIT_MAX_NAME_SIZE   = 64 ;
static const uint32_t PGP_CERTIFICATE_LIMIT_MAX_EMAIL_SIZE  = 64 ;
static const uint32_t PGP_CERTIFICATE_LIMIT_MAX_PASSWD_SIZE = 1024 ;

#define RNP_IDENTIFIER_KEYID  "keyid"

//#define DEBUG_PGPHANDLER 1
//#define PGPHANDLER_DSA_SUPPORT

#define DEBUG_RNP 1
#define NOT_IMPLEMENTED RsErr() << " function " << __PRETTY_FUNCTION__ << " Not implemented yet." << std::endl; return false;

// Helper structs to auto-delete after leaving current scope.

template<class T,rnp_result_t(*destructor)(T*)>
class t_ScopeGuard
{
public:
    t_ScopeGuard(T *& out) : mOut(out)
    {
        RsErr() << "Creating RNP structure pointer " << (void*)&mOut << " value: " << (void*)mOut;
    }
    ~t_ScopeGuard()
    {
        if(mOut != nullptr)
        {
            RsErr() << "Autodeleting RNP structure pointer " << (void*)&mOut << " value: " << (void*)mOut;
            destructor(mOut);
        }
    }
    T *& mOut;
};

rnp_result_t myBufferClean(char *s) { rnp_buffer_destroy(s); return RNP_SUCCESS; }

typedef t_ScopeGuard<rnp_output_st,          &rnp_output_destroy   >        rnp_output_autodelete;
typedef t_ScopeGuard<rnp_input_st,           &rnp_input_destroy    >        rnp_input_autodelete;
typedef t_ScopeGuard<rnp_op_verify_st,       &rnp_op_verify_destroy>        rnp_op_verify_autodelete;
typedef t_ScopeGuard<char            ,       &myBufferClean>                rnp_buffer_autodelete;
typedef t_ScopeGuard<rnp_key_handle_st,      &rnp_key_handle_destroy>       rnp_key_handle_autodelete;
typedef t_ScopeGuard<rnp_op_sign_st,         &rnp_op_sign_destroy>          rnp_op_sign_autodelete;
typedef t_ScopeGuard<rnp_op_encrypt_st,      &rnp_op_encrypt_destroy>       rnp_op_encrypt_autodelete;
typedef t_ScopeGuard<rnp_signature_handle_st,&rnp_signature_handle_destroy> rnp_signature_handle_autodelete;

#define RNP_INPUT_STRUCT(name)               rnp_input_t             name=nullptr; rnp_input_autodelete             name ## tmp_destructor(name);
#define RNP_OUTPUT_STRUCT(name)              rnp_output_t            name=nullptr; rnp_output_autodelete            name ## tmp_destructor(name);
#define RNP_OP_VERIFY_STRUCT(name)           rnp_op_verify_t         name=nullptr; rnp_op_verify_autodelete         name ## tmp_destructor(name);
#define RNP_KEY_HANDLE_STRUCT(name)          rnp_key_handle_t        name=nullptr; rnp_key_handle_autodelete        name ## tmp_destructor(name);
#define RNP_OP_SIGN_STRUCT(name)             rnp_op_sign_t           name=nullptr; rnp_op_sign_autodelete           name ## tmp_destructor(name);
#define RNP_OP_ENCRYPT_STRUCT(name)          rnp_op_encrypt_t        name=nullptr; rnp_op_encrypt_autodelete        name ## tmp_destructor(name);
#define RNP_SIGNATURE_HANDLE_STRUCT(name)    rnp_signature_handle_t  name=nullptr; rnp_signature_handle_autodelete  name ## tmp_destructor(name);
#define RNP_BUFFER_STRUCT(name)              char            *       name=nullptr; rnp_buffer_autodelete            name ## tmp_destructor(name);

// The following one misses a fnction to delete the pointer (problem in RNP lib???)

#define RNP_OP_VERIFY_SIGNATURE_STRUCT(name) rnp_op_verify_signature_t name=nullptr;

// Implementation of RNP pgp handler.

RNPPGPHandler::RNPPGPHandler(const std::string& pubring, const std::string& secring,const std::string& trustdb,const std::string& pgp_lock_filename)
    : PGPHandler(pubring,secring,trustdb,pgp_lock_filename)
{
    RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

    RsInfo() << "RNP-PGPHandler: Initing pgp keyrings";

    /* initialize FFI object */
    if (rnp_ffi_create(&mRnpFfi, "GPG", "GPG") != RNP_SUCCESS)
        throw std::runtime_error("RNPPGPHandler::RNPPGPHandler(): cannot initialize ffi object.");

    // Check that the file exists. If not, create a void keyring.

    bool pubring_exist = RsDirUtil::fileExists(pubring);
    bool secring_exist = RsDirUtil::fileExists(secring);

    // Read public and secret keyrings from supplied files.
    //
    if(pubring_exist)
    {
        RNP_INPUT_STRUCT(keyfile);

        /* load public keyring */
        if (rnp_input_from_path(&keyfile, pubring.c_str()) != RNP_SUCCESS)
            throw std::runtime_error("RNPPGPHandler::RNPPGPHandler(): cannot read public keyring. File access error.") ;

        if (rnp_load_keys(mRnpFfi, RNP_KEYSTORE_GPG, keyfile, RNP_LOAD_SAVE_PUBLIC_KEYS) != RNP_SUCCESS)
            throw std::runtime_error("RNPPGPHandler::RNPPGPHandler(): cannot read public keyring. File access error.") ;
    }
    else
        RsInfo() << "  pubring file: " << pubring << " not found. Creating an empty one";

    if(secring_exist)
    {
        RNP_INPUT_STRUCT(keyfile);

        /* load public keyring */
        if (rnp_input_from_path(&keyfile, secring.c_str()) != RNP_SUCCESS)
            throw std::runtime_error("RNPPGPHandler::RNPPGPHandler(): cannot read secret keyring. File access error.") ;

        if (rnp_load_keys(mRnpFfi, RNP_KEYSTORE_GPG, keyfile, RNP_LOAD_SAVE_SECRET_KEYS) != RNP_SUCCESS)
            throw std::runtime_error("RNPPGPHandler::RNPPGPHandler(): cannot read secret keyring. File access error.") ;
    }
    else
        RsInfo() << "  secring file: " << secring << " not found. Creating an empty one";


    size_t pub_count;
    size_t sec_count;
    rnp_get_public_key_count(mRnpFfi,&pub_count);
    rnp_get_secret_key_count(mRnpFfi,&sec_count);

    RsInfo() << "Loaded " << pub_count << " public keys, and " << sec_count << " secret keys." ;

    rnp_identifier_iterator_t it;
    rnp_identifier_iterator_create(mRnpFfi,&it,RNP_IDENTIFIER_KEYID);
    const char *key_identifier = nullptr;

    while(RNP_SUCCESS == rnp_identifier_iterator_next(it,&key_identifier) && key_identifier!=nullptr)
    {
        RNP_KEY_HANDLE_STRUCT(key_handle);
        rnp_locate_key(mRnpFfi,RNP_IDENTIFIER_KEYID,key_identifier,&key_handle);

        initCertificateInfo(key_handle) ;
    }

    rnp_identifier_iterator_destroy(it);

#ifdef TODO
        const ops_keydata_t *keydata ;
        int i=0 ;
        while( (keydata = ops_keyring_get_key_by_index(_pubring,i)) != NULL )
        {
            PGPCertificateInfo& cert(_public_keyring_map[ RsPgpId(keydata->key_id) ]) ;

            // Init all certificates.

            initCertificateInfo(cert,keydata,i) ;

            // Validate signatures.

            validateAndUpdateSignatures(cert,keydata) ;

            ++i ;
        }
        _pubring_last_update_time = time(NULL) ;

        RsInfo() << "  Pubring read successfully";

        if(secring_exist)
        {
            if(ops_false == ops_keyring_read_from_file(_secring, false, secring.c_str()))
            {
                RS_ERR("Cannot read secring. File seems corrupted");
                print_stacktrace();

                // We should not use exceptions they are terrible for embedded platforms
                throw std::runtime_error("OpenPGPSDKHandler::readKeyRing(): cannot read secring. File corrupted.") ;
            }
        }
        else
            RsInfo() << "  Secring file: " << pubring << " not found. Creating an empty one";

        i=0 ;
        while( (keydata = ops_keyring_get_key_by_index(_secring,i)) != NULL )
        {
            initCertificateInfo(_secret_keyring_map[ RsPgpId(keydata->key_id) ],keydata,i) ;
            ++i ;
        }
        _secring_last_update_time = time(NULL) ;

        RsInfo() << "  Secring read successfully";

        locked_readPrivateTrustDatabase() ;
        _trustdb_last_update_time = time(NULL) ;
#endif
    }

#ifdef TO_REMOVE
ops_keyring_t *OpenPGPSDKHandler::allocateOPSKeyring()
{
	ops_keyring_t *kr = (ops_keyring_t*)rs_malloc(sizeof(ops_keyring_t)) ;
    
    	if(kr == NULL)
            return NULL ;
        
	kr->nkeys = 0 ;
	kr->nkeys_allocated = 0 ;
	kr->keys = 0 ;

	return kr ;
}
#endif

bool rnp_get_passphrase_cb(rnp_ffi_t        /* ffi */,
                           void *           /* app_ctx */,
                           rnp_key_handle_t key,
                           const char *     pgp_context,
                           char             buf[],
                           size_t           buf_len)
{
	bool prev_was_bad = false ;

    RNP_BUFFER_STRUCT(key_id);
    RNP_BUFFER_STRUCT(user_id);

    rnp_key_get_keyid(key,&key_id);
    rnp_key_get_primary_uid(key,&user_id);

    RsDbg() << "GetPassphrase callback called: keyid = " << key_id << ", context = \"" << pgp_context << "\"" << " userid=\"" << user_id << "\"";

    std::string passwd;

    std::string uid_hint ;

    uid_hint += user_id;
    uid_hint += " (" + RsPgpId(key_id).toStdString()+")" ;

    bool cancelled = false ;
    passwd = PGPHandler::passphraseCallback()(NULL,"",uid_hint.c_str(),NULL,prev_was_bad,&cancelled) ;

    if(cancelled)
        return false;

    if(passwd.length() >= buf_len)
    {
        RsErr() << "Passwd is too long (" << passwd.length() << " chars). Passwd buffer should be larger (only " << buf_len << ")." ;
        return false;
    }
    memcpy(buf,passwd.c_str(),passwd.length());
    buf[passwd.length()] = 0;

    return true;
}

void RNPPGPHandler::initCertificateInfo(const rnp_key_handle_t& key_handle)
{
    // Parse certificate name
    //

    RNP_BUFFER_STRUCT(key_fprint);
    RNP_BUFFER_STRUCT(key_uid);
    RNP_BUFFER_STRUCT(key_id);
    RNP_BUFFER_STRUCT(key_alg);
    uint32_t key_bits = 0;

    rnp_key_get_fprint(key_handle, &key_fprint);
    rnp_key_get_primary_uid(key_handle, &key_uid);
    rnp_key_get_keyid(key_handle, &key_id);
    rnp_key_get_alg(key_handle, &key_alg);
    rnp_key_get_bits(key_handle, &key_bits);

    bool have_secret = false;
    rnp_key_have_secret(key_handle,&have_secret);

    RsInfo() << (have_secret?"  [SECRET]":"          ") << " type: " << key_alg << "-" << key_bits << "  Key id: " << key_id<< " fingerprint: " << key_fprint << " Username: \"" << key_uid << "\"" ;

    auto fill_cert = [key_alg,key_fprint](PGPCertificateInfo& cert,char *key_uid)
    {
        extract_name_and_comment(key_uid,cert._name,cert._comment,cert._email);

        cert._trustLvl = 1 ;	// to be setup accordingly
        cert._validLvl = 1 ;	// to be setup accordingly
        //cert._key_index = index ;
        cert._flags = 0 ;
        cert._time_stamp = 0 ;// "never" by default. Will be updated by trust database, and effective key usage.

        if(!strcmp(key_alg,"RSA"))
            cert._type = PGPCertificateInfo::PGP_CERTIFICATE_TYPE_RSA ;
        else
        {
            cert._flags |= PGPCertificateInfo::PGP_CERTIFICATE_FLAG_UNSUPPORTED_ALGORITHM ;

            if(!strcmp(key_alg,"DSA"))
                cert._type = PGPCertificateInfo::PGP_CERTIFICATE_TYPE_DSA ;
        }

        cert._fpr = RsPgpFingerprint(key_fprint) ;
    };

    fill_cert(_public_keyring_map[ RsPgpId(key_id)],key_uid) ;

    if(have_secret)
        fill_cert(_secret_keyring_map[ RsPgpId(key_id)],key_uid) ;
}

#ifdef TODO
bool OpenPGPSDKHandler::validateAndUpdateSignatures(PGPCertificateInfo& cert,const ops_keydata_t *keydata)
{
	ops_validate_result_t* result=(ops_validate_result_t*)ops_mallocz(sizeof *result);
	ops_boolean_t res = ops_validate_key_signatures(result,keydata,_pubring,cb_get_passphrase) ;

	if(res == ops_false)
	{
		static ops_boolean_t already = 0 ;
		if(!already)
		{
            RsErr() << "(WW) Error in OpenPGPSDKHandler::validateAndUpdateSignatures(). Validation failed for at least some signatures." ;
			already = 1 ;
		}
	}

	bool ret = false ;

	// Parse signers.
	//

	if(result != NULL)
		for(size_t i=0;i<result->valid_count;++i)
		{
			RsPgpId signer_id(result->valid_sigs[i].signer_id);

			if(cert.signers.find(signer_id) == cert.signers.end())
			{
				cert.signers.insert(signer_id) ;
				ret = true ;
			}
		}

	ops_validate_result_free(result) ;

	return ret ;
}
#endif

RNPPGPHandler::~RNPPGPHandler()
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.
#ifdef DEBUG_PGPHANDLER
    RsErr() << "Freeing OpenPGPSDKHandler. Deleting keyrings." ;
#endif
}

#ifdef TO_REMOVE
void OpenPGPSDKHandler::printOPSKeys() const
{
    RsErr() << "Public keyring list from OPS:" ;
    ops_keyring_list(_pubring) ;
}
#endif

bool RNPPGPHandler::haveSecretKey(const RsPgpId& id) const
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

    bool result = _secret_keyring_map.find(id) != _secret_keyring_map.end();

    RsDbg() << "HaveSecretKey: " << id << " : " << result ;
    return result;
}

bool RNPPGPHandler::GeneratePGPCertificate(const std::string& name, const std::string& email, const std::string& passphrase, RsPgpId& pgpId, const int keynumbits, std::string& errString)
{
	// Some basic checks
	
	if(!RsDiscSpace::checkForDiscSpace(RS_PGP_DIRECTORY))
	{
		errString = std::string("(EE) low disc space in pgp directory. Can't write safely to keyring.") ;
		return false ;
	}
	if(name.length() > PGP_CERTIFICATE_LIMIT_MAX_NAME_SIZE)
	{
		errString = std::string("(EE) name in certificate exceeds the maximum allowed name size") ;
		return false ;
	}
	if(email.length() > PGP_CERTIFICATE_LIMIT_MAX_EMAIL_SIZE)
	{
		errString = std::string("(EE) email in certificate exceeds the maximum allowed email size") ;
		return false ;
	}
	if(passphrase.length() > PGP_CERTIFICATE_LIMIT_MAX_PASSWD_SIZE)
	{
		errString = std::string("(EE) passphrase in certificate exceeds the maximum allowed passphrase size") ;
		return false ;
	}
	if(keynumbits % 1024 != 0)
	{
		errString = std::string("(EE) RSA key length is not a multiple of 1024") ;
		return false ;
	}

	// Now the real thing
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.
	RsStackFileLock flck(_pgp_lock_filename) ;	// lock access to PGP directory.

    NOT_IMPLEMENTED;
    return false;
#ifdef TODO
	// 1 - generate keypair - RSA-2048
	//
	ops_user_id_t uid ;
	char *s = strdup((name + " (Generated by RetroShare) <" + email + ">" ).c_str()) ;
	uid.user_id = (unsigned char *)s ;
	unsigned long int e = 65537 ; // some prime number

	ops_keydata_t *key = ops_rsa_create_selfsigned_keypair(keynumbits, e, &uid) ;

	free(s) ;

	if(!key)
		return false ;

	// 2 - save the private key encrypted to a temporary memory buffer, so as to read an encrypted key to memory

	ops_create_info_t *cinfo = NULL ;
	ops_memory_t *buf = NULL ;
	ops_setup_memory_write(&cinfo, &buf, 0);

	if(!ops_write_transferable_secret_key(key,(unsigned char *)passphrase.c_str(),passphrase.length(),ops_false,cinfo))
	{
		errString = std::string("(EE) Cannot encode secret key to memory!!") ;
		return false ;
	}

	// 3 - read the memory chunk into an encrypted keyring
	
	ops_keyring_t *tmp_secring = allocateOPSKeyring() ;

	if(! ops_keyring_read_from_mem(tmp_secring, ops_false, buf))
	{
		errString = std::string("(EE) Cannot re-read key from memory!!") ;
		return false ;
	}
	ops_teardown_memory_write(cinfo,buf);	// cleanup memory

	// 4 - copy the encrypted private key to the private keyring
	
	pgpId = RsPgpId(tmp_secring->keys[0].key_id) ;
	addNewKeyToOPSKeyring(_secring,tmp_secring->keys[0]) ;
	initCertificateInfo(_secret_keyring_map[ pgpId ],&tmp_secring->keys[0],_secring->nkeys-1) ;

#ifdef DEBUG_PGPHANDLER
    RsErr() << "Added new secret key with id " << pgpId.toStdString() << " to secret keyring." ;
#endif
	ops_keyring_free(tmp_secring) ;
	free(tmp_secring) ;

	// 5 - add key to secret keyring on disk.
	
	cinfo = NULL ;
	std::string secring_path_tmp = _secring_path + ".tmp" ;

	if(RsDirUtil::fileExists(_secring_path) && !RsDirUtil::copyFile(_secring_path,secring_path_tmp))
	{
		errString= std::string("Cannot copy secret keyring !! Disk full? Out of disk quota?") ;
		return false ;
	}
	int fd=ops_setup_file_append(&cinfo, secring_path_tmp.c_str());

	if(!ops_write_transferable_secret_key(key,(unsigned char *)passphrase.c_str(),passphrase.length(),ops_false,cinfo))
	{
		errString= std::string("Cannot encode secret key to disk!! Disk full? Out of disk quota?") ;
		return false ;
	}
	ops_teardown_file_write(cinfo,fd) ;

	if(!RsDirUtil::renameFile(secring_path_tmp,_secring_path))
	{
		errString= std::string("Cannot rename tmp secret key file ") + secring_path_tmp + " into " + _secring_path +". Disk error?" ;
		return false ;
	}

	// 6 - copy the public key to the public keyring on disk
	
	cinfo = NULL ;
	std::string pubring_path_tmp = _pubring_path + ".tmp" ;

	if(RsDirUtil::fileExists(_pubring_path) && !RsDirUtil::copyFile(_pubring_path,pubring_path_tmp))
	{
		errString= std::string("Cannot encode secret key to disk!! Disk full? Out of disk quota?") ;
		return false ;
	}
	fd=ops_setup_file_append(&cinfo, pubring_path_tmp.c_str());

	if(!ops_write_transferable_public_key(key, ops_false, cinfo))
	{
		errString=std::string("Cannot encode secret key to memory!!") ;
		return false ;
	}
	ops_teardown_file_write(cinfo,fd) ;

	if(!RsDirUtil::renameFile(pubring_path_tmp,_pubring_path))
	{
		errString= std::string("Cannot rename tmp public key file ") + pubring_path_tmp + " into " + _pubring_path +". Disk error?" ;
		return false ;
	}
	// 7 - clean
	ops_keydata_free(key) ;

	// 8 - re-read the key from the public keyring, and add it to memory.

	_pubring_last_update_time = 0 ; // force update pubring from disk.
	locked_syncPublicKeyring() ;	

#ifdef DEBUG_PGPHANDLER
    RsErr() << "Added new public key with id " << pgpId.toStdString() << " to public keyring." ;
#endif

	// 9 - Update some flags.

	privateTrustCertificate(pgpId,PGPCertificateInfo::PGP_CERTIFICATE_TRUST_ULTIMATE) ;

	return true ;
#endif
}

#ifdef TO_REMOVE
std::string OpenPGPSDKHandler::makeRadixEncodedPGPKey(const ops_keydata_t *key,bool include_signatures)
{
   ops_create_info_t* cinfo;
	ops_memory_t *buf = NULL ;
   ops_setup_memory_write(&cinfo, &buf, 0);
	ops_boolean_t armoured = ops_true ;

	if(key->type == OPS_PTAG_CT_PUBLIC_KEY)
	{
		if(ops_write_transferable_public_key_from_packet_data(key,armoured,cinfo) != ops_true)
			return "ERROR: This key cannot be processed by RetroShare because\nDSA certificates are not yet handled." ;
	}
	else if(key->type == OPS_PTAG_CT_ENCRYPTED_SECRET_KEY)
	{
		if(ops_write_transferable_secret_key_from_packet_data(key,armoured,cinfo) != ops_true)
			return "ERROR: This key cannot be processed by RetroShare because\nDSA certificates are not yet handled." ;
	}
	else
	{
        ops_create_info_delete(cinfo);
        RsErr() << "Unhandled key type " << key->type ;
		return "ERROR: Cannot write key. Unhandled key type. " ;
	}

	ops_writer_close(cinfo) ;

	std::string res((char *)ops_memory_get_data(buf),ops_memory_get_length(buf)) ;
   ops_teardown_memory_write(cinfo,buf);

	if(!include_signatures)
	{
		std::string tmp ;
		if(PGPKeyManagement::createMinimalKey(res,tmp) )
			res = tmp ;
	}

	return res ;
}

const ops_keydata_t *OpenPGPSDKHandler::locked_getSecretKey(const RsPgpId& id) const
{
	std::map<RsPgpId,PGPCertificateInfo>::const_iterator res = _secret_keyring_map.find(id) ;

	if(res == _secret_keyring_map.end())
		return NULL ;
	else
		return ops_keyring_get_key_by_index(_secring,res->second._key_index) ;
}
const ops_keydata_t *OpenPGPSDKHandler::locked_getPublicKey(const RsPgpId& id,bool stamp_the_key) const
{
	std::map<RsPgpId,PGPCertificateInfo>::const_iterator res = _public_keyring_map.find(id) ;

	if(res == _public_keyring_map.end())
		return NULL ;
	else
	{
		if(stamp_the_key)		// Should we stamp the key as used?
		{
			static rstime_t last_update_db_because_of_stamp = 0 ;
			rstime_t now = time(NULL) ;

			res->second._time_stamp = now ;

			if(now > last_update_db_because_of_stamp + 3600) // only update database once every hour. No need to do it more often.
			{
				_trustdb_changed = true ;
				last_update_db_because_of_stamp = now ;
			}
		}
		return ops_keyring_get_key_by_index(_pubring,res->second._key_index) ;
	}
}
#endif

std::string RNPPGPHandler::SaveCertificateToString(const RsPgpId& id,bool include_signatures) const
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.
    RsErr() << " function " << __PRETTY_FUNCTION__ << " Not implemented yet." << std::endl;
    return std::string();
#ifdef TODO
	const ops_keydata_t *key = locked_getPublicKey(id,false) ;

	if(key == NULL)
	{
        RsErr() << "Cannot output key " << id.toStdString() << ": not found in keyring." ;
		return "" ;
	}

	return makeRadixEncodedPGPKey(key,include_signatures) ;
#endif
}

bool RNPPGPHandler::exportPublicKey( const RsPgpId& id, unsigned char*& mem_block, size_t& mem_size, bool armoured, bool include_signatures ) const
{
    RS_STACK_MUTEX(pgphandlerMtx);

    mem_block = nullptr; mem_size = 0; // clear just in case

    RNP_OUTPUT_STRUCT(output);
    RNP_KEY_HANDLE_STRUCT(key_handle);

    try
    {
        if(rnp_output_to_memory(&output,0) != RNP_SUCCESS)
            throw std::runtime_error("Cannot create output structure");

        if(rnp_locate_key(mRnpFfi,"keyid",id.toStdString().c_str(),&key_handle))
            throw std::runtime_error("Cannot find PGP key " + id.toStdString() + " to export.");

        uint32_t flags = armoured? RNP_KEY_EXPORT_ARMORED : 0;
        flags |= RNP_KEY_EXPORT_PUBLIC;

        if(rnp_key_export(key_handle, output, flags) != RNP_SUCCESS)
            throw std::runtime_error("Key export failed ID=" + id.toStdString() + " to export.");

        if(rnp_output_memory_get_buf(output, &mem_block, &mem_size, true) != RNP_SUCCESS)
            throw std::runtime_error("Cannot extract key data from output structure.");

        if(!include_signatures)
        {
            size_t new_size;
            PGPKeyManagement::findLengthOfMinimalKey(mem_block, mem_size, new_size);
            mem_size = new_size;
        }
        return true;
    }
    catch (std::exception& e)
    {
        RS_ERR(std::string("Cannot export key: ")+e.what());
        return false;
    }
}

bool RNPPGPHandler::exportGPGKeyPair(const std::string& filename,const RsPgpId& exported_key_id) const
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

    NOT_IMPLEMENTED;
    return false;
#ifdef TODO
	const ops_keydata_t *pubkey = locked_getPublicKey(exported_key_id,false) ;

	if(pubkey == NULL)
	{
        RsErr() << "Cannot output key " << exported_key_id.toStdString() << ": not found in public keyring." ;
		return false ;
	}
	const ops_keydata_t *seckey = locked_getSecretKey(exported_key_id) ;

	if(seckey == NULL)
	{
        RsErr() << "Cannot output key " << exported_key_id.toStdString() << ": not found in secret keyring." ;
		return false ;
	}

	FILE *f = RsDirUtil::rs_fopen(filename.c_str(),"w") ;
	if(f == NULL)
	{
        RsErr() << "Cannot output key " << exported_key_id.toStdString() << ": file " << filename << " cannot be written. Please check for permissions, quotas, disk space." ;
		return false ;
	}

	fprintf(f,"%s\n", makeRadixEncodedPGPKey(pubkey,true).c_str()) ; 
	fprintf(f,"%s\n", makeRadixEncodedPGPKey(seckey,true).c_str()) ; 

	fclose(f) ;
	return true ;
#endif
}

bool RNPPGPHandler::exportGPGKeyPairToString( std::string& data, const RsPgpId& exportedKeyId, bool includeSignatures, std::string& errorMsg ) const
{
	RS_STACK_MUTEX(pgphandlerMtx);

    NOT_IMPLEMENTED;
    return false;
#ifdef TODO
	const ops_keydata_t *pubkey = locked_getPublicKey(exportedKeyId,false);

	if(!pubkey)
	{
		errorMsg = "Cannot output key " + exportedKeyId.toStdString() +
		           ": not found in public keyring.";
		return false;
	}
	const ops_keydata_t *seckey = locked_getSecretKey(exportedKeyId);

	if(!seckey)
	{
		errorMsg = "Cannot output key " + exportedKeyId.toStdString() +
		           ": not found in secret keyring.";
		return false;
	}

	data  = makeRadixEncodedPGPKey(pubkey, includeSignatures);
	data += "\n";
	data += makeRadixEncodedPGPKey(seckey, includeSignatures);
	data += "\n";
	return true;
#endif
}

bool RNPPGPHandler::getGPGDetailsFromBinaryBlock(const unsigned char *mem_block,size_t mem_size,RsPgpId& key_id, std::string& name, std::list<RsPgpId>& signers) const
{
    try
    {
        rnp_input_t input; 	// no need to delete since we don't copy
        if(rnp_input_from_memory(&input,(uint8_t*)mem_block,mem_size,false) != RNP_SUCCESS)
            throw std::runtime_error("Cannot open supplied memory block. Memory access error.") ;

        rnp_ffi_t tmp_ffi;
        rnp_ffi_create(&tmp_ffi,RNP_KEYSTORE_GPG,RNP_KEYSTORE_GPG);

        if(rnp_load_keys(tmp_ffi, RNP_KEYSTORE_GPG, input, RNP_LOAD_SAVE_PUBLIC_KEYS) != RNP_SUCCESS)
            throw std::runtime_error("Cannot interpret supplied memory block as public key.") ;

        size_t pub_count;
        rnp_get_public_key_count(tmp_ffi,&pub_count);

        if(pub_count == 0)
            throw std::runtime_error("Supplied memory block does not contain any key");
        else if(pub_count > 1)
            throw std::runtime_error("Supplied memory block contain more than one key (" + RsUtil::NumberToString(pub_count) + " found)");

        rnp_identifier_iterator_t it;
        rnp_identifier_iterator_create(tmp_ffi,&it,RNP_IDENTIFIER_KEYID);

        const char *key_identifier = nullptr;
        if(rnp_identifier_iterator_next(it,&key_identifier) != RNP_SUCCESS)
            throw std::runtime_error("Error while reaching first key");

        rnp_identifier_iterator_destroy(it);

        key_id = RsPgpId(key_identifier);

        RNP_KEY_HANDLE_STRUCT(key_handle);
        if(rnp_locate_key(mRnpFfi,RNP_IDENTIFIER_KEYID,key_identifier,&key_handle) != RNP_SUCCESS)
            throw std::runtime_error("Error while reaching first key data");

        RNP_BUFFER_STRUCT(uid);

        if(rnp_key_get_primary_uid(key_handle,&uid) != RNP_SUCCESS)
            throw std::runtime_error("Error while getting key uid");

        name = std::string(uid);
        size_t signature_count = 0;

        if(rnp_key_get_signature_count(key_handle,&signature_count) != RNP_SUCCESS)
            throw std::runtime_error("Error getting signature count");

        for(size_t i=0;i<signature_count;++i)
        {
            RNP_SIGNATURE_HANDLE_STRUCT(sig);

            if(rnp_key_get_signature_at(key_handle,i,&sig) != RNP_SUCCESS)
                throw std::runtime_error("Error getting signature data");

            RNP_BUFFER_STRUCT(suid);

            if(rnp_signature_get_keyid(sig,&suid) != RNP_SUCCESS)
                throw std::runtime_error("Error getting signature key id");

            signers.push_back(RsPgpId(suid));
        }
        return true;
    }
    catch(std::exception& e)
    {
        RS_ERR("ERROR: ",e.what());
        return false;
    }
}

bool RNPPGPHandler::importGPGKeyPair(const std::string& filename,RsPgpId& imported_key_id,std::string& import_error)
{
	import_error = "" ;

	// 1 - Test for file existance
	//
	FILE *ftest = RsDirUtil::rs_fopen(filename.c_str(),"r") ;

	if(ftest == NULL)
	{
		import_error = "Cannot open file " + filename + " for read. Please check access permissions." ;
		return false ;
	}

	fclose(ftest) ;

    NOT_IMPLEMENTED;
#ifdef TODO
	// 2 - Read keyring from supplied file.
	//
	ops_keyring_t *tmp_keyring = allocateOPSKeyring();

	if(ops_false == ops_keyring_read_from_file(tmp_keyring, ops_true, filename.c_str()))
	{
        import_error = "OpenPGPSDKHandler::readKeyRing(): cannot read key file. File corrupted?" ;
        free(tmp_keyring);
		return false ;
	}

    return checkAndImportKeyPair(tmp_keyring, imported_key_id, import_error);
#endif
}

bool RNPPGPHandler::importGPGKeyPairFromString(const std::string &data, RsPgpId &imported_key_id, std::string &import_error)
{
    NOT_IMPLEMENTED;
#ifdef TODO
    import_error = "" ;

    ops_memory_t* mem = ops_memory_new();
    ops_memory_add(mem, (unsigned char*)data.data(), data.length());

    ops_keyring_t *tmp_keyring = allocateOPSKeyring();

    if(ops_false == ops_keyring_read_from_mem(tmp_keyring, ops_true, mem))
    {
        import_error = "OpenPGPSDKHandler::importGPGKeyPairFromString(): cannot parse key data" ;
        free(tmp_keyring);
        return false ;
    }
    return checkAndImportKeyPair(tmp_keyring, imported_key_id, import_error);
#endif
}

#ifdef TO_REMOVE
bool OpenPGPSDKHandler::checkAndImportKeyPair(ops_keyring_t *tmp_keyring, RsPgpId &imported_key_id, std::string &import_error)
{
    if(tmp_keyring == 0)
    {
        import_error = "OpenPGPSDKHandler::checkAndImportKey(): keyring is null" ;
        return false;
    }

	if(tmp_keyring->nkeys != 2)
	{
        import_error = "OpenPGPSDKHandler::importKeyPair(): file does not contain a valid keypair." ;
		if(tmp_keyring->nkeys > 2)
			import_error += "\nMake sure that your key is a RSA key (DSA is not yet supported) and does not contain subkeys (not supported yet).";
		return false ;
	}

	// 3 - Test that keyring contains a valid keypair.
	//
	const ops_keydata_t *pubkey = NULL ;
	const ops_keydata_t *seckey = NULL ;

	if(tmp_keyring->keys[0].type == OPS_PTAG_CT_PUBLIC_KEY) 
		pubkey = &tmp_keyring->keys[0] ;
	else if(tmp_keyring->keys[0].type == OPS_PTAG_CT_ENCRYPTED_SECRET_KEY) 
		seckey = &tmp_keyring->keys[0] ;
	else
	{
		import_error = "Unrecognised key type in key file for key #0. Giving up." ;
        RsErr() << "Unrecognised key type " << tmp_keyring->keys[0].type << " in key file for key #0. Giving up." ;
		return false ;
	}
	if(tmp_keyring->keys[1].type == OPS_PTAG_CT_PUBLIC_KEY) 
		pubkey = &tmp_keyring->keys[1] ;
	else if(tmp_keyring->keys[1].type == OPS_PTAG_CT_ENCRYPTED_SECRET_KEY) 
		seckey = &tmp_keyring->keys[1] ;
	else
	{
		import_error = "Unrecognised key type in key file for key #1. Giving up." ;
        RsErr() << "Unrecognised key type " << tmp_keyring->keys[1].type << " in key file for key #1. Giving up." ;
		return false ;
	}

	if(pubkey == nullptr || seckey == nullptr || pubkey == seckey)
	{
		import_error = "File does not contain a public and a private key. Sorry." ;
		return false ;
	}
	if(memcmp( pubkey->fingerprint.fingerprint,
	           seckey->fingerprint.fingerprint,
	           RsPgpFingerprint::SIZE_IN_BYTES ) != 0)
	{
		import_error = "Public and private keys do nt have the same fingerprint. Sorry!" ;
		return false ;
	}
	if(pubkey->key.pkey.version != 4)
	{
		import_error = "Public key is not version 4. Rejected!" ;
		return false ;
	}

	// 4 - now check self-signature for this keypair. For this we build a dummy keyring containing only the key.
	//
	ops_validate_result_t *result=(ops_validate_result_t*)ops_mallocz(sizeof *result);

	ops_keyring_t dummy_keyring ;
	dummy_keyring.nkeys=1 ;
	dummy_keyring.nkeys_allocated=1 ;
	dummy_keyring.keys=const_cast<ops_keydata_t*>(pubkey) ;

	ops_validate_key_signatures(result, const_cast<ops_keydata_t*>(pubkey), &dummy_keyring, cb_get_passphrase) ;
	
	// Check that signatures contain at least one certification from the user id.
	//
	bool found = false ;

	for(uint32_t i=0;i<result->valid_count;++i)
		if(!memcmp(
		            static_cast<uint8_t*>(result->valid_sigs[i].signer_id),
		            pubkey->key_id,
		            RsPgpId::SIZE_IN_BYTES ))
		{
			found = true ;
			break ;
		}

	if(!found)
	{
		import_error = "Cannot validate self signature for the imported key. Sorry." ;
		return false ;
	}
	ops_validate_result_free(result);

	if(!RsDiscSpace::checkForDiscSpace(RS_PGP_DIRECTORY))
	{
		import_error = std::string("(EE) low disc space in pgp directory. Can't write safely to keyring.") ;
		return false ;
	}
	// 5 - All test passed. Adding key to keyring.
	//
	{
		RsStackMutex mtx(pgphandlerMtx) ;					// lock access to PGP memory structures.

		imported_key_id = RsPgpId(pubkey->key_id) ;

		if(locked_getSecretKey(imported_key_id) == NULL)
		{
			RsStackFileLock flck(_pgp_lock_filename) ;	// lock access to PGP directory.

			ops_create_info_t *cinfo = NULL ;

			// Make a copy of the secret keyring
			//
			std::string secring_path_tmp = _secring_path + ".tmp" ;
			if(RsDirUtil::fileExists(_secring_path) && !RsDirUtil::copyFile(_secring_path,secring_path_tmp)) 
			{
				import_error = "(EE) Cannot write secret key to disk!! Disk full? Out of disk quota. Keyring will be left untouched." ;
				return false ;
			}

			// Append the new key

			int fd=ops_setup_file_append(&cinfo, secring_path_tmp.c_str());

			if(!ops_write_transferable_secret_key_from_packet_data(seckey,ops_false,cinfo))
			{
				import_error = "(EE) Cannot encode secret key to disk!! Disk full? Out of disk quota?" ;
				return false ;
			}
			ops_teardown_file_write(cinfo,fd) ;

			// Rename the new keyring to overwrite the old one.
			//
			if(!RsDirUtil::renameFile(secring_path_tmp,_secring_path))
			{
				import_error = "  (EE) Cannot move temp file " + secring_path_tmp + ". Bad write permissions?" ;
				return false ;
			}

			addNewKeyToOPSKeyring(_secring,*seckey) ;
			initCertificateInfo(_secret_keyring_map[ imported_key_id ],seckey,_secring->nkeys-1) ;
		}
		else
			import_error = "Private key already exists! Not importing it again." ;

		if(locked_addOrMergeKey(_pubring,_public_keyring_map,pubkey))
			_pubring_changed = true ;
	}

	// 6 - clean
	//
	ops_keyring_free(tmp_keyring) ;
    free(tmp_keyring);

    // write public key to disk
    syncDatabase();

	return true ;
}

void OpenPGPSDKHandler::addNewKeyToOPSKeyring(ops_keyring_t *kr,const ops_keydata_t& key)
{
	if(kr->nkeys >= kr->nkeys_allocated)
	{
		kr->keys = (ops_keydata_t *)realloc(kr->keys,(kr->nkeys+1)*sizeof(ops_keydata_t)) ; 
		kr->nkeys_allocated = kr->nkeys+1;
	}
	memset(&kr->keys[kr->nkeys],0,sizeof(ops_keydata_t)) ;
	ops_keydata_copy(&kr->keys[kr->nkeys],&key) ;
	kr->nkeys++ ;
}
#endif

bool RNPPGPHandler::LoadCertificate(const unsigned char *data,uint32_t data_len,bool armoured,RsPgpId& id,std::string& error_string)
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.
#ifdef DEBUG_PGPHANDLER
    RsErr() << "Reading new key from string: " ;
#endif
    NOT_IMPLEMENTED;
#ifdef TODO
	ops_keyring_t *tmp_keyring = allocateOPSKeyring();
	ops_memory_t *mem = ops_memory_new() ;
	ops_memory_add(mem,data,data_len) ;

	if(!ops_keyring_read_from_mem(tmp_keyring,armoured,mem))
	{
		ops_keyring_free(tmp_keyring) ;
		free(tmp_keyring) ;
		ops_memory_release(mem) ;
		free(mem) ;

        RsErr() << "Could not read key. Format error?" ;
		error_string = std::string("Could not read key. Format error?") ;
		return false ;
	}
	ops_memory_release(mem) ;
	free(mem) ;
	error_string.clear() ;

	// Check that there is exactly one key in this data packet.
	//
	if(tmp_keyring->nkeys != 1)
	{
        RsErr() << "Loaded certificate contains more than one PGP key. This is not allowed." ;
		error_string = "Loaded certificate contains more than one PGP key. This is not allowed." ;
		return false ;
	}

	const ops_keydata_t *keydata = ops_keyring_get_key_by_index(tmp_keyring,0);

	// Check that the key is a version 4 key
	//
	if(keydata->key.pkey.version != 4)
	{
		error_string = "Public key is not version 4. Rejected!" ;
        RsErr() << "Received a key with unhandled version number (" << keydata->key.pkey.version << ")" ;
		return false ;
	}

	// Check that the key is correctly self-signed.
	//
	ops_validate_result_t* result=(ops_validate_result_t*)ops_mallocz(sizeof *result);

    ops_validate_key_signatures(result,keydata,tmp_keyring,cb_get_passphrase) ;

	bool found = false ;

	for(uint32_t i=0;i<result->valid_count;++i)
		if(!memcmp(
		            static_cast<uint8_t*>(result->valid_sigs[i].signer_id),
		            keydata->key_id,
		            RsPgpId::SIZE_IN_BYTES ))
		{
			found = true ;
			break ;
		}

	if(!found)
	{
		error_string = "This key is not self-signed. This is required by Retroshare." ;
        RsErr() <<   "This key is not self-signed. This is required by Retroshare." ;
		ops_validate_result_free(result);
		return false ;
	}
	ops_validate_result_free(result);

#ifdef DEBUG_PGPHANDLER
    RsErr() << "  Key read correctly: " ;
	ops_keyring_list(tmp_keyring) ;
#endif

	int i=0 ;

	while( (keydata = ops_keyring_get_key_by_index(tmp_keyring,i++)) != NULL )
		if(locked_addOrMergeKey(_pubring,_public_keyring_map,keydata)) 
		{
			_pubring_changed = true ;
#ifdef DEBUG_PGPHANDLER
            RsErr() << "  Added the key in the main public keyring." ;
#endif
		}
		else
            RsErr() << "Key already in public keyring." ;
	
	if(tmp_keyring->nkeys > 0)
		id = RsPgpId(tmp_keyring->keys[0].key_id) ;
	else
		return false ;

	ops_keyring_free(tmp_keyring) ;
	free(tmp_keyring) ;

	_pubring_changed = true ;

	return true ;
#endif
}

#ifdef TO_REMOVE
bool OpenPGPSDKHandler::locked_addOrMergeKey(ops_keyring_t *keyring,std::map<RsPgpId,PGPCertificateInfo>& kmap,const ops_keydata_t *keydata)
{
	bool ret = false ;
	RsPgpId id(keydata->key_id) ;

#ifdef DEBUG_PGPHANDLER
    RsErr() << "AddOrMergeKey():" ;
    RsErr() << "  id: " << id.toStdString() ;
#endif

	// See if the key is already in the keyring
	const ops_keydata_t *existing_key = NULL;
	std::map<RsPgpId,PGPCertificateInfo>::const_iterator res = kmap.find(id) ;

	// Checks that
	// 	- the key is referenced by keyid
	// 	- the map is initialized
	// 	- the fingerprint matches!
	//
	if(res == kmap.end() || (existing_key = ops_keyring_get_key_by_index(keyring,res->second._key_index)) == NULL)
	{
#ifdef DEBUG_PGPHANDLER
        RsErr() << "  Key is new. Adding it to keyring" ;
#endif
		addNewKeyToOPSKeyring(keyring,*keydata) ; // the key is new.
		initCertificateInfo(kmap[id],keydata,keyring->nkeys-1) ;
		existing_key = &(keyring->keys[keyring->nkeys-1]) ;
		ret = true ;
	}
	else
	{
		if(memcmp( existing_key->fingerprint.fingerprint,
		           keydata->fingerprint.fingerprint,
		           RsPgpFingerprint::SIZE_IN_BYTES ))
		{
            RsErr() << "(EE) attempt to merge key with identical id, but different fingerprint!" ;
			return false ;
		}

#ifdef DEBUG_PGPHANDLER
        RsErr() << "  Key exists. Merging signatures." ;
#endif
		ret = mergeKeySignatures(const_cast<ops_keydata_t*>(existing_key),keydata) ;

		if(ret)
			initCertificateInfo(kmap[id],existing_key,res->second._key_index) ;
	}

	if(ret)
	{
		validateAndUpdateSignatures(kmap[id],existing_key) ;
		kmap[id]._time_stamp = time(NULL) ;
	}

	return ret ;
}
#endif

bool RNPPGPHandler::encryptTextToFile(const RsPgpId& key_id,const std::string& text,const std::string& outfile)
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

    rnp_input_t input ;
    RNP_OUTPUT_STRUCT(output);
    RNP_OP_ENCRYPT_STRUCT(encrypt);

    if(rnp_input_from_memory(&input, (uint8_t*)text.c_str(),text.size(),false) != RNP_SUCCESS)
        throw std::runtime_error("Cannot create input memory structure");

    if(rnp_output_to_path(&output, outfile.c_str()) != RNP_SUCCESS)
        throw std::runtime_error("Cannot create output structure");

    if(rnp_op_encrypt_create(&encrypt, mRnpFfi, input, output) != RNP_SUCCESS)
        throw std::runtime_error("Cannot create encryption structure");

    rnp_op_encrypt_set_armor(encrypt, true);
    rnp_op_encrypt_set_file_name(encrypt, nullptr);
    rnp_op_encrypt_set_file_mtime(encrypt, (uint32_t) time(NULL));
    rnp_op_encrypt_set_compression(encrypt, "ZIP", 6);
    rnp_op_encrypt_set_cipher(encrypt, RNP_ALGNAME_AES_256);
    rnp_op_encrypt_set_aead(encrypt, "None");

    RNP_KEY_HANDLE_STRUCT(key);

    if(rnp_locate_key(mRnpFfi, "keyid", key_id.toStdString().c_str(), &key) != RNP_SUCCESS)
        throw std::runtime_error("Cannot locate destination key " + key_id.toStdString() + " for encryption");

    if(rnp_op_encrypt_add_recipient(encrypt, key) != RNP_SUCCESS)
        throw std::runtime_error("Failed to add recipient " + key_id.toStdString() + " for encryption");

    // Execute encryption operation

    if(rnp_op_encrypt_execute(encrypt) != RNP_SUCCESS)
        throw std::runtime_error("Encryption operation failed.");

    return true;
}

bool RNPPGPHandler::encryptDataBin(const RsPgpId& key_id,const void *data, const uint32_t len, unsigned char *encrypted_data, unsigned int *encrypted_data_len)
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

    NOT_IMPLEMENTED;
#ifdef TODO
	const ops_keydata_t *public_key = locked_getPublicKey(key_id,true) ;

	if(public_key == NULL)
	{
        RsErr() << "Cannot get public key of id " << key_id.toStdString() ;
		return false ;
	}

	if(public_key->type != OPS_PTAG_CT_PUBLIC_KEY)
	{
        RsErr() << "OpenPGPSDKHandler::encryptTextToFile(): ERROR: supplied id did not return a public key!" ;
		return false ;
	}
	if(public_key->key.pkey.algorithm != OPS_PKA_RSA)
	{
        RsErr() << "OpenPGPSDKHandler::encryptTextToFile(): ERROR: supplied key id " << key_id.toStdString() << " is not an RSA key (DSA for instance, is not supported)!" ;
		return false ;
	}
	ops_create_info_t *info;
	ops_memory_t *buf = NULL ;
   ops_setup_memory_write(&info, &buf, 0);
	bool res = true;

	if(!ops_encrypt_stream(info, public_key, NULL, ops_false, ops_false))
	{
        RsErr() << "Encryption failed." ;
		res = false ;
	}

	ops_write(data,len,info);
	ops_writer_close(info);
	ops_create_info_delete(info);

	int tlen = ops_memory_get_length(buf) ;

	if( (int)*encrypted_data_len >= tlen)
	{
		if(res)
		{
			memcpy(encrypted_data,ops_memory_get_data(buf),tlen) ;
			*encrypted_data_len = tlen ;
			res = true ;
		}
	}
	else
	{
        RsErr() << "Not enough room to fit encrypted data. Size given=" << *encrypted_data_len << ", required=" << tlen ;
		res = false ;
	}

	ops_memory_release(buf) ;
	free(buf) ;

	return res ;
#endif
}

bool RNPPGPHandler::decryptDataBin(const RsPgpId& /*key_id*/,const void *encrypted_data, const uint32_t encrypted_len, unsigned char *data, unsigned int *data_len)
{
    RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

    /* set the password provider */
    rnp_ffi_set_pass_provider(mRnpFfi, rnp_get_passphrase_cb, NULL);

    /* create file input and memory output objects for the encrypted message and decrypted messages */

    RNP_INPUT_STRUCT(input);
    RNP_OUTPUT_STRUCT(output);

    try
    {
        if (rnp_input_from_memory(&input, (uint8_t*)encrypted_data,encrypted_len,false) != RNP_SUCCESS)
            throw std::runtime_error("cannot read input encrypted data") ;

        if (rnp_output_to_memory(&output,0) != RNP_SUCCESS)
            throw std::runtime_error("cannot create output decrypted data structure") ;

        if (rnp_decrypt(mRnpFfi, input, output) != RNP_SUCCESS)
            throw std::runtime_error("decryption failed.");

        uint8_t *output_buf = nullptr;
        size_t output_len = 0;

        /* get the decrypted message from the output structure */
        if (rnp_output_memory_get_buf(output, &output_buf, &output_len, false) != RNP_SUCCESS)
            throw std::runtime_error("decryption failed.");

        if(output_len > *data_len)
            throw std::runtime_error("Decrypted data is too large for the supplied buffer (" + RsUtil::NumberToString(output_len) + " vs. " + RsUtil::NumberToString(*data_len) + " bytes).");

        memcpy(data,output_buf,output_len);
        *data_len = output_len;
        return true;
    }
    catch(std::exception& e)
    {
        RsErr() << "DecryptMemory: ERROR: " << e.what() ;
        return false;
    }
}

bool RNPPGPHandler::decryptTextFromFile(const RsPgpId&,std::string& text,const std::string& inputfile)
{
    RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

    /* set the password provider */
    rnp_ffi_set_pass_provider(mRnpFfi, rnp_get_passphrase_cb, NULL);

    /* create file input and memory output objects for the encrypted message and decrypted
     * message */

    RNP_INPUT_STRUCT(input);
    RNP_OUTPUT_STRUCT(output);

    try
    {
        if (rnp_input_from_path(&input, inputfile.c_str()) != RNP_SUCCESS)
            throw std::runtime_error("cannot read input file to decrypt \"" + inputfile + "\"") ;

        if (rnp_output_to_memory(&output,0) != RNP_SUCCESS)
            throw std::runtime_error("cannot create output decrypted data structure") ;

        if (rnp_decrypt(mRnpFfi, input, output) != RNP_SUCCESS)
            throw std::runtime_error("decryption failed.");

        uint8_t *output_buf = nullptr;
        size_t output_len = 0;

        /* get the decrypted message from the output structure */
        if (rnp_output_memory_get_buf(output, &output_buf, &output_len, false) != RNP_SUCCESS)
            throw std::runtime_error("decryption failed.");

        text = std::string((char *)output_buf,output_len);
        return true;
    }
    catch(std::exception& e)
    {
        RsErr() << "DecryptMemory: ERROR: " << e.what() ;
        return false;
    }
}

bool RNPPGPHandler::SignDataBin(const RsPgpId& id,const void *data, const uint32_t len, unsigned char *sign, unsigned int *signlen,bool use_raw_signature, std::string reason /* = "" */)
{
    // passwd provider function.

    try
    {
        rnp_ffi_set_pass_provider(mRnpFfi, rnp_get_passphrase_cb, NULL);

        RNP_INPUT_STRUCT(data_input);
        RNP_OUTPUT_STRUCT(signature_output);
        RNP_OP_SIGN_STRUCT(signature);

        if(rnp_input_from_memory(&data_input, (uint8_t *) data, len, false) != RNP_SUCCESS)
            throw std::runtime_error("failed to create input object\n");

        if (rnp_output_to_memory(&signature_output, 0) != RNP_SUCCESS)
            throw std::runtime_error("failed to create output object");

        // initialize and configure signature parameters

        if(rnp_op_sign_detached_create(&signature, mRnpFfi, data_input, signature_output) != RNP_SUCCESS)
            throw std::runtime_error("failed to create sign operation");

        /* armor, file name, compression */
        rnp_op_sign_set_armor(signature, false);
        rnp_op_sign_set_file_mtime(signature, (uint32_t) time(NULL));
        rnp_op_sign_set_compression(signature, "ZIP", 6);
        rnp_op_sign_set_creation_time(signature, (uint32_t) time(NULL));   // now
        rnp_op_sign_set_expiration_time(signature, 0);				       // 0 = never expires
        rnp_op_sign_set_hash(signature, RNP_ALGNAME_SHA256);

        // now add signatures. First locate the signing key, then add and setup signature

        RNP_KEY_HANDLE_STRUCT(key);

        if (rnp_locate_key(mRnpFfi, "keyid", id.toStdString().c_str(), &key) != RNP_SUCCESS)
            throw std::runtime_error("failed to locate signing key " + id.toStdString());

        // we do not need pointer to the signature so passing NULL as the last parameter

        if (rnp_op_sign_add_signature(signature, key, nullptr) != RNP_SUCCESS)
            throw std::runtime_error("failed to add signature for key " + id.toStdString());

        // Finally do signing

        if (rnp_op_sign_execute(signature) != RNP_SUCCESS)
            throw std::runtime_error("failed to sign");

        // Now get the result

        uint8_t *output_buf = nullptr;
        size_t output_len = 0;

        /* get the decrypted message from the output structure */
        if (rnp_output_memory_get_buf(signature_output, &output_buf, &output_len, false) != RNP_SUCCESS)
            throw std::runtime_error("Cannot retrieve signature data.");

        if(output_len > *signlen)
            throw std::runtime_error("Decrypted data is too large for the supplied buffer (" + RsUtil::NumberToString(output_len) + " vs. " + RsUtil::NumberToString(*signlen) + " bytes).");

        memcpy(sign,output_buf,output_len);
        *signlen = output_len;

#ifdef DEBUG_RNP
        RsErr() << "Signed with key " << id.toStdString() << ", length " << std::dec << *signlen << ", literal data length = " << len ;
        RsErr() << "Signature body: " << RsUtil::BinToHex((unsigned char *)data,len);
        RsErr() << "Data: " << RsUtil::BinToHex( (unsigned char *)sign,*signlen) ;
        RsErr() ;
#endif
        return true ;
    }
    catch (std::exception& e)
    {
        RsErr() << __PRETTY_FUNCTION__ << ": ERROR" ;
        RsErr() << "Signature failed: " << e.what();
        return false;
    }
}

bool RNPPGPHandler::privateSignCertificate(const RsPgpId& ownId,const RsPgpId& id_of_key_to_sign)
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

    NOT_IMPLEMENTED;
#ifdef TODO
	ops_keydata_t *key_to_sign = const_cast<ops_keydata_t*>(locked_getPublicKey(id_of_key_to_sign,true)) ;

	if(key_to_sign == NULL)
	{
        RsErr() << "Cannot sign: no public key with id " << id_of_key_to_sign.toStdString() ;
		return false ;
	}

	// 1 - get decrypted secret key
	//
	const ops_keydata_t *skey = locked_getSecretKey(ownId) ;

	if(!skey)
	{
        RsErr() << "Cannot sign: no secret key with id " << ownId.toStdString() ;
		return false ;
	}
	const ops_keydata_t *pkey = locked_getPublicKey(ownId,true) ;

	if(!pkey)
	{
        RsErr() << "Cannot sign: no public key with id " << ownId.toStdString() ;
		return false ;
	}

	bool cancelled = false;
	std::string passphrase = _passphrase_callback(NULL,"",RsPgpId(skey->key_id).toStdString().c_str(),"Please enter passwd for encrypting your key : ",false,&cancelled) ;

	ops_secret_key_t *secret_key = ops_decrypt_secret_key_from_data(skey,passphrase.c_str()) ;

    if(cancelled)
    {
        RsErr() << "Key cancelled by used." ;
        return false ;
    }
    if(!secret_key)
	{
        RsErr() << "Key decryption went wrong. Wrong passwd?" ;
		return false ;
	}

	// 2 - then do the signature.

	if(!ops_sign_key(key_to_sign,pkey->key_id,secret_key)) 
	{
        RsErr() << "Key signature went wrong. Wrong passwd?" ;
		return false ;
	}

	// 3 - free memory
	//
	ops_secret_key_free(secret_key) ;
	free(secret_key) ;

	_pubring_changed = true ;

	// 4 - update signatures.
	//
	PGPCertificateInfo& cert(_public_keyring_map[ id_of_key_to_sign ]) ;
	validateAndUpdateSignatures(cert,key_to_sign) ;
	cert._flags |= PGPCertificateInfo::PGP_CERTIFICATE_FLAG_HAS_OWN_SIGNATURE ;

	return true ;
#endif
}

bool RNPPGPHandler::getKeyFingerprint(const RsPgpId& id, RsPgpFingerprint& fp) const
{
	RS_STACK_MUTEX(pgphandlerMtx);

    NOT_IMPLEMENTED;

#ifdef TODO
	const ops_keydata_t *key = locked_getPublicKey(id,false) ;

	if(!key) return false;

	ops_fingerprint_t f ;
	ops_fingerprint(&f,&key->key.pkey) ; 

	fp = RsPgpFingerprint::fromBufferUnsafe(f.fingerprint);

	return true ;
#endif
}

bool RNPPGPHandler::VerifySignBin(const void *literal_data, uint32_t literal_data_length, unsigned char *sign, unsigned int sign_len, const RsPgpFingerprint& key_fingerprint)
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

    RNP_OP_VERIFY_SIGNATURE_STRUCT(sig);
    RNP_OP_VERIFY_STRUCT          (verify);
    RNP_INPUT_STRUCT              (literal_data_input);
    RNP_INPUT_STRUCT              (signature_input);
    RNP_KEY_HANDLE_STRUCT         (key);
    RNP_BUFFER_STRUCT             (keyid);
    RNP_BUFFER_STRUCT             (key_fprint);

    rnp_result_t              sigstatus = RNP_SUCCESS;

    bool signature_verification_result = false;

    try
    {
        if(rnp_input_from_memory(&literal_data_input,(uint8_t*)literal_data,literal_data_length,false))
            throw std::runtime_error("Cannot initialize input data");

        if(rnp_input_from_memory(&signature_input,(uint8_t*)sign,sign_len,false))
            throw std::runtime_error("Cannot initialize signature data");

        if(rnp_op_verify_detached_create(&verify, mRnpFfi, literal_data_input, signature_input) != RNP_SUCCESS)
            throw std::runtime_error("Cannot initialize signature verification structure");

        if (rnp_op_verify_execute(verify) != RNP_SUCCESS)
            throw std::runtime_error("failed to execute verification operation");

        size_t sigcount=0;

        /* now check signatures and get some info about them */
        if (rnp_op_verify_get_signature_count(verify, &sigcount) != RNP_SUCCESS)
            throw std::runtime_error("failed to get signature count");

        if(sigcount != 1)
            throw std::runtime_error("ERROR: expected a single signature. Got "+RsUtil::NumberToString(sigcount));

        if (rnp_op_verify_get_signature_at(verify, 0, &sig) != RNP_SUCCESS)
            throw std::runtime_error("failed to get signature result ");

        if (rnp_op_verify_signature_get_key(sig, &key) != RNP_SUCCESS)
            throw std::runtime_error("failed to get signature result key");

        if (rnp_key_get_keyid(key, &keyid) != RNP_SUCCESS)
            throw std::runtime_error("failed to get signature result key id");

        sigstatus = rnp_op_verify_signature_get_status(sig);

        switch(sigstatus)
        {
        case RNP_SUCCESS :                 break;
        case RNP_ERROR_SIGNATURE_EXPIRED : throw std::runtime_error("Signature expired");
        case RNP_ERROR_KEY_NOT_FOUND : 	   throw std::runtime_error("key to verify signature was not available");
        default:
        case RNP_ERROR_SIGNATURE_INVALID : throw std::runtime_error("unmatched signature");
        }

        if(rnp_key_get_fprint(key, &key_fprint) != RNP_SUCCESS)
            throw std::runtime_error("Cannot extract fingerprint from signing key.");

        RsPgpFingerprint signer_fprint(key_fprint);
        signature_verification_result = (sigstatus == RNP_SUCCESS) && (signer_fprint == key_fingerprint);

        RsInfo() << "Status for signature by key " << key_fingerprint.toStdString() << ": found key " << signer_fprint.toStdString() << " in keyring. Status = " << (int)signature_verification_result;

        return signature_verification_result;
    }
    catch(std::exception& e)
    {
        RsErr() << "Signature verification failed: " << e.what() ;
        return false;
    }
}


#ifdef TO_REMOVE
// Lexicographic order on signature packets
//
bool operator<(const ops_packet_t& p1,const ops_packet_t& p2)
{
	if(p1.length < p2.length)
		return true ;
	if(p1.length > p2.length)
		return false ;

	for(uint32_t i=0;i<p1.length;++i)
	{
		if(p1.raw[i] < p2.raw[i])
			return true ;
		if(p1.raw[i] > p2.raw[i])
			return false ;
	}
	return false ;
}

bool OpenPGPSDKHandler::mergeKeySignatures(ops_keydata_t *dst,const ops_keydata_t *src)
{
	// First sort all signatures into lists to see which is new, which is not new

#ifdef DEBUG_PGPHANDLER
    RsErr() << "Merging signatures for key " << RsPgpId(dst->key_id).toStdString() ;
#endif
	std::set<ops_packet_t> dst_packets ;

	for(uint32_t i=0;i<dst->npackets;++i) dst_packets.insert(dst->packets[i]) ;

	std::set<ops_packet_t> to_add ;

	for(uint32_t i=0;i<src->npackets;++i) 
		if(dst_packets.find(src->packets[i]) == dst_packets.end())
		{
			uint8_t tag ;
			uint32_t length ;
			unsigned char *tmp_data = src->packets[i].raw ; // put it in a tmp variable because read_packetHeader() will modify it!!

			PGPKeyParser::read_packetHeader(tmp_data,tag,length) ;

			if(tag == PGPKeyParser::PGP_PACKET_TAG_SIGNATURE)
				to_add.insert(src->packets[i]) ;
#ifdef DEBUG_PGPHANDLER
			else
                RsErr() << "  Packet with tag 0x" << std::hex << (int)(src->packets[i].raw[0]) << std::dec << " not merged, because it is not a signature." ;
#endif
		}

	for(std::set<ops_packet_t>::const_iterator it(to_add.begin());it!=to_add.end();++it)
	{
#ifdef DEBUG_PGPHANDLER
        RsErr() << "  Adding packet with tag 0x" << std::hex << (int)(*it).raw[0] << std::dec ;
#endif
		ops_add_packet_to_keydata(dst,&*it) ;
	}
	return to_add.size() > 0 ;
}
#endif

bool RNPPGPHandler::locked_writeKeyringToDisk(bool secret, const std::string& keyring_file)
{
    RNP_OUTPUT_STRUCT(keyring_output);

    /* create file output object and save public keyring with generated keys, overwriting
     * previous file if any. You may use rnp_output_to_memory() here as well. */

    if (rnp_output_to_path(&keyring_output, keyring_file.c_str()) != RNP_SUCCESS)
    {
        RsErr() << "failed to initialize keyring writing structure" ;
        return false;
    }
    if (rnp_save_keys(mRnpFfi, "GPG", keyring_output, secret?RNP_LOAD_SAVE_SECRET_KEYS:RNP_LOAD_SAVE_PUBLIC_KEYS) != RNP_SUCCESS)
    {
        RsErr() << "failed to save keyring" ;
        return false;
    }
    return true;
}

bool RNPPGPHandler::locked_updateKeyringFromDisk(bool secret, const std::string& keyring_file)
{
    NOT_IMPLEMENTED;
    return false;
}

#ifdef TO_REMOVE
void OpenPGPSDKHandler::locked_mergeKeyringFromDisk(ops_keyring_t *keyring,
													std::map<RsPgpId,PGPCertificateInfo>& kmap,
													const std::string& keyring_file)
{
#ifdef DEBUG_PGPHANDLER
    RsErr() << "Merging keyring " << keyring_file << " from disk to memory." ;
#endif

	// 1 - load keyring into a temporary keyring list.
    ops_keyring_t *tmp_keyring = OpenPGPSDKHandler::allocateOPSKeyring() ;

	if(ops_false == ops_keyring_read_from_file(tmp_keyring, false, keyring_file.c_str()))
	{
        RsErr() << "OpenPGPSDKHandler::locked_mergeKeyringFromDisk(): cannot read keyring. File corrupted?" ;
		ops_keyring_free(tmp_keyring) ;
		return ;
	}

	// 2 - load new keys and merge existing key signatures

	for(int i=0;i<tmp_keyring->nkeys;++i)
		locked_addOrMergeKey(keyring,kmap,&tmp_keyring->keys[i]) ;// we dont' account for the return value. This is disk merging, not local changes.	

	// 4 - clean
	ops_keyring_free(tmp_keyring) ;
}
#endif

bool RNPPGPHandler::removeKeysFromPGPKeyring(const std::set<RsPgpId>& keys_to_remove,std::string& backup_file,uint32_t& error_code)
{
	// 1 - lock everything.
	//
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.
	RsStackFileLock flck(_pgp_lock_filename) ;	// lock access to PGP directory.

	error_code = PGP_KEYRING_REMOVAL_ERROR_NO_ERROR ;

    NOT_IMPLEMENTED;
#ifdef TODO
    for(std::set<RsPgpId>::const_iterator it(keys_to_remove.begin());it!=keys_to_remove.end();++it)
		if(locked_getSecretKey(*it) != NULL)
		{
            RsErr() << "(EE) OpenPGPSDKHandler:: can't remove key " << (*it).toStdString() << " since its shared by a secret key! Operation cancelled." ;
			error_code = PGP_KEYRING_REMOVAL_ERROR_CANT_REMOVE_SECRET_KEYS ;
			return false ;
		}

	// 2 - sync everything.
	//
	locked_syncPublicKeyring() ;

	// 3 - make a backup of the public keyring
	//
	char template_name[_pubring_path.length()+8] ;
	sprintf(template_name,"%s.XXXXXX",_pubring_path.c_str()) ;
	
#if defined __USE_XOPEN_EXTENDED || defined __USE_XOPEN2K8
	int fd_keyring_backup(mkstemp(template_name));
	if (fd_keyring_backup == -1)
#else
	if(mktemp(template_name) == NULL)
#endif
	{
        RsErr() << "OpenPGPSDKHandler::removeKeysFromPGPKeyring(): cannot create keyring backup file. Giving up." ;
		error_code = PGP_KEYRING_REMOVAL_ERROR_CANNOT_CREATE_BACKUP ;
		return false ;
	}
#if defined __USE_XOPEN_EXTENDED || defined __USE_XOPEN2K8
	close(fd_keyring_backup);	// TODO: keep the file open and use the fd
#endif

	if(!ops_write_keyring_to_file(_pubring,ops_false,template_name,ops_true)) 
	{
        RsErr() << "OpenPGPSDKHandler::removeKeysFromPGPKeyring(): cannot write keyring backup file. Giving up." ;
		error_code = PGP_KEYRING_REMOVAL_ERROR_CANNOT_WRITE_BACKUP ;
		return false ;
	}
	backup_file = std::string(template_name,_pubring_path.length()+7) ;

    RsErr() << "Keyring was backed up to file " << backup_file ;

	// Remove keys from the keyring, and update the keyring map.
	//
    for(std::set<RsPgpId>::const_iterator it(keys_to_remove.begin());it!=keys_to_remove.end();++it)
	{
		if(locked_getSecretKey(*it) != NULL)
		{
            RsErr() << "(EE) OpenPGPSDKHandler:: can't remove key " << (*it).toStdString() << " since its shared by a secret key!" ;
			continue ;
		}

		std::map<RsPgpId,PGPCertificateInfo>::iterator res = _public_keyring_map.find(*it) ;

		if(res == _public_keyring_map.end())
		{
            RsErr() << "(EE) OpenPGPSDKHandler:: can't remove key " << (*it).toStdString() << " from keyring: key not found." ;
			continue ;
		}

		if(res->second._key_index >= (unsigned int)_pubring->nkeys || RsPgpId(_pubring->keys[res->second._key_index].key_id) != *it)
		{
            RsErr() << "(EE) OpenPGPSDKHandler:: can't remove key " << (*it).toStdString() << ". Inconsistency found." ;
			error_code = PGP_KEYRING_REMOVAL_ERROR_DATA_INCONSISTENCY ;
			return false ;
		}

		// Move the last key to the freed place. This deletes the key in place.
		//
		ops_keyring_remove_key(_pubring,res->second._key_index) ;

		// Erase the info from the keyring map.
		//
		_public_keyring_map.erase(res) ;

		// now update all indices back. This internal look is very costly, but it avoids deleting the wrong keys, since the keyring structure is
		// changed by ops_keyring_remove_key and therefore indices don't point to the correct location anymore.

		int i=0 ;
		const ops_keydata_t *keydata ;
		while( (keydata = ops_keyring_get_key_by_index(_pubring,i)) != NULL )
		{
			PGPCertificateInfo& cert(_public_keyring_map[ RsPgpId(keydata->key_id) ]) ;
			cert._key_index = i ;
			++i ;
		}
	}

	// Everything went well, sync back the keyring on disk
	
	_pubring_changed = true ;
	_trustdb_changed = true ;

	locked_syncPublicKeyring() ;
	locked_syncTrustDatabase() ;

	return true ;
#endif
}
