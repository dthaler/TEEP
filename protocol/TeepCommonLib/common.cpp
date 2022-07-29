// Copyright (c) Microsoft Corporation
// SPDX-License-Identifier: MIT
//
// This file contains trusted code in common between the TAM and TEEP Agent.
#include <stdio.h>
#include <string.h>
#include "t_cose/t_cose_common.h"
#include "common.h"
extern "C" {
#ifdef OE_BUILD_ENCLAVE
#define _countof(x) OE_COUNTOF(x)
#define sprintf_s(dest, len, ...) sprintf(dest, __VA_ARGS__)
#endif
#include "teep_protocol.h"
#include "openssl/rsa.h"
#include "openssl/evp.h"
#include "openssl/pem.h"
#include "openssl/x509.h"
};

static const char* cbor_type_name[] = {
    nullptr, nullptr , "int64", "uint64", "array", "map", "bstr", "tstr"
};

static const char* get_cbor_type_name(unsigned int type)
{
    if ((type >= _countof(cbor_type_name)) || cbor_type_name[type] == nullptr) {
        static char buffer[80];
        sprintf_s(buffer, sizeof(buffer), "? (%d)", type);
        return buffer;
    }
    return cbor_type_name[type];
}

void report_type_error(std::ostream& s, const char* id, int expected_type, int actual_type)
{
    s << "Invalid " << id << " type " << get_cbor_type_name(actual_type) << ", expected " << get_cbor_type_name(expected_type) << std::endl;
}

#if 0
#define ASSERT(x) if (!(x)) { printf("wrong\n"); }
void TestJwLibs(void)
{
    const char* message;

    // We do a set of operations that would normally be split between the device and the TAM, to verify correct operation.

    // Verify JWS (signing).
    json_t* jwkTam = CreateNewJwk("RS256");
    message = json_dumps(jwkTam, 0);
    free((char*)message);
    json_t* jws = json_pack("{s:s}", "payload", "foo");
    bool ok = jose_jws_sig(nullptr, jws, nullptr, jwkTam); // Sign payload.
    message = json_dumps(jws, 0);
    free((char*)message);
    ok = jose_jws_ver(nullptr, jws, nullptr, jwkTam, false); // Verify the signature.
    json_decref(jws); // Free jws.

    // Convert the RS256 JWK to an RSA1_5 JWK.
    // First, copy the JWK.
    message = json_dumps(jwkTam, 0);
    json_error_t error;
    JsonAuto jwkTam2(json_loads(message, 0, &error), true);
    free((char*)message);
    json_t* rsa15 = json_string("RSA1_5");
    int err;
    err = json_object_set(jwkTam2, "alg", rsa15);
    json_decref(rsa15);
    json_t* key_ops = json_object_get(jwkTam2, "key_ops");
    json_t* wrapKey = json_string("wrapKey");
    json_t* unwrapKey = json_string("unwrapKey");
    err = json_array_clear(key_ops);
    err = json_array_append(key_ops, wrapKey);
    err = json_array_append(key_ops, unwrapKey);
    json_decref(wrapKey);
    json_decref(unwrapKey);

    message = json_dumps(jwkTam2, 0);
    free((char*)message);

    // Verify JWE (encryption).
    json_t* jwe = json_object();
    ok = jose_jwe_enc(nullptr, jwe, nullptr, jwkTam2, "foo", 4); // Encrypt
    ASSERT(ok);
    char* jwestr = json_dumps(jwe, 0);
    size_t ptl = 0;
    char *pt = (char*)jose_jwe_dec(nullptr, jwe, nullptr, jwkTam2, &ptl); // Decrypt
    ASSERT(strcmp(pt, "foo") == 0);
    json_decref(jwe); // Free jwe

    // Generate a cert from jwkTam2.
    RSA *rsa = jose_openssl_jwk_to_RSA(nullptr, jwkTam2);
    EVP_PKEY* pkey = EVP_PKEY_new();
    EVP_PKEY_assign_RSA(pkey, rsa);
    X509* x509 = X509_new();
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0); // Current time
    X509_gmtime_adj(X509_get_notAfter(x509), 31536000L); // 1 year from now
    X509_set_pubkey(x509, pkey);
    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char *)"US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (unsigned char *)"MyCompany Inc.", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char *)"localhost", -1, -1, 0);
    X509_set_issuer_name(x509, name);
    X509_sign(x509, pkey, EVP_sha1());

    // Now we want to verify we can generate a JWE that is encrypted with the public key,
    // and decrypted with the private key.

    EVP_PKEY *pkey3 = X509_get_pubkey(x509);
    RSA* rsa3 = EVP_PKEY_get1_RSA(pkey3); // This will only have the public key, no private key.
    JsonAuto jwkTam3(jose_openssl_jwk_from_RSA(nullptr, rsa3), true);
    JsonAuto jwkTam4(CopyToJweKey(jwkTam3, "RSA1_5"), true);
    char* jwkTam2str = json_dumps(jwkTam2, 0);
    char* jwkTam4str = json_dumps(jwkTam4, 0);
    
    json_t* jwe2 = json_object();
    ok = jose_jwe_enc(nullptr, jwe2, nullptr, jwkTam4, "foo", 4); // Encrypt with public key
    ASSERT(ok);
    char* jwestr2 = json_dumps(jwe2, 0);
    free((char*)jwestr2);

    ptl = 0;
    pt = (char*)jose_jwe_dec(nullptr, jwe2, nullptr, jwkTam2, &ptl); // Decrypt with private key
    ASSERT(strcmp(pt, "foo") == 0);
    json_decref(jwe); // Free jwe

    X509_free(x509);
    EVP_PKEY_free(pkey);
    free((char*)jwestr);
}
#endif

