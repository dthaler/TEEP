// Copyright (c) Microsoft Corporation
// SPDX-License-Identifier: MIT
#pragma once

/* Avoid confusing openssl */
#if defined(_WIN32)
#undef _WIN32
#endif

#include <stdio.h>
#define OE_NO_SAL 1
#ifdef TEEP_USE_TEE
#include <openenclave/enclave.h>
#endif
#define GETPID_IS_MEANINGLESS

#include <string.h>
#include <time.h>

#if defined(OE_USE_OPTEE)
#include <tee_api.h>
#include "optee/tcps_ctype_optee_t.h"
#else
unsigned long _lrotl(unsigned long val, int shift);
unsigned long _lrotr(unsigned long value, int shift);
#endif

void RAND_screen(void);

/* Hack: Rename openssl's ECDSA_verify, to avoid conflicting with RIoT's ECDSA_verify */
#define ECDSA_verify openssl_ECDSA_verify
