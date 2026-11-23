/* <arpa/inet.h> for the MSVC port: the byte-order functions only, over the
 C runtime's byte-swap functions, so nothing links against Winsock. */
#ifndef AN_MSVC_ARPA_INET_H
#define AN_MSVC_ARPA_INET_H

#include <stdlib.h>

#define htonl _byteswap_ulong
#define ntohl _byteswap_ulong
#define htons _byteswap_ushort
#define ntohs _byteswap_ushort

#endif