static teep_error_code_t save_signing_key_pair(
    _In_ const struct t_cose_key* key_pair,
    _In_z_ const char* private_file_name,
    _In_z_ const char* public_file_name)
{
    EVP_PKEY* pkey = (EVP_PKEY*)key_pair->k.key_ptr;

    // Write key pair with private key, for future use by the TAM.
    FILE* fp = fopen(private_file_name, "wb");
    if (fp == nullptr) {
        return TEEP_ERR_PERMANENT_ERROR;
    }
    int succeeded = PEM_write_PrivateKey(fp, (EVP_PKEY*)key_pair->k.key_ptr,
        NULL, NULL, 0, NULL, NULL);
    fclose(fp);
    if (!succeeded) {
        return TEEP_ERR_PERMANENT_ERROR;
    }

    // Write public key for use by TEEP Agents.
    fp = fopen(public_file_name, "wb");
    if (fp == nullptr) {
        return TEEP_ERR_PERMANENT_ERROR;
    }
    succeeded = PEM_write_PUBKEY(fp, pkey);
    fclose(fp);
    if (!succeeded) {
        return TEEP_ERR_PERMANENT_ERROR;
    }

    return TEEP_ERR_SUCCESS;
}

static teep_error_code_t load_signing_key_pair(
    _Out_ struct t_cose_key* key_pair,
    _In_z_ const char* file_name)
{
    FILE* fp = fopen(file_name, "rb");
    if (fp == nullptr) {
        return TEEP_ERR_PERMANENT_ERROR;
    }
    key_pair->crypto_lib = T_COSE_CRYPTO_LIB_OPENSSL;
    key_pair->k.key_ptr = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
    fclose(fp);
    if (key_pair->k.key_ptr == nullptr) {
        return TEEP_ERR_PERMANENT_ERROR;
    }

    return TEEP_ERR_SUCCESS;
}

/**
 * \brief Make an EC key pair in OpenSSL library form.
 *
 * \param[in] cose_algorithm_id  The algorithm to sign with, for example
 *                               \ref T_COSE_ALGORITHM_ES256.
 * \param[out] key_pair          The key pair. This must be freed.
 *
 * The key made here is fixed and just useful for testing.
 */
