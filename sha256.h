#ifndef INKDAY_SHA256_H
#define INKDAY_SHA256_H

#include <stddef.h>
#include <stdint.h>

enum
{
  SHA256_DIGEST_SIZE = 32
};

/* One-shot SHA-256 (FIPS 180-4); used for the OAuth PKCE challenge. */
void sha256(const void *data, size_t length, uint8_t digest[SHA256_DIGEST_SIZE]);

#endif
