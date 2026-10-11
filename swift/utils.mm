//
//  p12_password_check.cpp
//  feather
//
//  Created by HAHALOSAH on 8/6/24.
//

#include "utils.hpp"
#include "common.h"

#include <openssl/pem.h>
#include <openssl/cms.h>
#include <openssl/err.h>
#include <openssl/provider.h>
#include <openssl/pkcs12.h>
#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include <string>

using namespace std;

bool p12_password_check(NSString *file, NSString *pass) {
	const std::string strFile = [file cStringUsingEncoding:NSUTF8StringEncoding];
	const std::string strPass = [pass cStringUsingEncoding:NSUTF8StringEncoding];
	
	BIO *bio = BIO_new_file(strFile.c_str(), "rb");
	if (!bio) {
		NSLog(@"Failed to open .p12 file");
		return false;
	}
	
	OSSL_PROVIDER_load(NULL, "legacy");
	
	PKCS12 *p12 = d2i_PKCS12_bio(bio, NULL);
	BIO_free(bio);
	
	if (!p12) {
		NSLog(@"Failed to parse PKCS12");
		return false;
	}
	
	if( PKCS12_verify_mac(p12, NULL, 0) ) {
		return true;
	} else if( PKCS12_verify_mac(p12, strPass.c_str(), -1) ) {
		return true;
	} else {
		return false;
	}
	
	PKCS12_free(p12);
	return false;
}

NSData *p12_certificate(NSString *file, NSString *pass) {
	BIO *bio = BIO_new_file([file fileSystemRepresentation], "rb");
	if (!bio) {
		return nil;
	}

	OSSL_PROVIDER_load(NULL, "legacy");

	PKCS12 *p12 = d2i_PKCS12_bio(bio, NULL);
	BIO_free(bio);
	if (!p12) {
		return nil;
	}

	EVP_PKEY *pkey = NULL;
	X509 *cert = NULL;
	NSData *der = nil;

	if (PKCS12_parse(p12, [pass UTF8String], &pkey, &cert, NULL) && cert) {
		unsigned char *buffer = NULL;
		int length = i2d_X509(cert, &buffer);
		if (length > 0) {
			der = [NSData dataWithBytes:buffer length:length];
		}
		OPENSSL_free(buffer);
	}

	X509_free(cert);
	EVP_PKEY_free(pkey);
	PKCS12_free(p12);
	return der;
}

// This is fucking bullshit IMO.
//
// In total, I probably wasted a total of 1.5 hours on this
// Feel free to increment the counter until someone finds a proper fix
//
// hours_wasted = 1.5
//
// TODO: FIX
void password_check_fix(NSString *path) {
	string strProvisionFile = [path cStringUsingEncoding:NSUTF8StringEncoding];
	string strProvisionData;
	ZFile::ReadFile(strProvisionFile.c_str(), strProvisionData);
	
	BIO *in = BIO_new(BIO_s_mem());
	OPENSSL_assert((size_t)BIO_write(in, strProvisionData.data(), (int)strProvisionData.size()) == strProvisionData.size());
	d2i_CMS_bio(in, NULL);
}

void password_check_fix_free(NSString *path) {
	string strProvisionFile = [path cStringUsingEncoding:NSUTF8StringEncoding];
	string strProvisionData;
	ZFile::ReadFile(strProvisionFile.c_str(), strProvisionData);
	
	BIO *in = BIO_new(BIO_s_mem());
	if (!in) return;
	
	if ((size_t)BIO_write(in, strProvisionData.data(), (int)strProvisionData.size()) != strProvisionData.size()) {
		BIO_free(in);
		return;
	}
	
	CMS_ContentInfo *cms = d2i_CMS_bio(in, NULL);
	if (cms) CMS_ContentInfo_free(cms);
	// free my boy
	BIO_free(in);
}