enum t_cose_err_t make_ossl_ecdsa_key_pair(int32_t            cose_algorithm_id,
    struct t_cose_key* key_pair)
{
    enum t_cose_err_t  return_value;
    int                ossl_result;
    int                ossl_nid;
    EVP_PKEY* pkey = NULL;
    EVP_PKEY_CTX* ctx;

    switch (cose_algorithm_id) {
    case T_COSE_ALGORITHM_ES256:
        ossl_nid = NID_X9_62_prime256v1;
        break;

    case T_COSE_ALGORITHM_ES384:
        ossl_nid = NID_secp384r1;
        break;

    case T_COSE_ALGORITHM_ES512:
        ossl_nid = NID_secp521r1;
        break;

    default:
        return T_COSE_ERR_UNSUPPORTED_SIGNING_ALG;
    }

    ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (ctx == NULL) {
        return_value = T_COSE_ERR_INSUFFICIENT_MEMORY;
        goto Done;
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        return_value = T_COSE_ERR_FAIL;
        goto Done;
    }

    ossl_result = EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, ossl_nid);
    if (ossl_result != 1) {
        return_value = T_COSE_ERR_FAIL;
        goto Done;
    }

    pkey = EVP_PKEY_new();

    ossl_result = EVP_PKEY_keygen(ctx, &pkey);

    if (ossl_result != 1) {
        return_value = T_COSE_ERR_FAIL;
        goto Done;
    }

    key_pair->k.key_ptr = pkey;
    key_pair->crypto_lib = T_COSE_CRYPTO_LIB_OPENSSL;
    return_value = T_COSE_SUCCESS;

Done:
    return return_value;
}

teep_error_code_t get_signing_key_pair(
    _Out_ struct t_cose_key* key_pair,
    _In_z_ const char* private_file_name,
    _In_z_ const char* public_file_name)
{
    if (load_signing_key_pair(key_pair, private_file_name) == TEEP_ERR_PERMANENT_ERROR) {
        enum t_cose_err_t return_value = make_ossl_ecdsa_key_pair(T_COSE_ALGORITHM_ES256, key_pair);
        if (return_value != T_COSE_SUCCESS) {
            return TEEP_ERR_TEMPORARY_ERROR;
        }

        save_signing_key_pair(key_pair, private_file_name, public_file_name);
    }

    return TEEP_ERR_SUCCESS;
}

teep_error_code_t get_verifying_key_pair(
    _Out_ struct t_cose_key* key_pair,
    _In_z_ const char* public_file_name)
{
    FILE* fp = fopen(public_file_name, "rb");
    if (fp == nullptr) {
        return TEEP_ERR_PERMANENT_ERROR;
    }
    key_pair->crypto_lib = T_COSE_CRYPTO_LIB_OPENSSL;
    key_pair->k.key_ptr = PEM_read_PUBKEY(fp, NULL, NULL, NULL);
    fclose(fp);
    if (key_pair->k.key_ptr == nullptr) {
        return TEEP_ERR_PERMANENT_ERROR;
    }
    return TEEP_ERR_SUCCESS;
}

int TeepInitialize(void)
{
#ifdef TEEP_ENABLE_JSON
    jose_init();
#endif
    return 0;
}

#ifdef TEEP_ENABLE_JSON
json_t* CreateNewJwk(const char* alg)
{
    JsonAuto jwk(json_pack("{s:s}", "alg", alg), true);
    if (jwk == nullptr) {
        return nullptr;
    }

    bool ok = jose_jwk_gen(nullptr, jwk);
    if (!ok) {
        return nullptr;
    }

    return json_incref(jwk);
}

