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
#define NOT_IMPLEMENTED RsErr() << " function " << __PRETTY_FUNCTION__ << " Not implemented yet." << std::endl; assert(false); return false;

// Helper structs to auto-delete after leaving current scope.

template<class T,rnp_result_t(*destructor)(T*)>
class t_ScopeGuard
{
public:
    t_ScopeGuard(T *& out) : mOut(out)
    {
#ifdef DEBUG_RNP
//        RsErr() << "Creating RNP structure pointer " << (void*)&mOut << " value: " << (void*)mOut;
#endif
    }
    ~t_ScopeGuard()
    {
        if(mOut != nullptr)
        {
#ifdef DEBUG_RNP
//            RsErr() << "Autodeleting RNP structure pointer " << (void*)&mOut << " value: " << (void*)mOut;
#endif
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
typedef t_ScopeGuard<rnp_uid_handle_st,      &rnp_uid_handle_destroy>       rnp_uid_handle_autodelete;
typedef t_ScopeGuard<rnp_op_sign_st,         &rnp_op_sign_destroy>          rnp_op_sign_autodelete;
typedef t_ScopeGuard<rnp_op_encrypt_st,      &rnp_op_encrypt_destroy>       rnp_op_encrypt_autodelete;
typedef t_ScopeGuard<rnp_signature_handle_st,&rnp_signature_handle_destroy> rnp_signature_handle_autodelete;
typedef t_ScopeGuard<rnp_op_generate_st     ,&rnp_op_generate_destroy>      rnp_op_generate_autodelete;
typedef t_ScopeGuard<rnp_ffi_st             ,&rnp_ffi_destroy>              rnp_ffi_autodelete;

#define RNP_INPUT_STRUCT(name)               rnp_input_t             name=nullptr; rnp_input_autodelete             name ## tmp_destructor(name);
#define RNP_OUTPUT_STRUCT(name)              rnp_output_t            name=nullptr; rnp_output_autodelete            name ## tmp_destructor(name);
#define RNP_OP_VERIFY_STRUCT(name)           rnp_op_verify_t         name=nullptr; rnp_op_verify_autodelete         name ## tmp_destructor(name);
#define RNP_KEY_HANDLE_STRUCT(name)          rnp_key_handle_t        name=nullptr; rnp_key_handle_autodelete        name ## tmp_destructor(name);
#define RNP_UID_HANDLE_STRUCT(name)          rnp_uid_handle_t        name=nullptr; rnp_uid_handle_autodelete        name ## tmp_destructor(name);
#define RNP_OP_SIGN_STRUCT(name)             rnp_op_sign_t           name=nullptr; rnp_op_sign_autodelete           name ## tmp_destructor(name);
#define RNP_OP_ENCRYPT_STRUCT(name)          rnp_op_encrypt_t        name=nullptr; rnp_op_encrypt_autodelete        name ## tmp_destructor(name);
#define RNP_SIGNATURE_HANDLE_STRUCT(name)    rnp_signature_handle_t  name=nullptr; rnp_signature_handle_autodelete  name ## tmp_destructor(name);
#define RNP_OP_GENERATE_STRUCT(name)         rnp_op_generate_t       name=nullptr; rnp_op_generate_autodelete       name ## tmp_destructor(name);
#define RNP_FFI_STRUCT(name)                 rnp_ffi_t               name=nullptr; rnp_ffi_autodelete               name ## tmp_destructor(name);
#define RNP_BUFFER_STRUCT(name)              char            *       name=nullptr; rnp_buffer_autodelete            name ## tmp_destructor(name);

// The following one misses a fnction to delete the pointer (problem in RNP lib???)

#define RNP_OP_VERIFY_SIGNATURE_STRUCT(name) rnp_op_verify_signature_t name=nullptr;

// This overrides SHA1 security rules, so that certs signed with sha1 alg are still accepted as friends and profiles signed with sha1 still load.

#ifdef V07_NON_BACKWARD_COMPATIBLE_CHANGE_006
#define FFI_CREATE(ffi) \
    rnp_ffi_create(&ffi,RNP_KEYSTORE_GPG,RNP_KEYSTORE_GPG);
#else
#define FFI_CREATE(ffi) \
    rnp_ffi_create(&ffi,RNP_KEYSTORE_GPG,RNP_KEYSTORE_GPG);\
    rnp_add_security_rule(ffi,RNP_FEATURE_HASH_ALG,"SHA1",RNP_SECURITY_OVERRIDE,0,RNP_SECURITY_DEFAULT);
#endif

// Implementation of RNP pgp handler.

RNPPGPHandler::RNPPGPHandler(const std::string& pubring, const std::string& secring,const std::string& trustdb,const std::string& pgp_lock_filename)
    : PGPHandler(pubring,secring,trustdb,pgp_lock_filename)
{
    RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

    RsInfo() << "Using RNP lib version " << rnp_version_string() ;
    RsInfo() << "RNP-PGPHandler: Initing pgp keyrings";

    FFI_CREATE(mRnpFfi);

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

        if (rnp_import_keys(mRnpFfi, keyfile, RNP_LOAD_SAVE_PUBLIC_KEYS | RNP_LOAD_SAVE_PERMISSIVE,nullptr) != RNP_SUCCESS)
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

        if (rnp_import_keys(mRnpFfi, keyfile, RNP_LOAD_SAVE_SECRET_KEYS | RNP_LOAD_SAVE_PERMISSIVE,nullptr) != RNP_SUCCESS)
            throw std::runtime_error("RNPPGPHandler::RNPPGPHandler(): cannot read secret keyring. File access error.") ;
    }
    else
        RsInfo() << "  secring file: " << secring << " not found. Creating an empty one";


    size_t pub_count;
    size_t sec_count;
    rnp_get_public_key_count(mRnpFfi,&pub_count);
    rnp_get_secret_key_count(mRnpFfi,&sec_count);

    RsInfo() << "Loaded " << pub_count << " public keys, and " << sec_count << " secret keys." ;

    {
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
    }

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

void RNPPGPHandler::locked_timeStampKey(const RsPgpId& key_id)
{
    _public_keyring_map[key_id]._time_stamp = time(nullptr);
    _trustdb_changed = true;
}

bool rnp_get_passphrase_cb(rnp_ffi_t        /* ffi */,
                           void *           app_ctx,
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

    static_cast<RNPPGPHandler*>(app_ctx)->locked_timeStampKey(RsPgpId(key_id));

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

    auto fill_cert = [key_alg,key_fprint](PGPCertificateInfo& cert,char *key_uid,const std::set<RsPgpId>& signers)
    {
        extract_name_and_comment(key_uid,cert._name,cert._comment,cert._email);

        cert.signers = signers;
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

    std::set<RsPgpId> signers;

    size_t signature_count = 0;
    rnp_key_get_signature_count(key_handle,&signature_count);

    RsDbg() << "Key " << key_id << " has " << signature_count << " signers." ;

    for(size_t i=0;i<signature_count;++i)
    {
        RNP_SIGNATURE_HANDLE_STRUCT(sig);

        if(rnp_key_get_signature_at(key_handle,i,&sig) != RNP_SUCCESS)
            throw std::runtime_error("Error getting signature data");

        RNP_BUFFER_STRUCT(suid);

        if(rnp_signature_get_keyid(sig,&suid) != RNP_SUCCESS)
            throw std::runtime_error("Error getting signature key id");

        signers.insert(RsPgpId(suid));
    }

    signers.insert(RsPgpId(key_id)); // in libRNP the signer of self-signed certificates is not reported in signers.

    {
        auto& cert(_public_keyring_map[ RsPgpId(key_id)]);
        fill_cert(cert,key_uid,signers) ;
    }

    if(have_secret)
    {
        auto& cert(_secret_keyring_map[ RsPgpId(key_id)]);
        fill_cert(cert,key_uid,signers) ;
    }
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

    rnp_ffi_destroy(mRnpFfi);
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

    {
        // Now the real thing
        RsStackMutex mtx(pgphandlerMtx) ;			// lock access to PGP memory structures.
        RsStackFileLock flck(_pgp_lock_filename) ;	// lock access to PGP directory.

        RNP_OP_GENERATE_STRUCT(generate);

        if(rnp_op_generate_create(&generate,mRnpFfi,"rsa") != RNP_SUCCESS)
        {
            errString = std::string("(EE) cannot create RNP gey generation structure");
            return false;
        }

        auto s = std::string(name) + " (Generated by RetroShare) <" + email + ">" ;

        rnp_op_generate_set_bits(generate,keynumbits);
        rnp_op_generate_set_hash(generate,"SHA256");
        rnp_op_generate_set_protection_password(generate,passphrase.c_str());
        rnp_op_generate_set_protection_cipher(generate,"AES256");
        rnp_op_generate_set_protection_iterations(generate,8192);
        rnp_op_generate_clear_usage(generate);
        rnp_op_generate_add_usage(generate,"encrypt");
        rnp_op_generate_add_usage(generate,"certify");
        rnp_op_generate_add_usage(generate,"authenticate");
        rnp_op_generate_add_usage(generate,"sign");
        rnp_op_generate_set_userid(generate,s.c_str());
        rnp_op_generate_set_expiration(generate,0);

        if(rnp_op_generate_execute(generate) != RNP_SUCCESS)
        {
            errString = std::string("(EE) gey generation failed.");
            return false;
        }

        RNP_KEY_HANDLE_STRUCT(key);

        if(rnp_op_generate_get_key(generate,&key) != RNP_SUCCESS)
        {
            errString = std::string("(EE) cannot retrieve generated key.");
            return false;
        }

        RNP_BUFFER_STRUCT(buf);

        if(rnp_key_get_keyid(key,&buf) != RNP_SUCCESS)
        {
            errString = std::string("(EE) cannot retrieve key ID of generated key.");
            return false;
        }

        pgpId = RsPgpId(buf);

        initCertificateInfo(key);
        privateTrustCertificate(pgpId,PGPCertificateInfo::PGP_CERTIFICATE_TRUST_ULTIMATE) ;

        locked_writeKeyringToDisk(true,_secring_path);
    }
    syncDatabase();

    return true;
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

std::string RNPPGPHandler::SaveCertificateToString(const RsPgpId& /* id */,bool /* include_signatures */) const
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.
    RsErr() << " function " << __PRETTY_FUNCTION__ << " Not implemented yet." << std::endl;
    assert(false);
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

bool RNPPGPHandler::exportGPGKeyPair(const std::string& filename,const RsPgpId& id) const
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

    RNP_OUTPUT_STRUCT(output);
    RNP_KEY_HANDLE_STRUCT(key_handle);

    try
    {
        if(rnp_output_to_path(&output,filename.c_str()) != RNP_SUCCESS)
            throw std::runtime_error("Cannot create output structure");

        if(rnp_locate_key(mRnpFfi,"keyid",id.toStdString().c_str(),&key_handle))
            throw std::runtime_error("Cannot find PGP key " + id.toStdString() + " to export.");

        uint32_t flags = RNP_KEY_EXPORT_ARMORED ;

        if(rnp_key_export(key_handle, output, flags | RNP_KEY_EXPORT_PUBLIC) != RNP_SUCCESS)
            throw std::runtime_error("Key export failed ID=" + id.toStdString() + " to export.");

        if(rnp_key_export(key_handle, output, flags | RNP_KEY_EXPORT_SECRET) != RNP_SUCCESS)
            throw std::runtime_error("Private key export failed ID=" + id.toStdString() + " to export.");

        return true;
    }
    catch (std::exception& e)
    {
        RS_ERR(std::string("Cannot export key pair: ")+e.what());
        return false;
    }
}

bool RNPPGPHandler::exportGPGKeyPairToString( std::string& /* data */, const RsPgpId& /* exportedKeyId */, bool /* includeSignatures */, std::string& /* errorMsg */ ) const
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

        RNP_FFI_STRUCT(tmp_ffi);
        FFI_CREATE(tmp_ffi);

        if(rnp_load_keys(tmp_ffi, RNP_KEYSTORE_GPG, input, RNP_LOAD_SAVE_PUBLIC_KEYS) != RNP_SUCCESS)
            throw std::runtime_error("Cannot interpret supplied memory block as public key.") ;

        size_t pub_count;
        rnp_get_public_key_count(tmp_ffi,&pub_count);

        if(pub_count == 0)
            throw std::runtime_error("Supplied memory block does not contain any key");
        else if(pub_count > 1)
            throw std::runtime_error("Supplied memory block contain more than one key (" + RsUtil::NumberToString(pub_count) + " found)");

        {
            rnp_identifier_iterator_t it;
            rnp_identifier_iterator_create(tmp_ffi,&it,RNP_IDENTIFIER_KEYID);

            const char *key_identifier = nullptr;
            if(rnp_identifier_iterator_next(it,&key_identifier) != RNP_SUCCESS)
                throw std::runtime_error("Error while reaching first key");

            key_id = RsPgpId(key_identifier);
            rnp_identifier_iterator_destroy(it);
        }

        RsDbg() << "Binary block contains key ID " << key_id.toStdString() ;

        RNP_KEY_HANDLE_STRUCT(key_handle);
        if(rnp_locate_key(tmp_ffi,RNP_IDENTIFIER_KEYID,key_id.toStdString().c_str(),&key_handle) != RNP_SUCCESS)
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

// static in order to make sure we're not using members from the RNPPGPHandler class.
static bool checkGPGKeyPair(rnp_ffi_t tmp_ffi,
                            RsPgpId& imported_key_id,
                            RsPgpFingerprint& fprint,
                            std::string& username,
                            std::string& key_algorithm,
                            uint32_t& key_size)
{
    size_t pub_count;
    size_t sec_count;

    rnp_get_public_key_count(tmp_ffi,&pub_count);
    rnp_get_secret_key_count(tmp_ffi,&sec_count);

    if(pub_count != 1) throw std::runtime_error("Expected 1 public key: found "+RsUtil::NumberToString(pub_count));
    if(sec_count != 1) throw std::runtime_error("Expected 1 secret key: found "+RsUtil::NumberToString(sec_count));

    {
        rnp_identifier_iterator_t it;
        rnp_identifier_iterator_create(tmp_ffi,&it,RNP_IDENTIFIER_KEYID);
        const char *key_identifier = nullptr;

        rnp_identifier_iterator_next(it,&key_identifier);

        if(key_identifier == nullptr)
            throw std::runtime_error("no key identifier found in this keypair");

        imported_key_id = RsPgpId(key_identifier);
        rnp_identifier_iterator_destroy(it);
    }

    // check that the key has public and secret key for the same key

    RNP_KEY_HANDLE_STRUCT(key_handle);
    RNP_BUFFER_STRUCT(key_fprint);
    RNP_BUFFER_STRUCT(key_uid);
    RNP_BUFFER_STRUCT(key_alg);
    uint32_t key_bits;

    rnp_locate_key(tmp_ffi,RNP_IDENTIFIER_KEYID,imported_key_id.toStdString().c_str(),&key_handle);

    rnp_key_get_fprint(key_handle, &key_fprint);
    rnp_key_get_primary_uid(key_handle, &key_uid);
    rnp_key_get_alg(key_handle, &key_alg);
    rnp_key_get_bits(key_handle, &key_bits);

    key_size = key_bits;
    username = std::string(key_uid);
    fprint = RsPgpFingerprint(key_fprint);
    key_algorithm = std::string(key_alg);

    bool have_secret = false;
    rnp_key_have_secret(key_handle,&have_secret);

    return have_secret; // there's exactly 1 sec/pub key each, so if that key has secret part, it matches the public one.
}

static bool testKeyPairInput(rnp_input_t keyfile,RsPgpId& imported_key_id)
{
        RNP_FFI_STRUCT(tmp_ffi);
        FFI_CREATE(tmp_ffi);

        uint32_t flags = RNP_LOAD_SAVE_PUBLIC_KEYS | RNP_LOAD_SAVE_SECRET_KEYS ;

        if (rnp_load_keys(tmp_ffi, RNP_KEYSTORE_GPG, keyfile, flags) != RNP_SUCCESS)
        {
            RsErr() << "RNPPGPHandler::RNPPGPHandler(): cannot read public keyring. File access error." ;
            return false;
        }

        RsPgpFingerprint key_fingerprint;
        std::string key_username;
        std::string key_algorithm;
        uint32_t key_size;

        if(!checkGPGKeyPair(tmp_ffi,imported_key_id,key_fingerprint,key_username,key_algorithm,key_size))
            return false;

        RsInfo() << "Imported " << key_algorithm << "-" << key_size << " key pair. Key id: " << imported_key_id
                 << " fingerprint: " << key_fingerprint << " Username: \"" << key_username << "\"" ;

        return true;
}

bool RNPPGPHandler::importKeyPairData(rnp_input_t input)
{
    RS_STACK_MUTEX(pgphandlerMtx);

    // Import the key in the actual keyring.

    RNP_BUFFER_STRUCT(result);
    size_t old_public_key_count = 0;
    rnp_get_public_key_count(mRnpFfi,&old_public_key_count);

    if(rnp_import_keys(mRnpFfi, input, RNP_LOAD_SAVE_PUBLIC_KEYS | RNP_LOAD_SAVE_SECRET_KEYS,&result) != RNP_SUCCESS)
        throw std::runtime_error("RNPPGPHandler::RNPPGPHandler(): cannot read public keyring. File access error.") ;

    size_t new_public_key_count = 0;
    rnp_get_public_key_count(mRnpFfi,&new_public_key_count);

    RsInfo() << "Loaded keypair. Info is: " << result;
    RsInfo() << "Old key count: " << old_public_key_count << ", new key count:" << new_public_key_count ;

    // sync the keyring.

    _pubring_changed = true;
    locked_writeKeyringToDisk(true,_secring_path);

    return true;
}

bool RNPPGPHandler::importGPGKeyPair(const std::string& filename,RsPgpId& imported_key_id,std::string& import_error)
{
    if(!RsDirUtil::fileExists(filename))
        throw std::runtime_error("File " + filename + " does not exist.");

    // First, check how many keys we have, tht there is a secret key, etc.
    // We have to create twice the rnp_input_t structure, because once we read it, the next read will get nothing.

    try
    {
        {
            RNP_INPUT_STRUCT(keyfile);

            /* load public keyring */
            if (rnp_input_from_path(&keyfile, filename.c_str()) != RNP_SUCCESS)
                throw std::runtime_error("Cannot create input structure.") ;

            if(!testKeyPairInput(keyfile,imported_key_id))
                return false;
        }

        // Then input the file in actual keyring

        RNP_INPUT_STRUCT(keyfile);

        if (rnp_input_from_path(&keyfile, filename.c_str()) != RNP_SUCCESS)
            throw std::runtime_error("Cannot create input structure.") ;

        if(!importKeyPairData(keyfile))
            throw std::runtime_error("Data inport failed.") ;

        // check that the key was actually imported

        RNP_KEY_HANDLE_STRUCT(key_handle);

        if(RNP_SUCCESS != rnp_locate_key(mRnpFfi,"keyid",imported_key_id.toStdString().c_str(),&key_handle))
            throw std::runtime_error("Key import check failed: imported key is missing from keyring.");

        initCertificateInfo(key_handle) ;
        import_error.clear();
        return true;
    }
    catch (std::exception& e)
    {
        import_error = e.what();
        RS_ERR("Cannot import GPG keypair. ERROR: "+std::string(e.what()))   ;
        return false;
    }
}

bool RNPPGPHandler::importGPGKeyPairFromString(const std::string &data, RsPgpId &imported_key_id, std::string &import_error)
{
    try
    {
        {
            RNP_INPUT_STRUCT(keyfile);

            /* load public keyring */
            if (rnp_input_from_memory(&keyfile, (uint8_t*)data.c_str(),data.size(),false) != RNP_SUCCESS)
                throw std::runtime_error("Cannot create input structure.") ;

            if(!testKeyPairInput(keyfile,imported_key_id))
                return false;
        }

        // Then input the file in actual keyring

        RNP_INPUT_STRUCT(keyfile);

        if (rnp_input_from_memory(&keyfile, (uint8_t*)data.c_str(),data.size(),false) != RNP_SUCCESS)
            throw std::runtime_error("Cannot create input structure.") ;

        if(!importKeyPairData(keyfile))
            throw std::runtime_error("Data inport failed.") ;

        // check that the key was actually imported

        RNP_KEY_HANDLE_STRUCT(key_handle);

        if(RNP_SUCCESS != rnp_locate_key(mRnpFfi,"keyid",imported_key_id.toStdString().c_str(),&key_handle))
            throw std::runtime_error("Key import check failed: imported key is missing from keyring.");

        initCertificateInfo(key_handle) ;
        import_error.clear();
        return true;
    }
    catch(std::exception& e)
    {
        import_error = e.what();
        RS_ERR("Cannot import GPG keypair. ERROR: "+std::string(e.what()))   ;
        return false;
    }
}

bool RNPPGPHandler::LoadCertificate(const unsigned char *data,uint32_t data_len,
                                    bool /* armoured */,RsPgpId& id,std::string& /* error_string */)
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.
#ifdef DEBUG_PGPHANDLER
    RsErr() << "Reading new key from string: " ;
#endif

    rnp_input_t input;

    if(rnp_input_from_memory(&input,(uint8_t*)data,data_len,false) != RNP_SUCCESS)
        return false;

    size_t old_key_count = 0;
    size_t new_key_count = 0;

    rnp_get_public_key_count(mRnpFfi,&old_key_count);

    uint32_t flags = RNP_LOAD_SAVE_PUBLIC_KEYS | RNP_LOAD_SAVE_PERMISSIVE;
    RNP_BUFFER_STRUCT(result);

    if(rnp_import_keys(mRnpFfi,input,flags,&result) != RNP_SUCCESS)
        return false;

    // Parse the json output. This is extremely coarse parsing work. Using jsoncpp would be much cleaner.
    // Another way to go would be to importe into a tmp keyring and parse the key.

    std::string resultstr(result);
    const std::string fprint_str("\"fingerprint\":\"");
    auto pos = resultstr.find(fprint_str);

    if(pos == std::string::npos)
    {
        RsErr() << "Cannot find fingerprint of loaded key in the following text: " << result;
        RsErr() << "Is this a bug?";
        return false;
    }
    if(resultstr.find(fprint_str,pos+1) != std::string::npos)	// check that there's only one fingerprint
    {
        RsErr() << "Multiple fingerprints in the following text: " << result;
        RsErr() << "This is inconsistent.";
        return false;
    }
    id = RsPgpId(resultstr.substr(pos+16+23,16));

    if(id.isNull())
    {
        RsErr()<< "Error while parsing fingerprint from result string." ;
        return false;
    }
    rnp_get_public_key_count(mRnpFfi,&new_key_count);

    RsInfo() << "Loaded " << new_key_count - old_key_count <<  " new keys." ;
    RsInfo() << "Loaded information: " << result ;
    RsInfo() << "Loaded key ID: " << id.toStdString() ;

    // try to locate the key in the keyring, to check that it's been imported correctly

    RNP_KEY_HANDLE_STRUCT(key_handle);

    if(RNP_SUCCESS != rnp_locate_key(mRnpFfi,"keyid",id.toStdString().c_str(),&key_handle))
    {
        RsErr() << "Something went wrong: cannot locate key ID " << id << " in public keyring." ;
        return false;
    }
    RsInfo() << "Key ID " << id << " is in public keyring." ;

    initCertificateInfo(key_handle) ;

    _pubring_changed = true;
    return true;
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

bool RNPPGPHandler::encryptData(const RsPgpId& key_id,bool armored,rnp_input_t input,rnp_output_t output)
{
    RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

    RNP_OP_ENCRYPT_STRUCT(encrypt);

    if(rnp_op_encrypt_create(&encrypt, mRnpFfi, input, output) != RNP_SUCCESS)
        throw std::runtime_error("Cannot create encryption structure");

    rnp_op_encrypt_set_armor(encrypt, armored);
    rnp_op_encrypt_set_file_name(encrypt, nullptr);
    rnp_op_encrypt_set_file_mtime(encrypt, (uint32_t) time(NULL));
    rnp_op_encrypt_set_compression(encrypt, "ZIP", 6);
    rnp_op_encrypt_set_cipher(encrypt, RNP_ALGNAME_AES_256);
    rnp_op_encrypt_set_aead(encrypt, "None");

    RNP_KEY_HANDLE_STRUCT(key);

    locked_timeStampKey(key_id);

    if(rnp_locate_key(mRnpFfi, "keyid", key_id.toStdString().c_str(), &key) != RNP_SUCCESS)
        throw std::runtime_error("Cannot locate destination key " + key_id.toStdString() + " for encryption");

    if(rnp_op_encrypt_add_recipient(encrypt, key) != RNP_SUCCESS)
        throw std::runtime_error("Failed to add recipient " + key_id.toStdString() + " for encryption");

    // Execute encryption operation

    if(rnp_op_encrypt_execute(encrypt) != RNP_SUCCESS)
        throw std::runtime_error("Encryption operation failed.");

    return true;
}

bool RNPPGPHandler::encryptTextToFile(const RsPgpId& key_id,const std::string& text,const std::string& outfile)
{
    try
    {
        rnp_input_t input ;
        RNP_OUTPUT_STRUCT(output);

        if(rnp_input_from_memory(&input, (uint8_t*)text.c_str(),text.size(),false) != RNP_SUCCESS)
            throw std::runtime_error("Cannot create input memory structure");

        if(rnp_output_to_path(&output, outfile.c_str()) != RNP_SUCCESS)
            throw std::runtime_error("Cannot create output structure");

        return encryptData(key_id,true,input,output);
    }
    catch(std::exception& e)
    {
        RS_ERR("Encryption failed with key " + key_id.toStdString() + ": "+e.what());
        return false;
    }
}

bool RNPPGPHandler::encryptDataBin(const RsPgpId& key_id,const void *data, const uint32_t len, unsigned char *encrypted_data, unsigned int *encrypted_data_len)
{
	RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

    locked_timeStampKey(key_id);

    try
    {
        rnp_input_t input ;
        RNP_OUTPUT_STRUCT(output);

        if(rnp_input_from_memory(&input, (uint8_t*)data,len,false) != RNP_SUCCESS)
            throw std::runtime_error("Cannot create input memory structure");

        if(rnp_output_to_memory(&output,0) != RNP_SUCCESS)
            throw std::runtime_error("Cannot create output structure");

        if(!encryptData(key_id,false,input,output))
            return false;

        uint8_t *buf=nullptr;
        size_t size=0;

        rnp_output_memory_get_buf(output,&buf,&size,false);

        if(size > *encrypted_data_len)
            throw std::runtime_error("Cannot encrypt because output data length exceeds buffer size ("+RsUtil::NumberToString(size)+">"+RsUtil::NumberToString(*encrypted_data_len));

        *encrypted_data_len = size;
        memcpy(encrypted_data,buf,size);

        return true;
    }
    catch(std::exception& e)
    {
        RS_ERR("Encryption failed with key " + key_id.toStdString() + ": "+e.what());
        return false;
    }
}

bool RNPPGPHandler::decryptDataBin(const RsPgpId& key_id,const void *encrypted_data, const uint32_t encrypted_len, unsigned char *data, unsigned int *data_len)
{
    RsStackMutex mtx(pgphandlerMtx) ;				// lock access to PGP memory structures.

    /* set the password provider */
    rnp_ffi_set_pass_provider(mRnpFfi, rnp_get_passphrase_cb, this);

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
    rnp_ffi_set_pass_provider(mRnpFfi, rnp_get_passphrase_cb, this);

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

bool RNPPGPHandler::SignDataBin(const RsPgpId& id,const void *data, const uint32_t len,
                                unsigned char *sign, unsigned int *signlen,
                                bool /* use_raw_signature */, std::string /* reason = "" */)
{
    RS_STACK_MUTEX(pgphandlerMtx);

    try
    {
        rnp_ffi_set_pass_provider(mRnpFfi, rnp_get_passphrase_cb, this);

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

    try
    {
        RNP_KEY_HANDLE_STRUCT(signed_key);
        RNP_KEY_HANDLE_STRUCT(signer_key);
        RNP_UID_HANDLE_STRUCT(signed_key_uid);
        RNP_SIGNATURE_HANDLE_STRUCT(signature_handle);

        rnp_ffi_set_pass_provider(mRnpFfi, rnp_get_passphrase_cb, this);

        if(rnp_locate_key(mRnpFfi,"keyid",id_of_key_to_sign.toStdString().c_str(),&signed_key) != RNP_SUCCESS)
            throw std::runtime_error("Key not found: "+id_of_key_to_sign.toStdString());

        if(rnp_locate_key(mRnpFfi,"keyid",ownId.toStdString().c_str(),&signer_key) != RNP_SUCCESS)
            throw std::runtime_error("Key not found: "+id_of_key_to_sign.toStdString());

        if(rnp_key_direct_signature_create(signer_key,signed_key, &signature_handle) != RNP_SUCCESS)
            throw std::runtime_error("Adding signature failed.");

        if(rnp_key_signature_sign(signature_handle) != RNP_SUCCESS)
            throw std::runtime_error("Creating signature failed.");

        initCertificateInfo(signed_key);	// update signatures
        _pubring_changed = true;
        _public_keyring_map[id_of_key_to_sign]._flags |= PGPCertificateInfo::PGP_CERTIFICATE_FLAG_HAS_OWN_SIGNATURE ;
        return true;
    }
    catch(std::exception& e)
    {
        RS_ERR(std::string("ERROR: Signature failed: ") + e.what());
        return false;
    }
}

bool RNPPGPHandler::getKeyFingerprint(const RsPgpId& id, RsPgpFingerprint& fp) const
{
	RS_STACK_MUTEX(pgphandlerMtx);

    RNP_KEY_HANDLE_STRUCT(key);

    if(rnp_locate_key(mRnpFfi,"keyid",id.toStdString().c_str(),&key)!=RNP_SUCCESS)
    {
        RS_ERR("Cannot find key "+id.toStdString());
        return false;
    }
    RNP_BUFFER_STRUCT(buf);
    if(rnp_key_get_fprint(key,&buf)!=RNP_SUCCESS)
    {
        RS_ERR("Cannot extract fingerprint from key "+id.toStdString());
        return false;
    }

    fp = RsPgpFingerprint(buf);
    return true;
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

    locked_timeStampKey(pgpIdFromFingerprint(key_fingerprint));

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
    RNP_INPUT_STRUCT(input);

    rnp_input_from_path(&input,keyring_file.c_str());

    uint32_t flags = secret ? RNP_LOAD_SAVE_SECRET_KEYS : RNP_LOAD_SAVE_PUBLIC_KEYS;
    flags |= RNP_LOAD_SAVE_PERMISSIVE;

    RNP_BUFFER_STRUCT(result);

    if(rnp_import_keys(mRnpFfi, input, flags, &result) != RNP_SUCCESS)
    {
        RS_ERR("Cannot sync keyring file " + keyring_file);
        return false;
    }

    if(result)
        RsInfo() << "Updated keyring with the following keys: " << result ;

    return true;
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

    if(!locked_writeKeyringToDisk(false, template_name))
    {
        RsErr() << "Cannot backup public keyring before removing keys. Operation cancelled." ;
        return false;
    }
	backup_file = std::string(template_name,_pubring_path.length()+7) ;

    RsErr() << "Keyring was backed up to file " << backup_file ;

	// Remove keys from the keyring, and update the keyring map.
	//
    for(std::set<RsPgpId>::const_iterator it(keys_to_remove.begin());it!=keys_to_remove.end();++it)
	{
        RNP_KEY_HANDLE_STRUCT(key_handle);
        rnp_locate_key(mRnpFfi,"keyid",(*it).toStdString().c_str(),&key_handle);

        if(!key_handle)
        {
            RsErr() << "Cannot find key " << it->toStdString() << " into keyring." ;
            continue;
        }

        bool have_secret = false;
        rnp_key_have_secret(key_handle,&have_secret);

        if(have_secret)
		{
            RsErr() << "Can't remove key " << (*it).toStdString() << " since its a secret key!" ;
			continue ;
		}

		std::map<RsPgpId,PGPCertificateInfo>::iterator res = _public_keyring_map.find(*it) ;

		if(res == _public_keyring_map.end())
		{
            RsErr() << "Can't remove key " << (*it).toStdString() << " from keyring: key not found in keyring map." ;
			continue ;
		}

		// Move the last key to the freed place. This deletes the key in place.
		//
        if(rnp_key_remove(key_handle,RNP_KEY_REMOVE_PUBLIC | RNP_KEY_REMOVE_SUBKEYS) != RNP_SUCCESS)
        {
            RsErr() << "Failed to remove key " << (*it).toStdString() << ": rnp_key_remove failed." ;
            continue ;
        }

		// Erase the info from the keyring map.
		//
		_public_keyring_map.erase(res) ;
	}

	// Everything went well, sync back the keyring on disk
	
	_pubring_changed = true ;
	_trustdb_changed = true ;

	locked_syncPublicKeyring() ;
	locked_syncTrustDatabase() ;

	return true ;
}
