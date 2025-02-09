/*****************************************************************************
 *   Ledger App Boilerplate.
 *   (c) 2020 Ledger SAS.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *****************************************************************************/

#include <stdint.h>   // uint*_t
#include <string.h>   // memset, explicit_bzero
#include <stdbool.h>  // bool

#include "crypto.h"

#include "globals.h"

#include "sw.h"

static unsigned char const C_cx_Stark256_n[] = {
    // n: 0x0800000000000010ffffffffffffffffb781126dcae7b2321e66a241adc64d2f
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xb7, 0x81, 0x12, 0x6d, 0xca, 0xe7, 0xb2, 0x32, 0x1e, 0x66, 0xa2, 0x41, 0xad, 0xc6, 0x4d, 0x2f};

// C_cx_secp256k1_n - (C_cx_secp256k1_n % C_cx_Stark256_n)
static unsigned char const STARK_DERIVE_BIAS[] = {
    0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf7,
    0x38, 0xa1, 0x3b, 0x4b, 0x92, 0x0e, 0x94, 0x11, 0xae, 0x6d, 0xa5, 0xf4, 0x0b, 0x03, 0x58, 0xb1};

int crypto_derive_private_key(cx_ecfp_private_key_t *private_key,
                              uint8_t chain_code[static 32],
                              const uint32_t *bip32_path,
                              uint8_t bip32_path_len) {
    uint8_t raw_private_key[32] = {0};

    BEGIN_TRY {
        TRY {
            // derive the seed with bip32_path
            os_perso_derive_node_bip32(CX_CURVE_256K1,
                                       bip32_path,
                                       bip32_path_len,
                                       raw_private_key,
                                       chain_code);
            // new private_key from raw
            cx_ecfp_init_private_key(CX_CURVE_256K1,
                                     raw_private_key,
                                     sizeof(raw_private_key),
                                     private_key);
        }
        CATCH_OTHER(e) {
            THROW(e);
        }
        FINALLY {
            explicit_bzero(&raw_private_key, sizeof(raw_private_key));
        }
    }
    END_TRY;

    return 0;
}

int crypto_init_public_key(cx_ecfp_private_key_t *private_key,
                           cx_ecfp_public_key_t *public_key,
                           uint8_t raw_public_key[static 64]) {
    // generate corresponding public key
    cx_ecfp_generate_pair(CX_CURVE_Stark256, public_key, private_key, 1);

    memmove(raw_public_key, public_key->W + 1, 64);

    return 0;
}

int crypto_sign_message() {
    cx_ecfp_private_key_t private_key = {0};
    uint32_t info = 0;
    int sig_len = 0;

    // derive private key according to BIP32 path
    eip2645_derive_private_key(&private_key,
                              G_context.bip32_path,
                              G_context.bip32_path_len);

    BEGIN_TRY {
        TRY {
            sig_len = cx_ecdsa_sign(&private_key,
                                    CX_RND_RFC6979 | CX_LAST,
                                    CX_SHA256,
                                    G_context.hash_info.m_hash,
                                    sizeof(G_context.hash_info.m_hash),
                                    G_context.hash_info.signature,
                                    sizeof(G_context.hash_info.signature),
                                    &info);
            PRINTF("Signature: %.*H\n", sig_len, G_context.hash_info.signature);
        }
        CATCH_OTHER(e) {
            THROW(e);
        }
        FINALLY {
            explicit_bzero(&private_key, sizeof(private_key));
        }
    }
    END_TRY;

    if (sig_len < 0) {
        return -1;
    }

    G_context.hash_info.signature_len = sig_len;
    G_context.hash_info.v = (uint8_t)(info & CX_ECCINFO_PARITY_ODD);

    return 0;
}

int eip2645_derive_private_key(cx_ecfp_private_key_t *private_key,
                              const uint32_t *bip32_path,
                              uint8_t bip32_path_len) {

    uint8_t raw_private_key[32] = {0};
    uint8_t tmp[33] = {0};
    uint8_t index = 0;

    BEGIN_TRY {
        TRY {

            // Sanity check
            if ((bip32_path_len < 2) || (bip32_path[0] != STARK_BIP32_PATH_0)) {
                    PRINTF("Invalid Stark derivation path %d \n", bip32_path[0]);
                    THROW(SW_DISPLAY_BIP32_PATH_FAIL);
             }

            // derive the seed with bip32_path
            os_perso_derive_node_bip32(CX_CURVE_256K1,
                                       bip32_path,
                                       bip32_path_len,
                                       tmp,
                                       NULL);
            for (;;) {
                tmp[32] = index;
                cx_hash_sha256(tmp, 33, raw_private_key, 32);
                PRINTF("Key hash %.*H\n", 32, raw_private_key);
                if (cx_math_cmp(raw_private_key, STARK_DERIVE_BIAS, 32) < 0) {
                    cx_math_modm(raw_private_key, 32, C_cx_Stark256_n, 32);
                    break;
                }
                index++;
            }

            // new private_key from raw
            cx_ecfp_init_private_key(CX_CURVE_Stark256,
                                     raw_private_key,
                                     sizeof(raw_private_key),
                                     private_key);
        }
        CATCH_OTHER(e) {
            THROW(e);
        }
        FINALLY {
            explicit_bzero(&raw_private_key, sizeof(raw_private_key));
            explicit_bzero(&tmp, sizeof(tmp));
        }
    }
    END_TRY;

    return 0;
}