// Take a JWK created for signing, and create a copy of it usable for encryption.
json_t* CopyToJweKey(json_t* jwk1, const char* alg)
{
    // First, copy the JWK.
    const char* message = json_dumps(jwk1, 0);
    json_error_t error;
    JsonAuto jwk2(json_loads(message, 0, &error), true);
    free((char*)message);
    if (jwk2 == nullptr) {
        return nullptr;
    }
    json_t* algstr = json_string(alg);
    int err = json_object_set(jwk2, "alg", algstr);
    if (err != 0) {
        return nullptr;
    }
    json_decref(algstr);
    json_t* key_ops = json_object_get(jwk2, "key_ops");
    if (key_ops != nullptr) {
        err = json_array_clear(key_ops);
    } else {
        key_ops = json_array();
        err = json_object_set_new(jwk2, "key_ops", key_ops);
    }
    if (err != 0) {
        return nullptr;
    }

    if (strcmp(alg, "RSA1_5") == 0) {
        if (json_array_append_new(key_ops, json_string("wrapKey")) < 0) {
            return nullptr;
        }
        if (json_array_append_new(key_ops, json_string("unwrapKey")) < 0) {
            return nullptr;
        }
    } else {
        if (json_array_append_new(key_ops, json_string("sign")) < 0) {
            return nullptr;
        }
        if (json_array_append_new(key_ops, json_string("verify")) < 0) {
            return nullptr;
        }
    }

    return jwk2.Detach();
}

const unsigned char* GetDerCertificate(json_t* jwk, size_t *pCertificateSize)
{
    RSA *rsa = jose_openssl_jwk_to_RSA(nullptr, jwk);

    // Now that we have the RSA key we can do the rest by following the steps at
    // https://stackoverflow.com/questions/256405/programmatically-create-x509-certificate-using-openssl

    // Get the private key.
    EVP_PKEY* pkey = EVP_PKEY_new();
    EVP_PKEY_assign_RSA(pkey, rsa);

    // Create a certificate.
    X509* x509 = X509_new();
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0); // Current time
    X509_gmtime_adj(X509_get_notAfter(x509), 31536000L); // 1 year from now
    X509_set_pubkey(x509, pkey);

    // We set the name of the issuer to the name of the subject, for a self-signed cert.
    X509_NAME* name = X509_get_subject_name(x509);

    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char *)"US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (unsigned char *)"MyCompany Inc.", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char *)"localhost", -1, -1, 0);
    X509_set_issuer_name(x509, name);

    // Now sign the certificate with the private key using SHA1.
    X509_sign(x509, pkey, EVP_sha1());

    // We now have a self-signed certificate and need to get the DER form of it.
    *pCertificateSize = i2d_X509(x509, nullptr);
    unsigned char* cert = (unsigned char*)malloc(*pCertificateSize);
    unsigned char* out = cert;
    i2d_X509(x509, &out);

    X509_free(x509);
    EVP_PKEY_free(pkey); // This also frees rsa.

    return cert;
}

char *DecodeJWS(const json_t *jws, const json_t *jwk)
{
    char *str = nullptr;
    size_t len = 0;

    // Verify signature, if requested.
    if (jwk != nullptr && !jose_jws_ver(nullptr, jws, nullptr, jwk, false)) {
        return nullptr;
    }

    len = jose_b64_dec(json_object_get(jws, "payload"), nullptr, 0);
    str = (char*)malloc(len + 1);
    if (jose_b64_dec(json_object_get(jws, "payload"), str, len) == SIZE_MAX) {
        free(str);
        return nullptr;
    }
    str[len] = 0;
    return str;
}
#endif

void HexPrintBuffer(const void* buffer, size_t length)
{
    const unsigned char* charbuffer = (const unsigned char*)buffer;

    for (size_t i = 0; i < length; i++) {
        printf("%02x ", charbuffer[i]);
    }
    printf("\n");
}

#ifndef TEEP_USE_TEE
#include <random>
teep_error_code_t teep_random(
    _Out_writes_(length) void* buffer,
    size_t length)
{
    std::random_device rd;  // non-deterministic generator
    std::mt19937 gen(rd()); // to seed mersenne twister.
                            // replace the call to rd() with a
                            // constant value to get repeatable
                            // results.
    for (size_t i = 0; i < length; i++) {
        ((uint8_t*)buffer)[i] = gen() % 256;
    }
    return TEEP_ERR_SUCCESS;
}
#endif
