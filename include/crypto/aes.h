#ifndef QEMU_AES_H
#define QEMU_AES_H

#define AES_MAXNR 14
#define AES_BLOCK_SIZE 16

struct aes_key_st {
    uint32_t rd_key[4 *(AES_MAXNR + 1)];
    int rounds;
};
typedef struct aes_key_st AES_KEY;

/* FreeBSD/OpenSSL have their own AES functions with the same names in -lcrypto
 * (which might be pulled in via curl), so redefine to avoid conflicts. */
#define AES_set_encrypt_key QEMU_AES_set_encrypt_key
#define AES_set_decrypt_key QEMU_AES_set_decrypt_key
#define AES_encrypt QEMU_AES_encrypt
#define AES_decrypt QEMU_AES_decrypt

int AES_set_encrypt_key(const unsigned char *userKey, const int bits,
	AES_KEY *key);
int AES_set_decrypt_key(const unsigned char *userKey, const int bits,
	AES_KEY *key);

void AES_encrypt(const unsigned char *in, unsigned char *out,
	const AES_KEY *key);
void AES_decrypt(const unsigned char *in, unsigned char *out,
	const AES_KEY *key);

extern const uint8_t AES_sbox[256];
extern const uint8_t AES_isbox[256];

/* AES ShiftRows and InvShiftRows */
extern const uint8_t AES_shifts[16];
extern const uint8_t AES_ishifts[16];

/* AES InvMixColumns */
/* AES_imc[x][0] = [x].[0e, 09, 0d, 0b]; */
/* AES_imc[x][1] = [x].[0b, 0e, 09, 0d]; */
/* AES_imc[x][2] = [x].[0d, 0b, 0e, 09]; */
/* AES_imc[x][3] = [x].[09, 0d, 0b, 0e]; */
extern const uint32_t AES_imc[256][4];

/*
AES_Te0[x] = S [x].[02, 01, 01, 03];
AES_Te1[x] = S [x].[03, 02, 01, 01];
AES_Te2[x] = S [x].[01, 03, 02, 01];
AES_Te3[x] = S [x].[01, 01, 03, 02];
AES_Te4[x] = S [x].[01, 01, 01, 01];

AES_Td0[x] = Si[x].[0e, 09, 0d, 0b];
AES_Td1[x] = Si[x].[0b, 0e, 09, 0d];
AES_Td2[x] = Si[x].[0d, 0b, 0e, 09];
AES_Td3[x] = Si[x].[09, 0d, 0b, 0e];
AES_Td4[x] = Si[x].[01, 01, 01, 01];
*/

extern const uint32_t AES_Te0[256], AES_Te1[256], AES_Te2[256],
                      AES_Te3[256], AES_Te4[256];
extern const uint32_t AES_Td0[256], AES_Td1[256], AES_Td2[256],
                      AES_Td3[256], AES_Td4[256];

#endif
