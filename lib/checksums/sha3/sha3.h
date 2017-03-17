#ifndef _RM_CHECKSUM_SHA3
#define _RM_CHECKSUM_SHA3

#include <stdlib.h>

void sha3_Init256(void *priv);
void sha3_Init384(void *priv);
void sha3_Init512(void *priv);

void sha3_Update(void *priv, void const *bufIn, size_t len);
void const * sha3_Finalize(void *priv);

#endif /* _RM_CHECKSUM_SHA3 */
