/*******************************************************************************
*   Ledger Blue - Bitcoin Wallet
*   (c) 2016 Ledger
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
********************************************************************************/

#include "internal.h"
#include "apdu_constants.h"

#define SIGHASH_ALL 0x01

unsigned short apdu_hash_sign() {

    PRINTF("\n### HASH_SIGN:\n");

    unsigned long int lockTime;
    unsigned long int expiry;
    uint32_t sighashType;
    unsigned char dataBuffer[8];
    unsigned char hash1[32];
    unsigned char hash2[32];
    unsigned char authorizationLength;
    unsigned char *parameters = G_io_apdu_buffer + ISO_OFFSET_CDATA;
    unsigned char *authorization;
    unsigned short sw;
    unsigned char keyPath[MAX_BIP32_PATH_LENGTH];
    cx_sha256_t localHash;

    SB_CHECK(N_btchip.bkp.config.operationMode);
    switch (SB_GET(N_btchip.bkp.config.operationMode)) {
    case MODE_WALLET:
        break;
    default:
        return SW_CONDITIONS_OF_USE_NOT_SATISFIED;
    }

    if ((G_io_apdu_buffer[ISO_OFFSET_P1] != 0) &&
        (G_io_apdu_buffer[ISO_OFFSET_P2] != 0)) {
        return SW_INCORRECT_P1_P2;
    }

    if (G_io_apdu_buffer[ISO_OFFSET_LC] < (1 + 1 + 4 + 1)) {
        return SW_INCORRECT_LENGTH;
    }

    // Check state
    BEGIN_TRY {
        TRY {
            set_check_internal_structure_integrity(0);
            if (context_D.transactionContext.transactionState !=
                TRANSACTION_SIGN_READY) {
                L_DEBUG_APP(("Invalid transaction state %d\n",
                     context_D.transactionContext.transactionState));
                sw = SW_CONDITIONS_OF_USE_NOT_SATISFIED;
                goto discardTransaction;
            }

            // Read parameters
            if (G_io_apdu_buffer[ISO_OFFSET_CDATA] > MAX_BIP32_PATH) {
                sw = SW_INCORRECT_DATA;
            discardTransaction:
                CLOSE_TRY;
                goto catch_discardTransaction;
            }
            os_memmove(keyPath, G_io_apdu_buffer + ISO_OFFSET_CDATA,
                       MAX_BIP32_PATH_LENGTH);
            parameters += (4 * G_io_apdu_buffer[ISO_OFFSET_CDATA]) + 1;

            lockTime = read_u32(parameters, 1, 0);
            parameters += 4;
            expiry = read_u32(parameters, 1, 0);
            parameters += 4;
            sighashType = *(parameters++);
            PRINTF("SighashType: %d\n", sighashType);

            if (((N_btchip.bkp.config.options &
                  OPTION_FREE_SIGHASHTYPE) == 0)) {
                // if bitcoin cash OR forkid is set, then use the fork id
                if (G_coin_config->forkid) {
#define SIGHASH_FORKID 0x40
                    if (sighashType != (SIGHASH_ALL | SIGHASH_FORKID)) {
                        sw = SW_INCORRECT_DATA;
                        goto discardTransaction;
                    }
                    sighashType |= (G_coin_config->forkid << 8);
                } else {
                    if (sighashType != SIGHASH_ALL) {
                        sw = SW_INCORRECT_DATA;
                        goto discardTransaction;
                    }
                }
            }

            // Read transaction parameters

            // Fetch the private key

            private_derive_keypair(keyPath, 1, NULL);

            // TODO optional : check the public key against the associated non
            // blank input to sign

            // Finalize the hash

            write_u32_le(dataBuffer, lockTime);
            write_u32_le(dataBuffer + 4, expiry);
            PRINTF("Finalize hash with %.*H\n", sizeof(dataBuffer), dataBuffer); 


            blake256_Update(&context_D.transactionHashPrefix, dataBuffer,  sizeof(dataBuffer));
            blake256_Final(&context_D.transactionHashPrefix, hash1);
            PRINTF("Hash1 %.*H\n", sizeof(hash1), hash1);

            blake256_Final(&context_D.transactionHashWitness, hash2);

            blake256_Init(&context_D.transactionHashPrefix);

            write_u32_le(dataBuffer, sighashType);
            // include sighash type
            PRINTF("Sighash type: %.*H\n", 4, dataBuffer);
            blake256_Update(&context_D.transactionHashPrefix, dataBuffer,  4);
            // include prefix_hash
            PRINTF("Prefix hash: %.*H\n", sizeof(hash1), hash1);
            blake256_Update(&context_D.transactionHashPrefix, hash1,  sizeof(hash1));
            // include witness_hash
            PRINTF("Witness hash: %.*H\n", sizeof(hash2), hash2);
            blake256_Update(&context_D.transactionHashPrefix, hash2,  sizeof(hash2));

            // final signature hash
            blake256_Final(&context_D.transactionHashPrefix, hash2);
            PRINTF("Hash to sign: %.*H\n", sizeof(hash2), hash2);


            // Sign
            PRINTF("Pub key: %.*H\n", sizeof(public_key_D.W), public_key_D.W);
            signverify_finalhash(
                &private_key_D, 1, hash2, sizeof(hash2),
                G_io_apdu_buffer, sizeof(G_io_apdu_buffer),
                ((N_btchip.bkp.config.options &
                  OPTION_DETERMINISTIC_SIGNATURE) != 0));

            context_D.outLength = G_io_apdu_buffer[1] + 2;
            G_io_apdu_buffer[context_D.outLength++] = sighashType;

            sw = SW_OK;

            // Then discard the transaction and reply
        }
        CATCH_ALL {
            sw = SW_TECHNICAL_DETAILS(0xF);
        catch_discardTransaction:
            context_D.transactionContext.transactionState =
                TRANSACTION_NONE;
        }
        FINALLY {
            set_check_internal_structure_integrity(1);
            return sw;
        }
    }
    END_TRY;
}
